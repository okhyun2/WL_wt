# self_diagnosis_core.py
"""
DUT 자가진단 로그(TeraTerm 등)를 파싱해서
'실제 시료가 로그에 남긴 판정/결함 코드' 자체를 근거로 통계를 산출한다.
(사람이 예상 결함을 미리 체크하는 방식은 사용하지 않는다.)
시뮬레이터 CSV는 전혀 사용하지 않는다 (실 하드웨어 고장 시료 대상).
"""
import csv
import re
from datetime import datetime

# ---------------- 로그 형식 ----------------
LOG_LINE_RE = re.compile(
    r'^\[(?P<wall>\d{4}-\d{2}-\d{2}[ T]\d{2}:\d{2}:\d{2}\.\d+'
    r'|\d{2}:\d{2}:\d{2}\.\d+'
    r'|\d{6}\.\d+)\]\s*'
    r'(?:\[(?P<tick>[\d.]+)\]\s*)?'
    r'\[(?P<level>\w+)\]\[(?P<tag>\w+)\]\s*(?P<msg>.*)$'
)
SELFDIAG_MSG_RE = re.compile(
    r'test=(?P<test>\w+),seq=(?P<seq>\d+),dut=(?P<dut>[^,]+),'
    r'(?:resetCause=(?P<resetCause>\w+),)?'
    r'fault=(?P<fault>[\w+]+),judge=(?P<judge>\w+)'
)
# ---------------- 결함 모듈 분류 ----------------
FAULT_MODULES_ALL = {
    "BUZZ", "CRC", "ADC", "DBG", "METER_LINE", "METER",
    "NBIOT", "NFC", "TEMP", "EWDT", "GPIO",
}
# METER(계량기 응답), METER_LINE(계량기 UART 배선/포트)을 모두 계량기측 고장으로 분류
METER_MODULES = {"METER", "METER_LINE"}
TERMINAL_MODULES = FAULT_MODULES_ALL - METER_MODULES

FAULT_LABEL_KR = {
    "BUZZ": "부저 이상",
    "CRC": "CRC 연산 이상",
    "ADC": "배터리 전압(ADC) 이상",
    "DBG": "디버그 UART 이상",
    "METER_LINE": "계량기 UART 배선/라인 이상",   # 신규
    "METER": "계량기 응답 이상",
    "NBIOT": "NB-IoT 모듈 이상",
    "NFC": "NFC 모듈 이상",
    "TEMP": "온습도(AUX I2C) 센서 이상",
    "EWDT": "외부 Watchdog 동작",
    "GPIO": "GPIO 입력 이상",
    "NONE": "고장 없음",
}

JUDGE_LABEL_KR = {
    "NORMAL": "정상",
    "TERMINAL_FAULT": "단말기 자체 불량",
    "METER_FAULT": "계량기 기기 불량",
}


class SelfDiagError(Exception):
    """파싱/집계 중 사용자에게 알려야 할 오류."""
    pass


def _parse_wall_time(wall_str):
    """DUT 로그 타임스탬프 문자열을 datetime으로 변환. 날짜가 없으면 오늘 날짜로 보정."""
    if '-' in wall_str:
        return datetime.strptime(wall_str, '%Y-%m-%d %H:%M:%S.%f')
    today = datetime.now().strftime('%Y-%m-%d')
    if ':' in wall_str:
        return datetime.strptime(f"{today} {wall_str}", '%Y-%m-%d %H:%M:%S.%f')
    hh, mm, rest = wall_str[0:2], wall_str[2:4], wall_str[4:]
    return datetime.strptime(f"{today} {hh}:{mm}:{rest}", '%Y-%m-%d %H:%M:%S.%f')

