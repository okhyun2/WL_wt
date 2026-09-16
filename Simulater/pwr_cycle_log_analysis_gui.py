import os
import tkinter as tk
from tkinter import ttk, filedialog, messagebox

from pwr_cycle_log_analysis_core import (
    parse_pwr_cycle_log, summarize_pwr_cycle, save_pwr_cycle_report_csv, PwrCycleLogError,
)


class PwrCycleLogAnalysisWindow(tk.Toplevel):
    """전원 이상 및 복구 신뢰성 시험 DUT 로그를 파싱해
    사이클별 카운터 증가 여부와 손상/부팅실패를 집계하는 창."""

    def __init__(self, master, initial_log_path=None):
        super().__init__(master)
        self.title("전원 이상 및 복구 신뢰성 로그 분석")
        self.geometry("1080x680")
        self._last_records = []
        self._last_stats = None

        self.log_path_var = tk.StringVar(value="")
        self.power_on_var = tk.StringVar(value="10")
        self.power_off_var = tk.StringVar(value="5")
        self.summary_var = tk.StringVar(value="DUT 로그를 선택하고 [분석 실행]을 눌러주세요.")

        self._build_file_frame()
        self._build_power_time_frame()
        self._build_action_frame()
        self._build_result_frame()
        self._build_stats_frame()

        self.transient(master)
        self.lift()
        self.focus_force()

        self.refresh_with_log_path(initial_log_path)

    # ---------- 파일 선택 ----------
    def _build_file_frame(self):
        frame = ttk.LabelFrame(self, text="DUT 전원 이상/복구 로그")
        frame.pack(fill="x", padx=8, pady=6)

        row = ttk.Frame(frame); row.pack(fill="x", padx=6, pady=3)
        ttk.Label(row, text="로그 파일(txt/log)", width=16).pack(side="left")
        ttk.Entry(row, textvariable=self.log_path_var).pack(side="left", fill="x", expand=True, padx=4)
        ttk.Button(row, text="찾아보기", command=self._browse_log).pack(side="left", padx=4)

    def _browse_log(self):
        path = filedialog.askopenfilename(
            title="전원 이상/복구 DUT 로그 선택",
            filetypes=[("Text/Log", "*.txt;*.log"), ("All files", "*.*")],
            parent=self,
        )
        if path:
            self.log_path_var.set(path)
        self.lift(); self.focus_force()

    # ---------- Power ON/OFF 시간 입력 ----------
    def _build_power_time_frame(self):
        frame = ttk.LabelFrame(self, text="전원 사이클 조건 (실제 전원공급장치 설정과 동일하게 입력)")
        frame.pack(fill="x", padx=8, pady=(0, 6))

        row = ttk.Frame(frame); row.pack(fill="x", padx=6, pady=4)

        ttk.Label(row, text="Power ON(초)").pack(side="left")
        ttk.Entry(row, textvariable=self.power_on_var, width=8).pack(side="left", padx=(4, 16))

        ttk.Label(row, text="Power OFF(초)").pack(side="left")
        ttk.Entry(row, textvariable=self.power_off_var, width=8).pack(side="left", padx=(4, 16))

        ttk.Label(row, text="(합계=예상 사이클 주기, 부팅실패 판정 기준으로 사용됩니다)",
                  foreground="#666666").pack(side="left")

    # ---------- 실행 버튼 ----------
    def _build_action_frame(self):
        frame = ttk.Frame(self)
        frame.pack(fill="x", padx=8, pady=(0, 6))
        ttk.Button(frame, text="분석 실행", command=self._on_run).pack(side="left")
        ttk.Button(frame, text="결과 CSV 저장", command=self._on_save_report).pack(side="left", padx=6)
        ttk.Label(frame, textvariable=self.summary_var, foreground="#333333").pack(side="left", padx=16)

    # ---------- 사이클별 결과 테이블 ----------
    def _build_result_frame(self):
        frame = ttk.LabelFrame(self, text="사이클별 실행 결과 (시간순)")
        frame.pack(fill="both", expand=True, padx=8, pady=6)

        cols = ("cycle_no", "ts", "counter", "chk", "gap_sec", "delta_counter", "verdict")
        self.tree = ttk.Treeview(frame, columns=cols, show="headings", height=14)
        widths = {"cycle_no": 60, "ts": 170, "counter": 80, "chk": 90,
                  "gap_sec": 90, "delta_counter": 90, "verdict": 160}
        headers = {"cycle_no": "사이클#", "ts": "시각", "counter": "COUNTER", "chk": "CHK",
                   "gap_sec": "간격(초)", "delta_counter": "증가량", "verdict": "판정"}
        for c in cols:
            self.tree.heading(c, text=headers[c])
            self.tree.column(c, width=widths[c], anchor="center")

        vsb = ttk.Scrollbar(frame, orient="vertical", command=self.tree.yview)
        self.tree.configure(yscrollcommand=vsb.set)
        self.tree.pack(side="left", fill="both", expand=True)
        vsb.pack(side="left", fill="y")

        self.tree.tag_configure("ok", background="#dff5e1")
        self.tree.tag_configure("no_inc", background="#fff3cd")     # 허용된 미증가: 연한 노랑
        self.tree.tag_configure("warn", background="#ffb74d")       # 부팅실패 등 주의: 주황
        self.tree.tag_configure("fail", background="#ff8a8a")       # 손상/비정상 증가: 빨강

    # ---------- 통계 ----------
    def _build_stats_frame(self):
        frame = ttk.LabelFrame(self, text="전체 집계")
        frame.pack(fill="x", padx=8, pady=(0, 8))
        self.stree = ttk.Treeview(frame, columns=("metric", "count"), show="headings", height=6)
        for c, t, w in (("metric", "항목", 160), ("count", "값", 100)):
            self.stree.heading(c, text=t)
            self.stree.column(c, width=w, anchor="center" if c == "count" else "w")
        self.stree.pack(fill="x", padx=4, pady=4)

    # ---------- 실행 ----------
    def _on_run(self):
        self._clear_result()

        log_path = self.log_path_var.get().strip()
        if not log_path or not os.path.isfile(log_path):
            messagebox.showerror("오류", "DUT 로그 파일 경로가 올바르지 않습니다.", parent=self)
            return

        try:
            power_on_sec = float(self.power_on_var.get())
            power_off_sec = float(self.power_off_var.get())
        except ValueError:
            messagebox.showerror("오류", "Power ON/OFF 시간은 숫자로 입력해주세요.", parent=self)
            return

        try:
            records, expected_cycle_sec = parse_pwr_cycle_log(log_path, power_on_sec, power_off_sec)
        except PwrCycleLogError as e:
            messagebox.showerror("파싱 오류", str(e), parent=self)
            return

        if not records:
            messagebox.showwarning("결과 없음", "조건에 맞는 [TEST12] 로그 라인을 찾지 못했습니다.", parent=self)
            return

        stats = summarize_pwr_cycle(records, power_on_sec, power_off_sec)
        self._render_result(records, stats, expected_cycle_sec)

    def _clear_result(self):
        self._last_records = []
        self._last_stats = None
        for item in self.tree.get_children():
            self.tree.delete(item)
        for item in self.stree.get_children():
            self.stree.delete(item)
        self.summary_var.set("DUT 로그를 선택하고 [분석 실행]을 눌러주세요.")

    def _render_result(self, records, stats, expected_cycle_sec):
        self._last_records = records
        self._last_stats = stats

        for r in records:
            verdict = r['verdict']
            if verdict == 'OK' or verdict == 'FIRST':
                tag = "ok"
            elif verdict == 'NO_INCREMENT':
                tag = "no_inc"
            elif verdict.startswith('BOOT_FAIL'):
                tag = "warn"
            else:  # CORRUPTED, ABNORMAL_INCREMENT, UNEXPECTED
                tag = "fail"

            self.tree.insert("", "end", values=(
                r['cycle_no'], r['ts'].strftime('%Y-%m-%d %H:%M:%S.%f')[:-3],
                r['counter'], r['chk'],
                r['gap_sec'] if r['gap_sec'] is not None else "-",
                r['delta_counter'] if r['delta_counter'] is not None else "-",
                verdict,
            ), tags=(tag,))

        rows = [
            ("Power ON(초)", stats['power_on_sec']),
            ("Power OFF(초)", stats['power_off_sec']),
            ("예상 사이클 주기(초)", expected_cycle_sec),
            ("전체 사이클", stats['total']),
            ("정상(+1)", stats['ok']),
            ("미증가(허용)", stats['no_increment']),
            ("비정상 증가", stats['abnormal_increment']),
            ("손상", stats['corrupted']),
            ("부팅 실패", stats['boot_fail']),
        ]
        for metric, val in rows:
            self.stree.insert("", "end", values=(metric, val))

        verdict = "PASS" if stats['overall_ok'] else "FAIL"
        self.summary_var.set(
            f"전체 {stats['total']}사이클 | 정상 {stats['ok']} | 미증가(허용) {stats['no_increment']} "
            f"| 비정상증가 {stats['abnormal_increment']} | 손상 {stats['corrupted']} "
            f"| 부팅실패 {stats['boot_fail']}  =>  판정: {verdict}"
        )

    def refresh_with_log_path(self, log_path=None):
        self._clear_result()
        if log_path:
            self.log_path_var.set(log_path)

    def _on_save_report(self):
        if not self._last_records:
            messagebox.showinfo("알림", "먼저 분석을 실행해주세요.", parent=self)
            return
        path = filedialog.asksaveasfilename(
            title="전원 이상/복구 결과 저장", defaultextension=".csv",
            filetypes=[("CSV", "*.csv")], parent=self,
        )
        if path:
            save_pwr_cycle_report_csv(self._last_records, self._last_stats, path)
            messagebox.showinfo("완료", f"결과를 저장했습니다:\n{path}", parent=self)

