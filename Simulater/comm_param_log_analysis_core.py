# comm_param_log_analysis_core.py
"""
시험 7 (무선환경 기반 통신 파라미터 자동설정) DUT 로그 분석 순수 로직 모듈.
compare_core.py / self_diagnosis_core.py와 동일한 스타일:
- print/argparse 없음, GUI/CLI 어느 쪽에서도 재사용 가능
- 예외는 CommParamLogError로만 밖에 알림

실제 DUT 출력 포맷 (App_CommTest7RunCycle):
[2026-09-15 10:00:01.123][NOTI][TEST7] test=TEST7,seq=1,case=A_normal,rssi=-70,rsrp=-75,
    attemptIdx=0,allFailed=0,signal=STRONG,success=HIGH,periodH=1,nightOnly=0,
    expSignal=STRONG,expSuccess=HIGH,expPeriodH=1,expNightOnly=0,result=PASS

FAIL 시 mismatch 필드가 뒤에 추가됨:
    result=FAIL,mismatch=nightOnly(exp=1,act=0);

전체 완료 시:
    test=TEST7,summary=ALL_COMPLETE,total=10,pass=10,fail=0
"""
import csv
import re
from datetime import datetime

# ---------------- 공통 로그 라인 포맷 (compare_core.py / self_diagnosis_core.py와 동일) ----------------
LOG_LINE_RE = re.compile(
    r'^\[(?P<wall>\d{4}-\d{2}-\d{2}[ T]\d{2}:\d{2}:\d{2}\.\d+'
    r'|\d{2}:\d{2}:\d{2}\.\d+'
    r'|\d{6}\.\d+)\]\s*'
    r'(?:\[(?P<tick>[\d.]+)\]\s*)?'
    r'\[(?P<level>\w+)\]\[(?P<tag>\w+)\]\s*(?P<msg>.*)$'
)

# ---------------- 통신 파라미터 자동설정(TEST7) 구조화 포맷 (App_CommTest7RunCycle 실제 출력) ----------------
# ---------------- 통신 파라미터 자동설정(TEST7) 구조화 포맷 ----------------
# 실제 DUT는 버퍼 오버플로우 방지를 위해 한 사이클을 두 줄로 나눠 출력한다.
#  1줄째: case/rssi/rsrp/attemptIdx/allFailed/signal/success/periodH/nightOnly
#  2줄째: expSignal/expSuccess/expPeriodH/expNightOnly/result(,mismatch)
COMM_PARAM_LINE1_RE = re.compile(
    r'test=TEST7,seq=(?P<seq>\d+),case=(?P<case>\w+),'
    r'rssi=(?P<rssi>-?\d+),rsrp=(?P<rsrp>-?\d+),'
    r'attemptIdx=(?P<attemptIdx>\d+),allFailed=(?P<allFailed>[01]),'
    r'signal=(?P<signal>\w+),success=(?P<success>\w+),'
    r'periodH=(?P<periodH>\d+),nightOnly=(?P<nightOnly>[01])$'
)

COMM_PARAM_LINE2_RE = re.compile(
    r'test=TEST7,seq=(?P<seq>\d+),'
    r'expSignal=(?P<expSignal>\w+),expSuccess=(?P<expSuccess>\w+),'
    r'expPeriodH=(?P<expPeriodH>\d+),expNightOnly=(?P<expNightOnly>[01]),'
    r'result=(?P<result>PASS|FAIL)(?:,mismatch=(?P<mismatch>.+))?$'
)

# 구버전(한 줄) 포맷과의 호환을 위해 기존 통합 패턴도 남겨둔다.
COMM_PARAM_MSG_RE = re.compile(
    r'test=TEST7,seq=(?P<seq>\d+),case=(?P<case>\w+),'
    r'rssi=(?P<rssi>-?\d+),rsrp=(?P<rsrp>-?\d+),'
    r'attemptIdx=(?P<attemptIdx>\d+),allFailed=(?P<allFailed>[01]),'
    r'signal=(?P<signal>\w+),success=(?P<success>\w+),'
    r'periodH=(?P<periodH>\d+),nightOnly=(?P<nightOnly>[01]),'
    r'expSignal=(?P<expSignal>\w+),expSuccess=(?P<expSuccess>\w+),'
    r'expPeriodH=(?P<expPeriodH>\d+),expNightOnly=(?P<expNightOnly>[01]),'
    r'result=(?P<result>PASS|FAIL)(?:,mismatch=(?P<mismatch>.+))?$'
)
    
