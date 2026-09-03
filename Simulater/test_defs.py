# test_defs.py
"""
시험 항목 메타데이터 (시뮬레이터 GUI와 로그 비교 GUI가 공통으로 참조).
시나리오 팩토리(factory)는 gui_main.py 쪽에서만 필요하므로 여기에 두지 않는다.
"""

TEST_META = {
    "0":  {"label": "기본(정상 응답만 반복)",
           "desc": "정상 응답만 반복 (시험 아님)",
           "needs_case": False, "dut_test_name": None},
    "1":  {"label": "시험1 - 검침 데이터 수집 신뢰성",
           "desc": "1차 고정지연 30ms 500회 + 2차 지터지연 20~50ms 500회",
           "needs_case": False, "dut_test_name": "TEST1"},
    "2":  {"label": "시험2 - 체크섬 오류 검출 성능",
           "desc": "정상 500회 + 오류 500회(단일비트/다중비트/체크섬) 균등 배분",
           "needs_case": False, "dut_test_name": "TEST2"},
    "3":  {"label": "시험3 - 자가진단 (Case 선택)",
           "desc": "Case 선택에 따른 자가진단 상황 재현",
           "needs_case": True,  "dut_test_name": "TEST3"},
    "11": {"label": "시험11 - 반복동작 내구성",
           "desc": "연속 정상 응답, 계량값 지속 증가",
           "needs_case": False, "dut_test_name": "TEST11"},
}