def parse_selfdiag_log(path, test_name_filter=None):
    """DUT 자가진단 로그 파일을 파싱해서 레코드 리스트로 반환한다."""
    records = []
    try:
        with open(path, encoding='utf-8', errors='replace') as f:
            for line in f:
                m = LOG_LINE_RE.match(line.rstrip('\n'))
                if not m:
                    continue
                if m.group('tag') != 'SELFDIAG':
                    continue
                sm = SELFDIAG_MSG_RE.search(m.group('msg'))
                if not sm:
                    continue
                if test_name_filter and sm.group('test') != test_name_filter:
                    continue
                wall = _parse_wall_time(m.group('wall'))
                fault_str = sm.group('fault')
                fault_set = set() if fault_str.upper() == 'NONE' else set(fault_str.split('+'))
                unknown = fault_set - FAULT_MODULES_ALL
                if unknown:
                    raise SelfDiagError(f"알 수 없는 결함 코드가 로그에 있습니다: {unknown}")
                records.append({
                    'seq': int(sm.group('seq')),
                    'ts': wall,
                    'dut': sm.group('dut'),
                    'fault_set': fault_set,
                    'judge': sm.group('judge'),
                })
    except FileNotFoundError:
        raise SelfDiagError(f"DUT 로그 파일을 찾을 수 없습니다: {path}")
    except SelfDiagError:
        raise
    except Exception as e:
        raise SelfDiagError(f"DUT 로그 파싱 오류: {e}")
    records.sort(key=lambda r: r['seq'])
    records = [r for r in records if r.get('seq', 0) > 0]
    return records

def summarize_fault_distribution(records):
    """
    사람이 지정한 '예상 고장'과 비교하지 않고,
    DUT 로그에 실제로 찍힌 fault_set/judge 값만으로
    판정별·고장 모듈별 발생 통계를 집계한다.
    (fault=NONE 인 '정상' 회차도 포함하여 집계)
    """
    total = len(records)

    judge_counts = {}
    for r in records:
        judge_counts[r['judge']] = judge_counts.get(r['judge'], 0) + 1

    fault_module_counts = {m: 0 for m in sorted(FAULT_MODULES_ALL)}
    normal_count = 0
    for r in records:
        if not r['fault_set']:
            normal_count += 1
        for m in r['fault_set']:
            fault_module_counts[m] = fault_module_counts.get(m, 0) + 1

    def pct(n):
        return round(n / total * 100, 2) if total else 0.0

    return {
        'total_runs': total,
        'normal_count': normal_count,
        'normal_pct': pct(normal_count),
        'judge_counts': judge_counts,
        'judge_pct': {j: pct(c) for j, c in judge_counts.items()},
        'fault_module_counts': fault_module_counts,
        'fault_module_pct': {m: pct(c) for m, c in fault_module_counts.items()},
    }


def save_selfdiag_report_csv(records, stats, path):
    """회차별 원본 기록 + 판정/모듈별 통계 요약을 CSV로 저장한다."""
    if not records:
        return
    fieldnames = ['seq', 'ts', 'dut', 'fault_set', 'judge']
    with open(path, 'w', newline='', encoding='utf-8-sig') as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        for r in records:
            writer.writerow({
                'seq': r['seq'],
                'ts': r['ts'],
                'dut': r['dut'],
                'fault_set': '+'.join(sorted(r['fault_set'])) or 'NONE',
                'judge': r['judge'],
            })

        writer.writerow({})
        writer.writerow({'seq': '요약', 'ts': f"전체 {stats['total_runs']}건",
                          'dut': f"정상 {stats['normal_count']}건({stats['normal_pct']}%)"})

        writer.writerow({})
        writer.writerow({'seq': '판정별 분포'})
        for j, cnt in sorted(stats['judge_counts'].items(), key=lambda x: -x[1]):
            writer.writerow({
                'ts': JUDGE_LABEL_KR.get(j, j),
                'dut': cnt,
                'fault_set': f"{stats['judge_pct'][j]}%",
            })

        writer.writerow({})
        writer.writerow({'seq': '고장 모듈별 검출 횟수'})
        for m, cnt in sorted(stats['fault_module_counts'].items(), key=lambda x: -x[1]):
            writer.writerow({
                'ts': FAULT_LABEL_KR.get(m, m),
                'dut': cnt,
                'fault_set': f"{stats['fault_module_pct'][m]}%",
            })