COMM_PARAM_SUMMARY_RE = re.compile(
    r'test=TEST7,summary=ALL_COMPLETE,total=(?P<total>\d+),'
    r'pass=(?P<pass>\d+),fail=(?P<fail>\d+)'
)

# 개별 mismatch 항목 하나: "필드명(exp=값,act=값)"
MISMATCH_ITEM_RE = re.compile(
    r'(?P<field>\w+)\(exp=(?P<exp>[^,)]*),act=(?P<act>[^)]*)\)'
)


class CommParamLogError(Exception):
    """파싱/집계 중 사용자에게 알려야 할 오류."""
    pass


def _parse_wall_time(wall_str):
    if '-' in wall_str:
        return datetime.strptime(wall_str, '%Y-%m-%d %H:%M:%S.%f')
    today = datetime.now().strftime('%Y-%m-%d')
    if ':' in wall_str:
        return datetime.strptime(f"{today} {wall_str}", '%Y-%m-%d %H:%M:%S.%f')
    hh, mm, rest = wall_str[0:2], wall_str[2:4], wall_str[4:]
    return datetime.strptime(f"{today} {hh}:{mm}:{rest}", '%Y-%m-%d %H:%M:%S.%f')


def _parse_mismatch_items(mismatch_str):
    """'signal(exp=WEAK,act=STRONG);nightOnly(exp=1,act=0);' -> 필드별 dict 리스트"""
    if not mismatch_str:
        return []
    items = []
    for m in MISMATCH_ITEM_RE.finditer(mismatch_str):
        items.append({
            'field': m.group('field'),
            'exp': m.group('exp'),
            'act': m.group('act'),
        })
    return items

def parse_comm_param_log(path):
    records = []
    summary_line = None
    pending = {}   # seq -> 1줄째에서 수집한 필드 dict (2줄째를 기다리는 중)

    try:
        with open(path, encoding='utf-8', errors='replace') as f:
            for line in f:
                m = LOG_LINE_RE.match(line.rstrip('\n'))
                if not m:
                    continue
                if m.group('tag') != 'TEST7':
                    continue
                msg = m.group('msg')
                wall = _parse_wall_time(m.group('wall'))

                sm = COMM_PARAM_SUMMARY_RE.search(msg)
                if sm:
                    summary_line = {
                        'total': int(sm.group('total')),
                        'pass': int(sm.group('pass')),
                        'fail': int(sm.group('fail')),
                    }
                    continue

                # (1) 구버전 한 줄 포맷: 그대로 완결된 레코드로 처리
                cm = COMM_PARAM_MSG_RE.search(msg)
                if cm:
                    records.append(_build_record(cm, wall))
                    continue

                # (2) 신버전 두 줄 포맷: 1줄째
                m1 = COMM_PARAM_LINE1_RE.search(msg)
                if m1:
                    seq = int(m1.group('seq'))
                    pending[seq] = {
                        'seq': seq, 'ts': wall,
                        'case': m1.group('case'),
                        'rssi': int(m1.group('rssi')),
                        'rsrp': int(m1.group('rsrp')),
                        'attemptIdx': int(m1.group('attemptIdx')),
                        'allFailed': m1.group('allFailed') == '1',
                        'signal': m1.group('signal'),
                        'success': m1.group('success'),
                        'periodH': int(m1.group('periodH')),
                        'nightOnly': m1.group('nightOnly') == '1',
                    }
                    continue

                # (3) 신버전 두 줄 포맷: 2줄째 -> pending과 병합해서 완성
                m2 = COMM_PARAM_LINE2_RE.search(msg)
                if m2:
                    seq = int(m2.group('seq'))
                    base = pending.pop(seq, None)
                    if base is None:
                        # 1줄째를 못 받은 채 2줄째만 들어온 비정상 케이스는 건너뛴다
                        continue
                    mismatch_raw = m2.group('mismatch') or ''
                    base.update({
                        'expSignal': m2.group('expSignal'),
                        'expSuccess': m2.group('expSuccess'),
                        'expPeriodH': int(m2.group('expPeriodH')),
                        'expNightOnly': m2.group('expNightOnly') == '1',
                        'result': m2.group('result'),
                        'mismatch': mismatch_raw,
                        'mismatch_items': _parse_mismatch_items(mismatch_raw),
                    })
                    records.append(base)
                    continue
    except FileNotFoundError:
        raise CommParamLogError(f"DUT 로그 파일을 찾을 수 없습니다: {path}")
    except CommParamLogError:
        raise
    except Exception as e:
        raise CommParamLogError(f"DUT 로그 파싱 오류: {e}")

    records.sort(key=lambda r: r['seq'])
    return records, summary_line


