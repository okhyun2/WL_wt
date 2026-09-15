# comm_param_log_analysis_gui.py
import os
import tkinter as tk
from tkinter import ttk, filedialog, messagebox

from comm_param_log_analysis_core import (
    parse_comm_param_log, summarize_comm_param, save_comm_param_report_csv, CommParamLogError,
)


class CommParamLogAnalysisWindow(tk.Toplevel):
    """시험7(무선환경 기반 통신 파라미터 자동설정) DUT 로그를 파싱해
    케이스별 PASS/FAIL과 불일치 필드를 집계하는 창."""

    def __init__(self, master, initial_log_path=None):
        super().__init__(master)
        self.title("시험7 - 무선환경 기반 통신 파라미터 자동설정 로그 분석")
        self.geometry("1080x680")
        self._last_records = []
        self._last_stats = None

        self.log_path_var = tk.StringVar(value="")
        self.summary_var = tk.StringVar(value="DUT 로그를 선택하고 [분석 실행]을 눌러주세요.")

        self._build_file_frame()
        self._build_action_frame()
        self._build_result_frame()
        self._build_stats_frame()

        self.transient(master)
        self.lift()
        self.focus_force()

        self.refresh_with_log_path(initial_log_path)

    # ---------- 파일 선택 ----------
    def _build_file_frame(self):
        frame = ttk.LabelFrame(self, text="DUT 통신 파라미터 자동설정 로그")
        frame.pack(fill="x", padx=8, pady=6)

        row = ttk.Frame(frame); row.pack(fill="x", padx=6, pady=3)
        ttk.Label(row, text="로그 파일(txt/log)", width=16).pack(side="left")
        ttk.Entry(row, textvariable=self.log_path_var).pack(side="left", fill="x", expand=True, padx=4)
        ttk.Button(row, text="찾아보기", command=self._browse_log).pack(side="left", padx=4)

    def _browse_log(self):
        path = filedialog.askopenfilename(
            title="통신 파라미터 자동설정 DUT 로그 선택",
            filetypes=[("Text/Log", "*.txt;*.log"), ("All files", "*.*")],
            parent=self,
        )
        if path:
            self.log_path_var.set(path)
        self.lift(); self.focus_force()

    # ---------- 실행 버튼 ----------
    def _build_action_frame(self):
        frame = ttk.Frame(self)
        frame.pack(fill="x", padx=8, pady=(0, 6))
        ttk.Button(frame, text="분석 실행", command=self._on_run).pack(side="left")
        ttk.Button(frame, text="결과 CSV 저장", command=self._on_save_report).pack(side="left", padx=6)
        ttk.Label(frame, textvariable=self.summary_var, foreground="#333333").pack(side="left", padx=16)

    # ---------- 회차별 결과 테이블 ----------
    def _build_result_frame(self):
        frame = ttk.LabelFrame(self, text="케이스별 실행 결과 (seq 오름차순)")
        frame.pack(fill="both", expand=True, padx=8, pady=6)

        cols = ("seq", "case", "rssi", "rsrp", "attemptIdx", "allFailed",
                "signal", "success", "periodH", "nightOnly", "result", "mismatch")
        self.tree = ttk.Treeview(frame, columns=cols, show="headings", height=14)
        widths = {"seq": 45, "case": 110, "rssi": 60, "rsrp": 60, "attemptIdx": 80,
                  "allFailed": 75, "signal": 80, "success": 80, "periodH": 65,
                  "nightOnly": 80, "result": 70, "mismatch": 220}
        headers = {"seq": "SEQ", "case": "케이스", "rssi": "RSSI", "rsrp": "RSRP",
                   "attemptIdx": "attemptIdx", "allFailed": "allFailed",
                   "signal": "신호상태", "success": "성공상태", "periodH": "주기(h)",
                   "nightOnly": "야간전용", "result": "결과", "mismatch": "불일치 내역"}
        for c in cols:
            self.tree.heading(c, text=headers[c])
            self.tree.column(c, width=widths[c],
                              anchor="w" if c == "mismatch" else "center")

        vsb = ttk.Scrollbar(frame, orient="vertical", command=self.tree.yview)
        self.tree.configure(yscrollcommand=vsb.set)
        self.tree.pack(side="left", fill="both", expand=True)
        vsb.pack(side="left", fill="y")

        self.tree.tag_configure("pass", background="#dff5e1")
        self.tree.tag_configure("fail", background="#ff8a8a")

    # ---------- 케이스/필드 통계 ----------
    def _build_stats_frame(self):
        frame = ttk.Frame(self)
        frame.pack(fill="x", padx=8, pady=(0, 8))

        cframe = ttk.LabelFrame(frame, text="케이스별 PASS/FAIL")
        cframe.pack(side="left", fill="both", expand=True, padx=(0, 4))
        self.ctree = ttk.Treeview(cframe, columns=("case", "pass", "fail"),
                                   show="headings", height=6)
        for c, t, w in (("case", "케이스", 130), ("pass", "PASS", 60), ("fail", "FAIL", 60)):
            self.ctree.heading(c, text=t)
            self.ctree.column(c, width=w, anchor="center" if c != "case" else "w")
        self.ctree.pack(fill="both", expand=True, padx=4, pady=4)

        mframe = ttk.LabelFrame(frame, text="불일치 필드별 발생 횟수")
        mframe.pack(side="left", fill="both", expand=True, padx=(4, 0))
        self.mtree = ttk.Treeview(mframe, columns=("field", "count"),
                                   show="headings", height=6)
        for c, t, w in (("field", "필드", 140), ("count", "횟수", 70)):
            self.mtree.heading(c, text=t)
            self.mtree.column(c, width=w, anchor="center" if c == "count" else "w")
        self.mtree.pack(fill="both", expand=True, padx=4, pady=4)

    # ---------- 실행 ----------
    def _on_run(self):
        self._clear_result()

        log_path = self.log_path_var.get().strip()
        if not log_path or not os.path.isfile(log_path):
            messagebox.showerror("오류", "DUT 로그 파일 경로가 올바르지 않습니다.", parent=self)
            return

        try:
            records, summary_line = parse_comm_param_log(log_path)
        except CommParamLogError as e:
            messagebox.showerror("파싱 오류", str(e), parent=self)
            return

        if not records:
            messagebox.showwarning("결과 없음", "조건에 맞는 [TEST7] 로그 라인을 찾지 못했습니다.", parent=self)
            return

        stats = summarize_comm_param(records, summary_line)
        self._render_result(records, stats)

    def _clear_result(self):
        self._last_records = []
        self._last_stats = None
        for item in self.tree.get_children():
            self.tree.delete(item)
        for item in self.ctree.get_children():
            self.ctree.delete(item)
        for item in self.mtree.get_children():
            self.mtree.delete(item)
        self.summary_var.set("DUT 로그를 선택하고 [분석 실행]을 눌러주세요.")

    def _render_result(self, records, stats):
        self._last_records = records
        self._last_stats = stats

        for r in records:
            tag = "pass" if r['result'] == 'PASS' else "fail"
            self.tree.insert("", "end", values=(
                r['seq'], r['case'], r['rssi'], r['rsrp'], r['attemptIdx'], r['allFailed'],
                r['signal'], r['success'], r['periodH'], r['nightOnly'],
                r['result'], r['mismatch'],
            ), tags=(tag,))

        for case, c in stats['per_case'].items():
            self.ctree.insert("", "end", values=(case, c['pass'], c['fail']))

        for field, cnt in sorted(stats['mismatch_field_counts'].items(), key=lambda x: -x[1]):
            self.mtree.insert("", "end", values=(field, cnt))

        verdict = "PASS" if stats['overall_ok'] else "FAIL"
        color = "#1a7f37" if stats['overall_ok'] else "#c62828"
        self.summary_var.set(
            f"전체 {stats['total_runs']}건 | PASS {stats['pass_count']}건 "
            f"({stats['pass_pct']}%) | FAIL {stats['fail_count']}건  =>  판정: {verdict}"
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
            title="통신 파라미터 자동설정 결과 저장", defaultextension=".csv",
            filetypes=[("CSV", "*.csv")], parent=self,
        )
        if path:
            save_comm_param_report_csv(self._last_records, self._last_stats, path)
            messagebox.showinfo("완료", f"결과를 저장했습니다:\n{path}", parent=self)

