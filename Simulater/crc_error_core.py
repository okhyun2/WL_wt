# crc_error_core.py
"""
시험 2 - 체크섬(CRC) 오류 검출 성능 전용 시뮬레이터 로직 모듈.
기존 protocol.py / comm_logger.py를 그대로 재사용하며,
print/argparse 없는 순수 함수+클래스로 작성해 GUI/CLI 어느 쪽에서도 쓸 수 있게 한다.

<시험방법 대응>
  나. 시뮬레이터 연결
    시험1) 정상 프로토콜로만 응답             -> MODE_ALL_NORMAL
    시험2) 체크섬(CRC) 오류 프로토콜로만 응답  -> MODE_ALL_ERROR

오류 유형은 체크섬 오류 1종류로 고정한다(단일비트/다중비트/혼합비율 시험 모드는 제공하지 않음).
오류 모드로 전송한 프레임이 정상 프레임과 정확히 어느 바이트에서 달라졌는지
바로 확인할 수 있도록 compare_frames() / describe_diff_index()를 제공한다.
"""

MODE_ALL_NORMAL = "all_normal"   # 정상 프로토콜만 응답 (CRC 정상 확인용)
MODE_ALL_ERROR = "all_error"     # 체크섬 오류 프로토콜만 응답 (CRC 오류 확인용)

ERROR_TYPE_CHECKSUM_ONLY = "checksum_only"
ERROR_TYPE_LABEL_KR = {
    ERROR_TYPE_CHECKSUM_ONLY: "체크섬(CRC) 오류",
}


class CrcErrorConfigError(Exception):
    """설정값이 올바르지 않을 때 사용자에게 알리기 위한 예외."""
    pass


def inject_checksum_error(long_frame: bytes) -> bytes:
    """정상 프레임의 체크섬 바이트 1개만 반전시켜 CRC 오류를 생성한다.
    데이터 본문(계량값 등)은 전혀 건드리지 않고 체크섬만 틀리게 만든다."""
    if len(long_frame) < 2:
        raise CrcErrorConfigError("프레임 길이가 너무 짧습니다.")
    frame = bytearray(long_frame)
    cs_pos = len(frame) - 2
    frame[cs_pos] ^= 0xFF
    return bytes(frame)


def compare_frames(normal_frame: bytes, sent_frame: bytes):
    """정상 프레임과 실제 송신 프레임을 바이트 단위로 비교해
    서로 다른 위치의 (인덱스, 정상값, 송신값) 목록을 반환한다."""
    diffs = []
    length = min(len(normal_frame), len(sent_frame))
    for i in range(length):
        if normal_frame[i] != sent_frame[i]:
            diffs.append({"index": i,
                           "normal_byte": normal_frame[i],
                           "sent_byte": sent_frame[i]})
    if len(normal_frame) != len(sent_frame):
        diffs.append({"index": None, "normal_byte": None, "sent_byte": None,
                       "note": f"길이 다름 (정상 {len(normal_frame)} vs 송신 {len(sent_frame)})"})
    return diffs


def describe_diff_index(frame_len: int, index):
    """달라진 바이트 위치가 프레임에서 어떤 영역인지 사람이 읽기 쉬운 이름으로 설명한다."""
    if index is None:
        return "프레임 길이 불일치"
    if index == frame_len - 2:
        return "체크섬(CRC) 바이트"
    if index == frame_len - 1:
        return "종료 바이트"
    if index < 4:
        return "헤더(시작코드/C필드/주소) 영역"
    return "데이터(계량값 등) 영역"


def format_hex_with_highlight(frame: bytes, highlight_indices) -> str:
    """프레임을 공백구분 2자리 hex 문자열로 만들고,
    highlight_indices에 포함된 바이트는 [ ] 로 감싸 강조한다."""
    highlight = set(i for i in highlight_indices if i is not None)
    parts = []
    for i, b in enumerate(frame):
        hexstr = f"{b:02X}"
        parts.append(f"[{hexstr}]" if i in highlight else hexstr)
    return " ".join(parts)


class CrcErrorScenario:
    """시험2 전용 시나리오. 선택한 모드에 따라 매 요청마다
    정상 프레임과 실제 송신 프레임을 함께 계산해 비교 결과까지 반환한다."""

    def __init__(self, mode: str):
        if mode not in (MODE_ALL_NORMAL, MODE_ALL_ERROR):
            raise CrcErrorConfigError(f"알 수 없는 모드입니다: {mode}")
        self.mode = mode

    @property
    def description(self) -> str:
        if self.mode == MODE_ALL_NORMAL:
            return "시험2-시험1 (정상 프로토콜만 응답 / CRC 정상 확인용)"
        return "시험2-시험2 (체크섬 오류 프로토콜만 응답 / CRC 오류 확인용)"

    def build_response(self, normal_frame: bytes):
        """normal_frame(정상 응답 프레임)을 입력받아
        실제로 송신할 프레임, 모드 레이블, 비교(diff) 결과를 함께 반환한다.
        반환: (sent_frame, mode_label, diffs)
        """
        if self.mode == MODE_ALL_NORMAL:
            sent_frame = normal_frame
            mode_label = "normal"
        else:
            sent_frame = inject_checksum_error(normal_frame)
            mode_label = ERROR_TYPE_CHECKSUM_ONLY

        diffs = compare_frames(normal_frame, sent_frame)
        return sent_frame, mode_label, diffs


class CrcErrorStats:
    """실행 중 누적 통계. GUI에서 매 응답마다 update()를 호출한다."""

    def __init__(self):
        self.total = 0
        self.normal_count = 0
        self.error_count = 0

    def update(self, mode_label: str):
        self.total += 1
        if mode_label == "normal":
            self.normal_count += 1
        else:
            self.error_count += 1

    def error_ratio_pct(self) -> float:
        return round(self.error_count / self.total * 100, 2) if self.total else 0.0

    def as_rows(self):
        return [
            ("총 응답 수", self.total),
            ("정상 응답 수", self.normal_count),
            ("체크섬 오류 응답 수", self.error_count),
            ("오류 비율(%)", self.error_ratio_pct()),
        ]

