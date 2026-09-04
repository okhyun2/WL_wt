# scenarios.py
import random
from abc import ABC, abstractmethod

# scenarios.py
from test_defs import (
    TEST1_PHASE1_COUNT, TEST1_PHASE2_COUNT,
    TEST1_PHASE1_DELAY_SEC, TEST1_PHASE2_DELAY_RANGE,
    TEST2_DEFAULT_TOTAL_NORMAL, TEST2_DEFAULT_TOTAL_ERROR)


class BaseScenario(ABC):
    test_id: str = ""
    description: str = ""

    @abstractmethod
    def decide(self, request_index: int) -> str:
        raise NotImplementedError

    def phase(self, request_index: int) -> int:
        """구간 구분이 필요 없는 시험은 항상 1을 반환"""
        return 1

    def response_delay(self, request_index: int) -> float:
        """응답 전송 전 대기 시간(초). 기본은 지연 없음"""
        return 0.0

    def value_increment(self, request_index: int) -> int:
        """검침값 증가분. 기본은 기존과 동일한 무작위 0~2"""
        return random.randint(0, 2)

    def is_complete(self, request_index: int) -> bool:
        """지정 횟수 도달 시 True. 기본은 무제한(시험11 등)"""
        return False

    def phase_label(self, request_index: int) -> str:
        """상태 표시줄에 보여줄 구간 설명. 기본은 구간 구분 없음."""
        return ""

class NormalOnlyScenario(BaseScenario):
    """테스트 항목 선택 없이 계속 정상 응답만 반환하는 기본 동작"""
    test_id = "0"
    description = "기본 통신 확인 (정상 응답만 반복)"

    def decide(self, request_index: int) -> str:
        return "normal"

    def value_increment(self, request_index: int) -> int:
        return 0   # 연결 대기 중에는 검침값을 변화시키지 않음

class Test1NormalCollection(BaseScenario):
    test_id = "1"

    PHASE1_COUNT = TEST1_PHASE1_COUNT
    PHASE2_COUNT = TEST1_PHASE2_COUNT
    PHASE1_DELAY_SEC = TEST1_PHASE1_DELAY_SEC
    PHASE2_DELAY_RANGE = TEST1_PHASE2_DELAY_RANGE

    @property
    def description(self):
        lo, hi = self.PHASE2_DELAY_RANGE
        return (f"검침 데이터 수집 신뢰성 "
                f"(1차 고정지연 {self.PHASE1_DELAY_SEC*1000:.0f}ms {self.PHASE1_COUNT}회 + "
                f"2차 지터지연 {lo*1000:.0f}~{hi*1000:.0f}ms {self.PHASE2_COUNT}회)")

    def __init__(self, phase1_count: int = None, phase2_count: int = None):
        if phase1_count is not None:
            self.PHASE1_COUNT = phase1_count
        if phase2_count is not None:
            self.PHASE2_COUNT = phase2_count        

    def decide(self, request_index: int) -> str:
        return "normal"

    def phase(self, request_index: int) -> int:
        return 1 if request_index <= self.PHASE1_COUNT else 2

    def response_delay(self, request_index: int) -> float:
        if self.phase(request_index) == 1:
            return self.PHASE1_DELAY_SEC
        lo, hi = self.PHASE2_DELAY_RANGE
        return random.uniform(lo, hi)

    def value_increment(self, request_index: int) -> int:
        return 1   # 손실·중복·순서오류를 등차수열로 검증하기 위한 결정론적 증가

    def is_complete(self, request_index: int) -> bool:
        return request_index >= (self.PHASE1_COUNT + self.PHASE2_COUNT)

    def phase_label(self, request_index: int) -> str:
        if self.phase(request_index) == 1:
            count_in_phase = request_index
            return f"1차 구간(고정지연 {self.PHASE1_DELAY_SEC*1000:.0f}ms) - {count_in_phase}/{self.PHASE1_COUNT}회"
        else:
            count_in_phase = request_index - self.PHASE1_COUNT
            lo, hi = self.PHASE2_DELAY_RANGE
            return f"2차 구간(지터지연 {lo*1000:.0f}~{hi*1000:.0f}ms) - {count_in_phase}/{self.PHASE2_COUNT}회"

