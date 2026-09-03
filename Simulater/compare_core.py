# compare_core.py
"""
시뮬레이터 통신 로그(CSV)와 DUT(TeraTerm) 로그를 비교하는 순수 로직 모듈.
GUI/CLI 어느 쪽에서도 재사용할 수 있도록 print/argparse를 포함하지 않음.
"""
import csv
import re
from datetime import datetime
from statistics import mean, pstdev

SIM_NOTE_RE = re.compile(
    r'phase=(?P<phase>\d+);delay_ms=(?P<delay>[\d.]+);meter_value=(?P<val>\d+)'
)
LOG_LINE_RE = re.compile(
    r'^\[(?P<wall>\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d+)\]\s*'
    r'\[(?P<tick>[\d.]+)\]\[(?P<level>\w+)\]\[(?P<tag>\w+)\]\s*(?P<msg>.*)$'
)
EPC_MSG_RE = re.compile(
    r'test=(?P<test>\w+),seq=(?P<seq>\d+),res=(?P<res>\w+),id=(?P<id>\w+),val=(?P<val>\d+)'
)
ALARM_METER_RE = re.compile(
    r'\[\[AlarmA\(meter\)\]\]\s*set\s*(?P<time>\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})'
)


class CompareError(Exception):
    """파일 읽기/파싱 단계에서 발생하는 오류를 GUI에 전달하기 위한 예외."""
    pass


def parse_sim_csv(path, test_id_filter=None):
    records = []
    try:
        with open(path, newline='', encoding='utf-8') as f:
            reader = csv.DictReader(f)
            for row in reader:
                if test_id_filter is not None and row.get('test_id', '').strip() != str(test_id_filter):
                    continue
                note = row.get('note', '') or ''
                m = SIM_NOTE_RE.search(note)
                sim_val = int(m.group('val')) if m else None
                records.append({
                    'seq': int(row['seq']),
                    'ts': float(row['timestamp']),
                    'req_valid': row.get('req_valid', '').strip().lower() == 'true',
                    'mode': row.get('mode', ''),
                    'sim_value': sim_val,
                    'note': note,
                })
    except FileNotFoundError:
        raise CompareError(f"시뮬레이터 CSV 파일을 찾을 수 없습니다: {path}")
    except Exception as e:
        raise CompareError(f"시뮬레이터 CSV 파싱 오류: {e}")
    records.sort(key=lambda r: r['seq'])
    return records


def parse_dut_log(path, test_name_filter=None, seq_offset=0):
    epc_records, alarm_times = [], []
    try:
        with open(path, encoding='utf-8', errors='ignore') as f:
            for line in f:
                m = LOG_LINE_RE.match(line.rstrip('\n'))
                if not m:
                    continue
                wall = datetime.strptime(m.group('wall'), '%Y-%m-%d %H:%M:%S.%f')
                msg = m.group('msg')
                if m.group('tag') == 'EPC':
                    em = EPC_MSG_RE.search(msg)
                    if em and (test_name_filter is None or em.group('test') == test_name_filter):
                        epc_records.append({
                            'seq': int(em.group('seq')) - seq_offset,
                            'ts': wall,
                            'test': em.group('test'),
                            'res': em.group('res'),
                            'id': em.group('id'),
                            'dut_value': int(em.group('val')),
                        })
                am = ALARM_METER_RE.search(msg)
                if am:
                    alarm_times.append(datetime.strptime(am.group('time'), '%Y-%m-%d %H:%M:%S'))
    except FileNotFoundError:
        raise CompareError(f"DUT 로그 파일을 찾을 수 없습니다: {path}")
    except Exception as e:
        raise CompareError(f"DUT 로그 파싱 오류: {e}")
    epc_records.sort(key=lambda r: r['seq'])
    return epc_records, alarm_times


def check_alarm_intervals(alarm_times, expected, tol):
    intervals, issues = [], []
    for prev, cur in zip(alarm_times, alarm_times[1:]):
        d = (cur - prev).total_seconds()
        intervals.append(d)
        if abs(d - expected) > tol:
            issues.append(f"{prev} -> {cur} : {d:.1f}s (기대 {expected:.1f}±{tol:.1f}s 벗어남)")
    return intervals, issues


