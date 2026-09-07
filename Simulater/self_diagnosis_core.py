# self_diagnosis_core.py
"""
DUT 자가진단 로그(TeraTerm 등)를 파싱해서
'이번 시료에 실제로 주입된 결함 목록'과 비교, 통계만 산출한다.
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
    r'test=(?P<test>\w+),seq=(?P<seq>\d+),dut=(?P<dut>\w+),'
    r'fault=(?P<fault>[\w+]+),judge=(?P<judge>\w+)'
)

# ---------------- 결함 모듈 분류 ----------------
FAULT_MODULES_ALL = {
    "BUZZ", "CRC", "ADC", "DBG", "METER",
    "NBIOT", "NFC", "TEMP", "EWDT", "GPIO",
}
# METER(계량기 UART 응답)만 계량기측 고장으로 분류, 나머지는 모두 단말(DUT) 고장
METER_MODULES = {"METER"}
TERMINAL_MODULES = FAULT_MODULES_ALL - METER_MODULES

FAULT_LABEL_KR = {
    "BUZZ": "부저 이상",
    "CRC": "CRC 연산 이상",
    "ADC": "배터리 전압(ADC) 이상",
    "DBG": "디버그 UART 이상",
    "METER": "계량기 응답 이상",
    "NBIOT": "NB-IoT 모듈 이상",
    "NFC": "NFC 모듈 이상",
    "TEMP": "온습도(AUX I2C) 센서 이상",
    "EWDT": "외부 감시타이머 이상",
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


def expected_judge(injected_faults: set) -> str:
    """주입된 결함 집합으로부터 DUT가 내려야 할 최종 판정을 계산한다."""
    if not injected_faults:
        return "NORMAL"
    if injected_faults & TERMINAL_MODULES:
        return "TERMINAL_FAULT"
    if injected_faults & METER_MODULES:
        return "METER_FAULT"
    return "NORMAL"

def _parse_wall_time(wall_str):
    """DUT 로그 타임스탬프 문자열을 datetime으로 변환. 날짜가 없으면 오늘 날짜로 보정."""
    if '-' in wall_str:
        return datetime.strptime(wall_str, '%Y-%m-%d %H:%M:%S.%f')
    today = datetime.now().strftime('%Y-%m-%d')
    if ':' in wall_str:
        return datetime.strptime(f"{today} {wall_str}", '%Y-%m-%d %H:%M:%S.%f')
    hh, mm, rest = wall_str[0:2], wall_str[2:4], wall_str[4:]
    return datetime.strptime(f"{today} {hh}:{mm}:{rest}", '%Y-%m-%d %H:%M:%S.%f')

def parse_selfdiag_log(path, test_name_filter=None, dut_label_filter=None):
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
                dut_label = sm.group('dut')
                if dut_label_filter and dut_label != dut_label_filter:
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
                    'dut': dut_label,
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
    return records


def evaluate_selfdiag_run(records, injected_faults):
    """레코드별로 기대 결함/판정과 실제 결과를 비교해 행(row)을 만든다."""
    exp_judge = expected_judge(injected_faults)
    rows = []
    for r in records:
        missed = sorted(injected_faults - r['fault_set'])          # 주입했는데 못 잡은 결함
        false_positive = sorted(r['fault_set'] - injected_faults)   # 주입하지 않았는데 잡힌 결함
        judge_ok = (r['judge'] == exp_judge)
        exact_match = judge_ok and not missed and not false_positive
        rows.append({
            'seq': r['seq'],
            'ts': r['ts'],
            'dut': r['dut'],
            'fault_set': r['fault_set'],
            'judge': r['judge'],
            'missed': missed,
            'false_positive': false_positive,
            'judge_ok': judge_ok,
            'exact_match': exact_match,
        })
    return rows


def summarize_selfdiag(rows, injected_faults):
    """
    30회 반복 제한은 DUT 펌웨어가 관리하므로 여기서는 강제하지 않고,
    주어진 로그 전체(len(rows))를 그대로 집계한다.
    """
    total = len(rows)
    exact = sum(1 for r in rows if r['exact_match'])
    accuracy = (exact / total * 100) if total else 0.0

    distinct_fault_sets = {frozenset(r['fault_set']) for r in rows}
    distinct_judges = {r['judge'] for r in rows}
    reproducible = len(distinct_fault_sets) <= 1 and len(distinct_judges) <= 1

    per_module_miss = {m: 0 for m in injected_faults}
    per_module_falsepos = {}
    for r in rows:
        for m in r['missed']:
            per_module_miss[m] += 1
        for m in r['false_positive']:
            per_module_falsepos[m] = per_module_falsepos.get(m, 0) + 1

    return {
        'total_runs': total,
        'exact_match_count': exact,
        'accuracy_pct': round(accuracy, 2),
        'reproducible': reproducible,
        'distinct_fault_sets': [sorted(s) for s in distinct_fault_sets],
        'distinct_judges': sorted(distinct_judges),
        'per_module_miss_count': per_module_miss,
        'per_module_falsepos_count': per_module_falsepos,
        'overall_ok': (
            total > 0
            and accuracy >= 95.0
            and reproducible
            and not per_module_falsepos
        ),
    }


def save_selfdiag_report_csv(rows, summary, path):
    if not rows:
        return
    fieldnames = ['seq', 'ts', 'dut', 'fault_set', 'judge', 'missed',
                  'false_positive', 'judge_ok', 'exact_match']
    with open(path, 'w', newline='', encoding='utf-8-sig') as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        for r in rows:
            writer.writerow({
                'seq': r['seq'],
                'ts': r['ts'],
                'dut': r['dut'],
                'fault_set': '+'.join(sorted(r['fault_set'])) or 'NONE',
                'judge': r['judge'],
                'missed': '+'.join(r['missed']) or '',
                'false_positive': '+'.join(r['false_positive']) or '',
                'judge_ok': r['judge_ok'],
                'exact_match': r['exact_match'],
            })
        writer.writerow({})
        writer.writerow({'seq': '요약', 'ts': f"정확도 {summary['accuracy_pct']}%",
                          'dut': f"재현성 {summary['reproducible']}",
                          'judge': f"판정 {'PASS' if summary['overall_ok'] else 'FAIL'}"})

