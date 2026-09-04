# test_defs.py
"""
시험 항목 메타데이터 및 파라미터.
scenarios.py가 이 파일의 상수를 가져다 쓰는 방향으로만 참조한다.
(이 파일은 scenarios.py를 절대 import하지 않는다 - 순환 참조 방지)
"""

# ---------------- 시험1 파라미터 ----------------
TEST1_PHASE1_COUNT = 10
TEST1_PHASE2_COUNT = 10
TEST1_PHASE1_DELAY_SEC = 0.030
TEST1_PHASE2_DELAY_RANGE = (0.020, 0.050)

# ---------------- 시험2 파라미터 ----------------
TEST2_DEFAULT_TOTAL_NORMAL = 10
TEST2_DEFAULT_TOTAL_ERROR = 5


def _test1_desc():
    lo, hi = TEST1_PHASE2_DELAY_RANGE
    return (f"1차 고정지연 {TEST1_PHASE1_DELAY_SEC*1000:.0f}ms {TEST1_PHASE1_COUNT}회 + "
            f"2차 지터지연 {lo*1000:.0f}~{hi*1000:.0f}ms {TEST1_PHASE2_COUNT}회")


def _test2_desc():
    return (f"정상 {TEST2_DEFAULT_TOTAL_NORMAL}회 + 오류 {TEST2_DEFAULT_TOTAL_ERROR}회"
            f"(단일비트/다중비트/체크섬) 균등 배분")


TEST_META = {
    "0":  {"label": "기본(정상 응답만 반복)", "desc": "정상 응답만 반복 (시험 아님)",
           "needs_case": False, "dut_test_name": None},
    "1":  {"label": "시험1 - 검침 데이터 수집 신뢰성", "desc": _test1_desc(),
           "needs_case": False, "dut_test_name": "TEST1"},
    "2":  {"label": "시험2 - 체크섬 오류 검출 성능", "desc": _test2_desc(),
           "needs_case": False, "dut_test_name": "TEST2"},
    "3":  {"label": "시험3 - 자가진단 (Case 선택)", "desc": "Case 선택에 따른 자가진단 상황 재현",
           "needs_case": True,  "dut_test_name": "TEST3"},
    "11": {"label": "시험11 - 반복동작 내구성", "desc": "연속 정상 응답, 계량값 지속 증가",
           "needs_case": False, "dut_test_name": "TEST11"},
}

