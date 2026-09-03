# comm_logger.py
import csv
import time


class CommLogger:
    """시험 항목과 무관하게 항상 동일한 형식으로 DUT 통신 내역을 기록."""

    def __init__(self, log_path: str, test_id: str):
        self.log_path = log_path
        self._f = open(log_path, "w", newline="", encoding="utf-8")
        self._writer = csv.writer(self._f)
        self._writer.writerow([
            "timestamp", "test_id", "seq", "req_c", "req_a", "req_valid",
            "mode", "resp_hex", "note"
        ])
        self.test_id = test_id
        self.seq = 0

    def log(self, req: dict | None, mode: str, resp: bytes | None,
            note: str = "", test_id_override: str | None = None):
        self.seq += 1
        test_id = test_id_override if test_id_override is not None else self.test_id
        self._writer.writerow([
            time.time(), test_id, self.seq,
            req.get('c') if req else '', req.get('a') if req else '',
            req.get('valid') if req else '',
            mode, resp.hex() if resp else '', note,
        ])
        self._f.flush()

    def close(self):
        self._f.close()