def _build_record(cm, wall):
    """구버전 한 줄 포맷 매치 결과를 레코드 dict로 변환한다."""
    mismatch_raw = cm.group('mismatch') or ''
    return {
        'seq': int(cm.group('seq')),
        'ts': wall,
        'case': cm.group('case'),
        'rssi': int(cm.group('rssi')),
        'rsrp': int(cm.group('rsrp')),
        'attemptIdx': int(cm.group('attemptIdx')),
        'allFailed': cm.group('allFailed') == '1',
        'signal': cm.group('signal'),
        'success': cm.group('success'),
        'periodH': int(cm.group('periodH')),
        'nightOnly': cm.group('nightOnly') == '1',
        'expSignal': cm.group('expSignal'),
        'expSuccess': cm.group('expSuccess'),
        'expPeriodH': int(cm.group('expPeriodH')),
        'expNightOnly': cm.group('expNightOnly') == '1',
        'result': cm.group('result'),
        'mismatch': mismatch_raw,
        'mismatch_items': _parse_mismatch_items(mismatch_raw),
    }


def summarize_comm_param(records, summary_line=None):
    """케이스별/필드별 PASS·FAIL 통계와 전체 판정을 산출한다."""
    total = len(records)
    pass_count = sum(1 for r in records if r['result'] == 'PASS')
    fail_count = total - pass_count

    per_case = {}
    for r in records:
        c = per_case.setdefault(r['case'], {'total': 0, 'pass': 0, 'fail': 0, 'mismatches': []})
        c['total'] += 1
        if r['result'] == 'PASS':
            c['pass'] += 1
        else:
            c['fail'] += 1
            if r['mismatch']:
                c['mismatches'].append(f"seq={r['seq']}: {r['mismatch']}")

    # 필드명(signal/success/periodH/nightOnly)별 실제 불일치 발생 횟수 집계
    mismatch_field_counts = {}
    for r in records:
        for item in r['mismatch_items']:
            f = item['field']
            mismatch_field_counts[f] = mismatch_field_counts.get(f, 0) + 1

    overall_ok = (fail_count == 0) and (total > 0)
    if summary_line is not None:
        overall_ok = overall_ok and (summary_line['fail'] == 0) and (summary_line['total'] == total)

    return {
        'total_runs': total,
        'pass_count': pass_count,
        'fail_count': fail_count,
        'pass_pct': round(pass_count / total * 100, 2) if total else 0.0,
        'per_case': per_case,
        'mismatch_field_counts': mismatch_field_counts,
        'summary_line': summary_line,
        'overall_ok': overall_ok,
    }


def save_comm_param_report_csv(records, stats, path):
    if not records:
        return
    fieldnames = ['seq', 'ts', 'case', 'rssi', 'rsrp', 'attemptIdx', 'allFailed',
                  'signal', 'success', 'periodH', 'nightOnly',
                  'expSignal', 'expSuccess', 'expPeriodH', 'expNightOnly',
                  'result', 'mismatch']

    with open(path, 'w', newline='', encoding='utf-8-sig') as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        for r in records:
            writer.writerow({k: r.get(k, '') for k in fieldnames})

        writer.writerow({})
        writer.writerow({'seq': '요약', 'ts': f"전체 {stats['total_runs']}건",
                          'case': f"PASS {stats['pass_count']}건 ({stats['pass_pct']}%)"})

        writer.writerow({})
        writer.writerow({'seq': '케이스별 결과'})
        for case, c in stats['per_case'].items():
            writer.writerow({
                'seq': case, 'ts': f"{c['pass']}/{c['total']} PASS",
                'case': ('; '.join(c['mismatches']) if c['mismatches'] else ''),
            })

        if stats['mismatch_field_counts']:
            writer.writerow({})
            writer.writerow({'seq': '불일치 필드별 발생 횟수'})
            for field, cnt in sorted(stats['mismatch_field_counts'].items(), key=lambda x: -x[1]):
                writer.writerow({'seq': field, 'ts': cnt})

        writer.writerow({})
        judge = 'PASS' if stats['overall_ok'] else 'FAIL'
        writer.writerow({'seq': '전체 판정', 'ts': judge})