def run_compare(sim_csv_path, dut_log_path, sim_test_id="1", dut_test_name="TEST1",
                 seq_offset=0, interval=10.0, interval_tol=1.0, time_tol=3.0,
                 exclude_first_n=0):
    """
    exclude_first_n: seq 기준 정렬 후 맨 앞 N건을 시뮬레이터/DUT 양쪽 리스트에서 모두 제외.
    장치 최초 부팅 시 (RTC 주기 스케줄과 무관하게) 즉시 수행되는 초기 검침 1건을
    통계/판정에서 빼기 위한 용도. 0이면 기존과 동일하게 전부 비교.
    """
    sim_records = parse_sim_csv(sim_csv_path, sim_test_id)
    dut_records, alarm_times = parse_dut_log(dut_log_path, dut_test_name, seq_offset)

    if exclude_first_n > 0:
        excluded_sim = sim_records[:exclude_first_n]
        excluded_dut = dut_records[:exclude_first_n]
        sim_records = sim_records[exclude_first_n:]
        dut_records = dut_records[exclude_first_n:]
    else:
        excluded_sim, excluded_dut = [], []

    sim_by_seq = {r['seq']: r for r in sim_records}
    dut_by_seq = {r['seq']: r for r in dut_records}
    all_seqs = sorted(set(sim_by_seq) | set(dut_by_seq))

    rows = []
    value_mismatches = missing_in_dut = missing_in_sim = 0
    time_deltas = []
    interval_issues = []
    prev_dut_ts = None

    for seq in all_seqs:
        s, d = sim_by_seq.get(seq), dut_by_seq.get(seq)
        sim_value = s['sim_value'] if s else None
        dut_value = d['dut_value'] if d else None
        value_match, delta_sec, interval_sec = '', '', ''
        status = 'OK'

        if s is None:
            missing_in_sim += 1
            status = 'MISSING_SIM'
            note = '시뮬레이터 로그에 없음(DUT만 존재)'
        elif d is None:
            missing_in_dut += 1
            status = 'MISSING_DUT'
            note = 'DUT 로그에 없음(무응답/누락 의심)'
        else:
            value_match = (sim_value == dut_value)
            if not value_match:
                value_mismatches += 1
                status = 'MISMATCH'
            delta = (d['ts'] - datetime.fromtimestamp(s['ts'])).total_seconds()
            delta_sec = round(delta, 3)
            time_deltas.append(delta)
            if abs(delta) > time_tol:
                status = 'TIME_DELTA' if status == 'OK' else status
                note = f'송수신 시각차 {delta:.2f}s 초과'
            else:
                note = 'OK' if status == 'OK' else note if status != 'OK' else 'OK'

            if prev_dut_ts is not None:
                iv = (d['ts'] - prev_dut_ts).total_seconds()
                interval_sec = round(iv, 3)
                if abs(iv - interval) > interval_tol:
                    interval_issues.append(
                        f"seq={seq}: EPC 로그 기준 주기 {iv:.2f}s (기대 {interval}±{interval_tol}s 벗어남)"
                    )
            prev_dut_ts = d['ts']

        rows.append({
            'seq': seq, 'sim_value': sim_value, 'dut_value': dut_value,
            'value_match': value_match, 'delta_sec': delta_sec,
            'interval_sec': interval_sec, 'status': status, 'note': note,
        })

    alarm_intervals, alarm_issues = check_alarm_intervals(alarm_times, interval, interval_tol)

    overall_ok = (value_mismatches == 0 and missing_in_dut == 0 and missing_in_sim == 0
                  and not alarm_issues and not interval_issues)

    summary = {
        'sim_count': len(sim_records),
        'dut_count': len(dut_records),
        'excluded_count': exclude_first_n,
        'excluded_sim_seqs': [r['seq'] for r in excluded_sim],
        'excluded_dut_seqs': [r['seq'] for r in excluded_dut],
        'value_mismatches': value_mismatches,
        'missing_in_dut': missing_in_dut,
        'missing_in_sim': missing_in_sim,
        'time_delta_avg': mean(time_deltas) if time_deltas else None,
        'time_delta_max': max(time_deltas, key=abs) if time_deltas else None,
        'alarm_interval_avg': mean(alarm_intervals) if alarm_intervals else None,
        'alarm_interval_std': pstdev(alarm_intervals) if len(alarm_intervals) > 1 else None,
        'alarm_issues': alarm_issues,
        'interval_issues': interval_issues,
        'overall_ok': overall_ok,
    }
    return rows, summary

def save_report_csv(rows, path):
    if not rows:
        return
    with open(path, 'w', newline='', encoding='utf-8-sig') as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)