class Test2ChecksumError(BaseScenario):
    test_id = "2"

    DEFAULT_TOTAL_NORMAL = TEST2_DEFAULT_TOTAL_NORMAL
    DEFAULT_TOTAL_ERROR = TEST2_DEFAULT_TOTAL_ERROR

    def __init__(self, total_error: int = None, total_normal: int = None):
        total_error = self.DEFAULT_TOTAL_ERROR if total_error is None else total_error
        total_normal = self.DEFAULT_TOTAL_NORMAL if total_normal is None else total_normal
        self.total_error = total_error
        self.total_normal = total_normal
        pool = ["normal"] * total_normal
        error_types = ["single_bit", "multi_bit", "checksum_only"]
        for i in range(total_error):
            pool.append(error_types[i % len(error_types)])
        random.shuffle(pool)
        self._pool = pool

    def decide(self, request_index: int) -> str:
        return self._pool[(request_index - 1) % len(self._pool)]

    def value_increment(self, request_index: int) -> int:
        return 1  # 오류 프레임이라도 실제 검침값 자체는 결정론적으로 +1 증가

    def is_complete(self, request_index: int) -> bool:
        return request_index >= len(self._pool)

    def phase_label(self, request_index: int) -> str:
        mode = self._pool[(request_index - 1) % len(self._pool)]
        kind = "정상" if mode == "normal" else f"오류주입({mode})"
        return f"{kind} 프레임 - {request_index}/{len(self._pool)}회"

    @property
    def description(self):
        return (f"체크섬 오류 검출 성능 (정상 {self.total_normal}회 + "
                f"오류 {self.total_error}회(단일비트/다중비트/체크섬) 균등 배분)")

class Test3SelfDiagnosisCase(BaseScenario):
    """시험3 Case별 재현. case 번호로 동작을 분기."""
    test_id = "3"

    CASE_MODE = {
        1: "normal",                # 모든 기능 정상
        5: "no_response",           # 계량기 무응답
        6: "checksum_only",         # 체크섬(CRC) 오류
        7: "no_response_retry",     # 재시도 임계값 초과 (연속 무응답)
        8: "intermittent_error",    # 임계값 미만 일시 오류
        9: "normal",                # 단말기+계량기 동시 이상: 계량기 자체는 정상 응답
    }

    def __init__(self, case: int, retry_fail_count: int = 3):
        self.case = case
        self.description = f"자가진단 Case {case} 재현"
        self.retry_fail_count = retry_fail_count
        self._retry_hit = 0
        self._intermittent_toggle = 0

    def decide(self, request_index: int) -> str:
        base_mode = self.CASE_MODE.get(self.case, "normal")

        if base_mode == "no_response_retry":
            self._retry_hit += 1
            return "no_response" if self._retry_hit <= self.retry_fail_count else "normal"

        if base_mode == "intermittent_error":
            self._intermittent_toggle += 1
            return "checksum_only" if self._intermittent_toggle % 5 == 0 else "normal"

        return base_mode


class Test11EnduranceCycle(BaseScenario):
    test_id = "11"
    description = "반복동작 내구성 (연속 정상 응답, 계량값 지속 증가)"

    def decide(self, request_index: int) -> str:
        return "normal"


TEST_CATALOG: dict[str, callable] = {
    "1": lambda: Test1NormalCollection(),
    "2": lambda: Test2ChecksumError(),
    "3-case1": lambda: Test3SelfDiagnosisCase(case=1),
    "3-case5": lambda: Test3SelfDiagnosisCase(case=5),
    "3-case6": lambda: Test3SelfDiagnosisCase(case=6),
    "3-case7": lambda: Test3SelfDiagnosisCase(case=7),
    "3-case8": lambda: Test3SelfDiagnosisCase(case=8),
    "3-case9": lambda: Test3SelfDiagnosisCase(case=9),
    "11": lambda: Test11EnduranceCycle(),
}

