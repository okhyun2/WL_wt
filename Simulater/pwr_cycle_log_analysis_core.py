"""
전원 이상 및 복구 신뢰성 시험(TEST12) DUT 로그 분석 순수 로직 모듈.
DUT 출력 포맷:
[2026-09-15 10:00:01.123][NOTI][TEST12] test=TEST12,seq=0,event=BOOT
[2026-09-15 10:00:01.456][NOTI][TEST12] test=TEST12,COUNTER=00123,CHK=OK
[2026-09-15 10:00:01.510][NOTI][TEST12] test=TEST12,halt=1
"""
import re
from datetime import datetime

COUNTER_RE = re.compile(r'test=TEST12,COUNTER=(?P<counter>\d+),CHK=(?P<chk>\w+)')
WALL_RE = re.compile(
    r'^\[(?P<wall>\d{4}-\d{2}-\d{2}[ T]\d{2}:\d{2}:\d{2}\.\d+)\]'
)


class PwrCycleLogError(Exception):
    """파싱/집계 중 사용자에게 알려야 할 오류."""
    pass


def parse_pwr_cycle_log(path, power_on_sec, power_off_sec, gap_tolerance_ratio=0.5):
    """
    path: DUT 로그(txt/log) 경로
    power_on_sec: 전원 ON 유지 시간(초) - 시뮬레이터 UI에서 입력
    power_off_sec: 전원 OFF 유지 시간(초) - 시뮬레이터 UI에서 입력
    gap_tolerance_ratio: 사이클 판정 허용오차(기본 ±50%). 지터/부팅시간 편차 흡수용.

    반환: (records, expected_cycle_sec)
    """
    if power_on_sec <= 0 or power_off_sec < 0:
        raise PwrCycleLogError("Power ON/OFF 시간이 올바르지 않습니다.")

    expected_cycle_sec = power_on_sec + power_off_sec
    raw_entries = []

    try:
        with open(path, encoding='utf-8', errors='replace') as f:
            for line in f:
                wm = WALL_RE.match(line)
                if not wm:
                    continue
                cm = COUNTER_RE.search(line)
                if not cm:
                    continue
                wall = _parse_wall_time(wm.group('wall'))
                raw_entries.append({
                    'ts': wall,
                    'counter': int(cm.group('counter')),
                    'chk': cm.group('chk'),
                })
    except FileNotFoundError:
        raise PwrCycleLogError(f"DUT 로그 파일을 찾을 수 없습니다: {path}")
    except Exception as e:
        raise PwrCycleLogError(f"DUT 로그 파싱 오류: {e}")

    raw_entries.sort(key=lambda r: r['ts'])
    records = _classify(raw_entries, expected_cycle_sec, gap_tolerance_ratio)
    return records, expected_cycle_sec


def _parse_wall_time(wall_str):
    fmt = '%Y-%m-%d %H:%M:%S.%f' if ' ' in wall_str else '%Y-%m-%dT%H:%M:%S.%f'
    return datetime.strptime(wall_str, fmt)


def _classify(entries, expected_cycle_sec, tolerance_ratio):
    results = []
    prev = None
    lower = expected_cycle_sec * (1.0 - tolerance_ratio)
    upper = expected_cycle_sec * (1.0 + tolerance_ratio)

    for idx, cur in enumerate(entries):
        if prev is None:
            results.append({
                **cur, 'cycle_no': idx + 1, 'gap_sec': None,
                'delta_counter': None, 'verdict': 'FIRST',
            })
            prev = cur
            continue

        gap_sec = (cur['ts'] - prev['ts']).total_seconds()
        delta = cur['counter'] - prev['counter']

        if gap_sec > upper:
            missed = max(1, round(gap_sec / expected_cycle_sec) - 1)
            verdict = f'BOOT_FAIL(x{missed})'
        elif cur['chk'] in ('RECOVERED', 'SAVE_FAIL'):
            verdict = 'CORRUPTED'
        elif delta == 1:
            verdict = 'OK'
        elif delta == 0:
            verdict = 'NO_INCREMENT'
        elif delta > 1:
            verdict = 'ABNORMAL_INCREMENT'
        else:
            verdict = 'UNEXPECTED'

        results.append({
            **cur, 'cycle_no': idx + 1,
            'gap_sec': round(gap_sec, 2),
            'delta_counter': delta,
            'verdict': verdict,
        })
        prev = cur

    return results


def summarize_pwr_cycle(records, power_on_sec, power_off_sec):
    body = [r for r in records if r['verdict'] != 'FIRST']
    total = len(body)

    ok = sum(1 for r in body if r['verdict'] == 'OK')
    no_inc = sum(1 for r in body if r['verdict'] == 'NO_INCREMENT')
    abnormal = sum(1 for r in body if r['verdict'] == 'ABNORMAL_INCREMENT')
    corrupted = sum(1 for r in body if r['verdict'] == 'CORRUPTED')
    boot_fail = sum(1 for r in body if r['verdict'].startswith('BOOT_FAIL'))

    overall_ok = (total > 0) and (abnormal == 0) and (corrupted == 0) and (boot_fail == 0)

    return {
        'power_on_sec': power_on_sec,
        'power_off_sec': power_off_sec,
        'expected_cycle_sec': power_on_sec + power_off_sec,
        'total': total,
        'ok': ok,
        'no_increment': no_inc,
        'abnormal_increment': abnormal,
        'corrupted': corrupted,
        'boot_fail': boot_fail,
        'overall_ok': overall_ok,
    }


def save_pwr_cycle_report_csv(records, stats, path):
    import csv
    fieldnames = ['cycle_no', 'ts', 'counter', 'chk', 'gap_sec', 'delta_counter', 'verdict']
    with open(path, 'w', newline='', encoding='utf-8-sig') as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        for r in records:
            writer.writerow({k: r.get(k, '') for k in fieldnames})

        writer.writerow({})
        writer.writerow({'cycle_no': 'Power ON(초)', 'ts': stats['power_on_sec']})
        writer.writerow({'cycle_no': 'Power OFF(초)', 'ts': stats['power_off_sec']})
        writer.writerow({'cycle_no': '전체 사이클', 'ts': stats['total']})
        writer.writerow({'cycle_no': '정상(+1)', 'ts': stats['ok']})
        writer.writerow({'cycle_no': '미증가(허용)', 'ts': stats['no_increment']})
        writer.writerow({'cycle_no': '비정상 증가', 'ts': stats['abnormal_increment']})
        writer.writerow({'cycle_no': '손상', 'ts': stats['corrupted']})
        writer.writerow({'cycle_no': '부팅 실패', 'ts': stats['boot_fail']})
        writer.writerow({'cycle_no': '전체 판정', 'ts': 'PASS' if stats['overall_ok'] else 'FAIL'})

