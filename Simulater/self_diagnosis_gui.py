# self_diagnosis_gui.py
import os
import tkinter as tk
from tkinter import ttk, filedialog, messagebox

from self_diagnosis_core import (
    FAULT_MODULES_ALL, FAULT_LABEL_KR, JUDGE_LABEL_KR,
    parse_selfdiag_log, evaluate_selfdiag_run, summarize_selfdiag,
    save_selfdiag_report_csv, SelfDiagError,
)


class SelfDiagnosisWindow(tk.Toplevel):
    """시험3 - 실제 고장 H/W 시료의 DUT 자가진단 로그를 파싱해 통계만 산출하는 창."""

    def __init__(self, master):
        super().__init__(master)
        self.title("자가진단 및 고장원인 판정 결과 분석")
        self.geometry("980x620")
        self._last_rows = []
        self._last_summary = None

        self.log_path_var = tk.StringVar(value="")
        self.dut_label_var = tk.StringVar(value="")
        self.test_name_var = tk.StringVar(value="TEST3")
        self.fault_vars = {m: tk.BooleanVar(value=False) for m in sorted(FAULT_MODULES_ALL)}
        self.summary_var = tk.StringVar(value="DUT 로그를 선택하고 [분석 실행]을 눌러주세요.")

        self._build_file_frame()
        self._build_fault_frame()
        self._build_result_frame()

        self.transient(master)
        self.lift()
        self.focus_force()

    # ---------- 파일 선택 ----------
    def _build_file_frame(self):
        frame = ttk.LabelFrame(self, text="DUT 자가진단 로그")
        frame.pack(fill="x", padx=8, pady=6)

        row1 = ttk.Frame(frame); row1.pack(fill="x", padx=6, pady=3)
        ttk.Label(row1, text="로그 파일(txt/log)", width=16).pack(side="left")
        ttk.Entry(row1, textvariable=self.log_path_var).pack(side="left", fill="x", expand=True, padx=4)
        ttk.Button(row1, text="찾아보기", command=self._browse_log).pack(side="left", padx=4)

        row2 = ttk.Frame(frame); row2.pack(fill="x", padx=6, pady=3)
        ttk.Label(row2, text="시료 라벨(선택)", width=16).pack(side="left")
        ttk.Entry(row2, textvariable=self.dut_label_var, width=16).pack(side="left", padx=4)
        ttk.Label(row2, text="test 필터", width=10).pack(side="left", padx=(16, 0))
        ttk.Entry(row2, textvariable=self.test_name_var, width=10).pack(side="left", padx=4)

    def _browse_log(self):
        path = filedialog.askopenfilename(
            title="DUT 자가진단 로그 선택",
            filetypes=[("Text/Log", "*.txt;*.log"), ("All files", "*.*")],
            parent=self,
        )
        if path:
            self.log_path_var.set(path)
        self.lift(); self.focus_force()

    # ---------- 결함 체크박스 ----------
    def _build_fault_frame(self):
        frame = ttk.LabelFrame(self, text="이번 시료에 실제로 주입된 결함(체크)")
        frame.pack(fill="x", padx=8, pady=6)

        row = ttk.Frame(frame); row.pack(fill="x", padx=6, pady=4)
        for module in sorted(FAULT_MODULES_ALL):
            ttk.Checkbutton(
                row, text=FAULT_LABEL_KR.get(module, module),
                variable=self.fault_vars[module],
            ).pack(side="left", padx=6)

        btn_row = ttk.Frame(frame); btn_row.pack(fill="x", padx=6, pady=(0, 4))
        ttk.Button(btn_row, text="분석 실행", command=self._on_run).pack(side="left")
        ttk.Button(btn_row, text="결과 CSV 저장", command=self._on_save_report).pack(side="left", padx=6)
        ttk.Label(btn_row, textvariable=self.summary_var, foreground="#333333").pack(side="left", padx=16)

    # ---------- 결과 테이블 ----------
    def _build_result_frame(self):
        frame = ttk.LabelFrame(self, text="회차별 결과 (seq 오름차순)")
        frame.pack(fill="both", expand=True, padx=8, pady=6)

        cols = ("seq", "fault_set", "judge", "missed", "false_positive", "match")
        self.tree = ttk.Treeview(frame, columns=cols, show="headings", height=18)
        widths = {"seq": 50, "fault_set": 220, "judge": 120,
                  "missed": 160, "false_positive": 160, "match": 80}
        headers = {"seq": "SEQ", "fault_set": "검출 결함", "judge": "판정",
                   "missed": "누락(미검출)", "false_positive": "오탐", "match": "완전일치"}
        for c in cols:
            self.tree.heading(c, text=headers[c])
            self.tree.column(c, width=widths[c], anchor="w" if c in ("fault_set", "missed", "false_positive") else "center")

        vsb = ttk.Scrollbar(frame, orient="vertical", command=self.tree.yview)
        self.tree.configure(yscrollcommand=vsb.set)
        self.tree.pack(side="left", fill="both", expand=True)
        vsb.pack(side="left", fill="y")

        self.tree.tag_configure("match_ok", background="#dff5e1")
        self.tree.tag_configure("miss", background="#ff4d4d")
        self.tree.tag_configure("falsepos", background="#ffe9b3")

    # ---------- 실행 ----------
    def _on_run(self):
        log_path = self.log_path_var.get().strip()
        if not log_path or not os.path.isfile(log_path):
            messagebox.showerror("오류", "DUT 로그 파일 경로가 올바르지 않습니다.", parent=self)
            return

        injected_faults = {m for m, v in self.fault_vars.items() if v.get()}

        try:
            records = parse_selfdiag_log(
                log_path,
                test_name_filter=self.test_name_var.get().strip() or None,
                dut_label_filter=self.dut_label_var.get().strip() or None,
            )
        except SelfDiagError as e:
            messagebox.showerror("파싱 오류", str(e), parent=self)
            return

        if not records:
            messagebox.showwarning("결과 없음", "조건에 맞는 [SELFDIAG] 로그 라인을 찾지 못했습니다.", parent=self)
            return

        rows = evaluate_selfdiag_run(records, injected_faults)
        summary = summarize_selfdiag(rows, injected_faults)
        self._render_result(rows, summary)

    def _render_result(self, rows, summary):
        self._last_rows = rows
        self._last_summary = summary
        for item in self.tree.get_children():
            self.tree.delete(item)

        for r in rows:
            tag = "match_ok" if r['exact_match'] else ("miss" if r['missed'] else ("falsepos" if r['false_positive'] else ""))
            self.tree.insert("", "end", values=(
                r['seq'],
                '+'.join(sorted(r['fault_set'])) or 'NONE',
                JUDGE_LABEL_KR.get(r['judge'], r['judge']),
                '+'.join(r['missed']) or '-',
                '+'.join(r['false_positive']) or '-',
                'O' if r['exact_match'] else 'X',
            ), tags=(tag,))

        verdict = "PASS" if summary['overall_ok'] else "FAIL"
        color = "#1a7f37" if summary['overall_ok'] else "#c62828"
        self.summary_var.set(
            f"파싱된 시행 횟수 {summary['total_runs']}건 | 완전일치 {summary['exact_match_count']}건 "
            f"| 정확도 {summary['accuracy_pct']}% | 재현성 {summary['reproducible']} => 판정: {verdict}"
        )
        self.summary_var_color_hint = color  # (라벨 색상은 필요시 별도 Label 위젯에 적용)

    def _on_save_report(self):
        if not self._last_rows:
            messagebox.showinfo("알림", "먼저 분석을 실행해주세요.", parent=self)
            return
        path = filedialog.asksaveasfilename(
            title="자가진단 결과 저장", defaultextension=".csv",
            filetypes=[("CSV", "*.csv")], parent=self,
        )
        if path:
            save_selfdiag_report_csv(self._last_rows, self._last_summary, path)
            messagebox.showinfo("완료", f"결과를 저장했습니다:\n{path}", parent=self)

