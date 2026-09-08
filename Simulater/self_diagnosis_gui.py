# self_diagnosis_gui.py
import os
import tkinter as tk
from tkinter import ttk, filedialog, messagebox

from self_diagnosis_core import (
    FAULT_MODULES_ALL, FAULT_LABEL_KR, JUDGE_LABEL_KR,
    parse_selfdiag_log, summarize_fault_distribution,
    save_selfdiag_report_csv, SelfDiagError,
)


class SelfDiagnosisWindow(tk.Toplevel):
    """시험3 - 실제 고장 H/W 시료의 DUT 자가진단 로그를 파싱해 판정별/모듈별 통계를 산출하는 창."""

    def __init__(self, master, initial_log_path=None):
        super().__init__(master)
        self.title("자가진단 및 고장원인 판정 결과 분석")
        self.geometry("980x760")
        self._last_records = []
        self._last_stats = None

        self.log_path_var = tk.StringVar(value="")
        self.test_name_var = tk.StringVar(value="TEST3")
        self.summary_var = tk.StringVar(value="DUT 로그를 선택하고 [분석 실행]을 눌러주세요.")

        self._build_file_frame()
        self._build_action_frame()
        self._build_result_frame()
        self._build_stats_frame()

        self.transient(master)
        self.lift()
        self.focus_force()

        # 창이 처음 생성되는 시점에도 항상 refresh_with_log_path를 거치도록 통일한다.
        # (초기 결과가 없더라도 _clear_result가 한 번 더 실행되는 것은 무해하다.)
        self.refresh_with_log_path(initial_log_path)

    # ---------- 파일 선택 ----------
    def _build_file_frame(self):
        frame = ttk.LabelFrame(self, text="DUT 자가진단 로그")
        frame.pack(fill="x", padx=8, pady=6)

        row1 = ttk.Frame(frame); row1.pack(fill="x", padx=6, pady=3)
        ttk.Label(row1, text="로그 파일(txt/log)", width=16).pack(side="left")
        ttk.Entry(row1, textvariable=self.log_path_var).pack(side="left", fill="x", expand=True, padx=4)
        ttk.Button(row1, text="찾아보기", command=self._browse_log).pack(side="left", padx=4)

        row2 = ttk.Frame(frame); row2.pack(fill="x", padx=6, pady=3)
        ttk.Label(row2, text="test 필터", width=16).pack(side="left")
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

    # ---------- 실행 버튼 ----------
    def _build_action_frame(self):
        frame = ttk.Frame(self)
        frame.pack(fill="x", padx=8, pady=(0, 6))
        ttk.Button(frame, text="분석 실행", command=self._on_run).pack(side="left")
        ttk.Button(frame, text="결과 CSV 저장", command=self._on_save_report).pack(side="left", padx=6)
        ttk.Label(frame, textvariable=self.summary_var, foreground="#333333").pack(side="left", padx=16)

    # ---------- 회차별 결과 테이블 ----------
    def _build_result_frame(self):
        frame = ttk.LabelFrame(self, text="회차별 결과 (seq 오름차순)")
        frame.pack(fill="both", expand=True, padx=8, pady=6)

        cols = ("seq", "ts", "dut", "fault_set", "judge")
        self.tree = ttk.Treeview(frame, columns=cols, show="headings", height=14)
        widths = {"seq": 50, "ts": 150, "dut": 100, "fault_set": 260, "judge": 140}
        headers = {"seq": "SEQ", "ts": "시각", "dut": "시료", "fault_set": "검출 결함", "judge": "판정"}
        for c in cols:
            self.tree.heading(c, text=headers[c])
            self.tree.column(c, width=widths[c],
                              anchor="w" if c in ("fault_set",) else "center")

        vsb = ttk.Scrollbar(frame, orient="vertical", command=self.tree.yview)
        self.tree.configure(yscrollcommand=vsb.set)
        self.tree.pack(side="left", fill="both", expand=True)
        vsb.pack(side="left", fill="y")

        self.tree.tag_configure("judge_normal", background="#dff5e1")    # 정상 → 초록
        self.tree.tag_configure("judge_meter", background="#ffe9b3")     # 계량기 기기 불량 → 노랑
        self.tree.tag_configure("judge_terminal", background="#ff4d4d")  # 단말기 자체 불량 → 빨강

    # ---------- 판정별/모듈별 통계 테이블 ----------
    def _build_stats_frame(self):
        frame = ttk.Frame(self)
        frame.pack(fill="x", padx=8, pady=(0, 8))

        jframe = ttk.LabelFrame(frame, text="판정별 분포")
        jframe.pack(side="left", fill="both", expand=True, padx=(0, 4))
        self.jtree = ttk.Treeview(jframe, columns=("judge", "count", "pct"),
                                   show="headings", height=3)
        for c, t, w in (("judge", "판정", 140), ("count", "횟수", 70), ("pct", "비율(%)", 80)):
            self.jtree.heading(c, text=t)
            self.jtree.column(c, width=w, anchor="center")
        self.jtree.pack(fill="both", expand=True, padx=4, pady=4)

        mframe = ttk.LabelFrame(frame, text="고장 모듈별 검출 횟수 (동시 검출 시 중복 카운트)")
        mframe.pack(side="left", fill="both", expand=True, padx=(4, 0))
        # 신규: FAULT_MODULES_ALL 개수(10개)만큼 height를 잡아 스크롤 없이 한번에 표시
        self.mtree = ttk.Treeview(mframe, columns=("module", "count", "pct"),
                                   show="headings", height=len(FAULT_MODULES_ALL))
        for c, t, w in (("module", "고장 모듈", 160), ("count", "횟수", 70), ("pct", "비율(%)", 80)):
            self.mtree.heading(c, text=t)
            self.mtree.column(c, width=w, anchor="w" if c == "module" else "center")
        self.mtree.pack(fill="both", expand=True, padx=4, pady=4)

    # ---------- 실행 ----------
    def _on_run(self):
        self._clear_result()   # 수동 클릭이든 자동 호출이든, 실행 시작 시 항상 초기화

        log_path = self.log_path_var.get().strip()
        if not log_path or not os.path.isfile(log_path):
            messagebox.showerror("오류", "DUT 로그 파일 경로가 올바르지 않습니다.", parent=self)
            return

        try:
            records = parse_selfdiag_log(
                log_path,
                test_name_filter=self.test_name_var.get().strip() or None,
            )
        except SelfDiagError as e:
            messagebox.showerror("파싱 오류", str(e), parent=self)
            return

        if not records:
            messagebox.showwarning("결과 없음", "조건에 맞는 [SELFDIAG] 로그 라인을 찾지 못했습니다.", parent=self)
            return

        stats = summarize_fault_distribution(records)
        self._render_result(records, stats)

    def _clear_result(self):
        """화면에 남아있는 이전 실행 결과(테이블/통계/요약문구)를 모두 지운다."""
        self._last_records = []
        self._last_stats = None

        for item in self.tree.get_children():
            self.tree.delete(item)
        for item in self.jtree.get_children():
            self.jtree.delete(item)
        for item in self.mtree.get_children():
            self.mtree.delete(item)

        self.summary_var.set("DUT 로그를 선택하고 [분석 실행]을 눌러주세요.")

    def _render_result(self, records, stats):
        self._last_records = records
        self._last_stats = stats

        judge_tag = {
            "NORMAL": "judge_normal",
            "METER_FAULT": "judge_meter",
            "TERMINAL_FAULT": "judge_terminal",
        }
        for r in records:
            self.tree.insert("", "end", values=(
                r['seq'],
                r['ts'].strftime('%Y-%m-%d %H:%M:%S.%f')[:-3],
                r['dut'],
                '+'.join(sorted(r['fault_set'])) or 'NONE',
                JUDGE_LABEL_KR.get(r['judge'], r['judge']),
            ), tags=(judge_tag.get(r['judge'], ""),))

        for j, cnt in sorted(stats['judge_counts'].items(), key=lambda x: -x[1]):
            self.jtree.insert("", "end", values=(
                JUDGE_LABEL_KR.get(j, j), cnt, stats['judge_pct'][j]
            ))

        for m, cnt in sorted(stats['fault_module_counts'].items(), key=lambda x: -x[1]):
            self.mtree.insert("", "end", values=(
                FAULT_LABEL_KR.get(m, m), cnt, stats['fault_module_pct'][m]
            ))

        self.summary_var.set(
            f"전체 시행 횟수 {stats['total_runs']}건 | 정상(fault=NONE) "
            f"{stats['normal_count']}건 ({stats['normal_pct']}%)"
        )

    def refresh_with_log_path(self, log_path=None):
        """
        창을 새로 열 때나, 이미 열려 있는 창을 재사용할 때나
        동일하게 이 메서드를 거치도록 통일한다.
        1) 이전 결과를 무조건 먼저 지운다.
        2) 유효한 로그 경로가 주어지면 '경로만' 미리 채워 넣는다.
           (자동으로 분석을 실행하지는 않는다 - 반드시 사용자가 [분석 실행]을 눌러야 함)
        """
        self._clear_result()
        if log_path:
            self.log_path_var.set(log_path)
        # 자동 실행 제거: 경로만 채워두고, 실행 여부는 사용자의 명시적 클릭에 맡긴다.

    def _on_save_report(self):
        if not self._last_records:
            messagebox.showinfo("알림", "먼저 분석을 실행해주세요.", parent=self)
            return
        path = filedialog.asksaveasfilename(
            title="자가진단 결과 저장", defaultextension=".csv",
            filetypes=[("CSV", "*.csv")], parent=self,
        )
        if path:
            save_selfdiag_report_csv(self._last_records, self._last_stats, path)
            messagebox.showinfo("완료", f"결과를 저장했습니다:\n{path}", parent=self)

