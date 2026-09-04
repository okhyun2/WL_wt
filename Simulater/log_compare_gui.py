# log_compare_gui.py
import os
import queue
import threading
import tkinter as tk
from tkinter import ttk, filedialog, messagebox

from compare_core import run_compare, save_report_csv, CompareError
from test_defs import TEST_META

_COMPARABLE_TESTS = {k: v for k, v in TEST_META.items() if v["dut_test_name"] is not None}


class CompareWindow(tk.Toplevel):
    """
    시뮬레이터 CSV 로그 vs DUT(TeraTerm) 로그 비교 팝업창.
    메인 시뮬레이터(gui_main.py)의 메뉴에서 열리는 것을 전제로 한다.
    """

    def __init__(self, master, default_sim_csv="", default_test_id="1"):
        super().__init__(master)
        self.title("시뮬레이터-DUT 로그 비교")
        self.geometry("1080x640")
        self.result_queue = queue.Queue()
        self._worker = None
        self._last_rows = []

        self.sim_path_var = tk.StringVar(value=default_sim_csv)
        self.dut_path_var = tk.StringVar(value="")
        self.sim_test_id_var = tk.StringVar(value=str(default_test_id))
        self.dut_test_name_var = tk.StringVar(value="TEST1")
        self.seq_offset_var = tk.StringVar(value="0")
        self.exclude_first_n_var = tk.StringVar(value="1")   # ← 부팅 시 최초 검침 1건 기본 제외
        self.interval_var = tk.StringVar(value="10.0")
        self.interval_tol_var = tk.StringVar(value="1.0")
        self.time_tol_var = tk.StringVar(value="3.0")
        self.summary_var = tk.StringVar(value="파일을 선택한 뒤 [비교 실행]을 눌러주세요.")

        self._build_file_frame()
        self._build_param_frame()
        self._build_result_frame()

        self.after(100, self._poll_result_queue)

        # 창을 항상 앞으로, 부모 위에 고정
        self.transient(master)       # 부모(master)에 종속된 보조 창으로 지정
        self.lift()                  # 즉시 앞으로 가져오기
        self.focus_force()           # 키보드 포커스도 강제로 가져오기
        self.attributes("-topmost", True)   # 항상 최상단 고정
    # ---------- UI 구성 ----------
    def _build_file_frame(self):
        frame = ttk.LabelFrame(self, text="로그 파일 선택")
        frame.pack(fill="x", padx=8, pady=6)

        row1 = ttk.Frame(frame); row1.pack(fill="x", padx=6, pady=3)
        ttk.Label(row1, text="시뮬레이터 CSV", width=14).pack(side="left")
        ttk.Entry(row1, textvariable=self.sim_path_var).pack(side="left", fill="x", expand=True, padx=4)
        ttk.Button(row1, text="찾아보기", command=self._browse_sim).pack(side="left", padx=4)

        row2 = ttk.Frame(frame); row2.pack(fill="x", padx=6, pady=3)
        ttk.Label(row2, text="DUT 로그(txt)", width=14).pack(side="left")
        ttk.Entry(row2, textvariable=self.dut_path_var).pack(side="left", fill="x", expand=True, padx=4)
        ttk.Button(row2, text="찾아보기", command=self._browse_dut).pack(side="left", padx=4)

    def _build_param_frame(self):
        frame = ttk.LabelFrame(self, text="비교 파라미터")
        frame.pack(fill="x", padx=8, pady=6)
    
        # 시험 항목 선택 -> sim_test_id_var / dut_test_name_var 자동 채움
        top_row = ttk.Frame(frame); top_row.pack(fill="x", padx=6, pady=(4, 0))
        ttk.Label(top_row, text="시험 항목").pack(side="left")
    
        self.test_select_var = tk.StringVar()
        self.test_select_combo = ttk.Combobox(
            top_row, textvariable=self.test_select_var, width=45, state="readonly",
            values=[f"{k} | {v['label']}" for k, v in _COMPARABLE_TESTS.items()],
        )
        self.test_select_combo.pack(side="left", padx=4)
        self.test_select_combo.bind("<<ComboboxSelected>>", self._on_test_select_changed)
    
        self.test_desc_var = tk.StringVar(value="")
        ttk.Label(top_row, textvariable=self.test_desc_var, foreground="gray").pack(side="left", padx=(12, 0))
    

        row1 = ttk.Frame(frame); row1.pack(fill="x", padx=6, pady=(4, 0))
        row2 = ttk.Frame(frame); row2.pack(fill="x", padx=6, pady=4)
        
        def add_field(parent, label_text, var, width, readonly=False):
            ttk.Label(parent, text=label_text).pack(side="left", padx=(10, 0))
            entry = ttk.Entry(parent, textvariable=var, width=width,
                               state="readonly" if readonly else "normal")
            entry.pack(side="left", padx=4)
            return entry
        
        add_field(row1, "시험ID(sim, 자동)", self.sim_test_id_var, 6, readonly=True)
        add_field(row1, "시험명(DUT, 자동)", self.dut_test_name_var, 10, readonly=True)
        add_field(row1, "seq 오프셋", self.seq_offset_var, 6)
        add_field(row1, "최초 제외 건수", self.exclude_first_n_var, 6)
        
        add_field(row2, "주기(s)", self.interval_var, 6)
        add_field(row2, "주기허용오차(s)", self.interval_tol_var, 6)
        add_field(row2, "시각차허용(s)", self.time_tol_var, 6)
        
        self.run_btn = ttk.Button(row2, text="비교 실행", command=self._on_run)
        self.run_btn.pack(side="left", padx=16)
        self.progress = ttk.Progressbar(row2, mode="indeterminate", length=120)
        self.progress.pack(side="left", padx=4)

        summary_row = ttk.Frame(frame); summary_row.pack(fill="x", padx=6, pady=(0, 4))
        self.summary_label = ttk.Label(summary_row, textvariable=self.summary_var,
                                        anchor="w", foreground="#333333")
        self.summary_label.pack(fill="x")
    
        # 기본값: 생성 시 넘겨받은 default_test_id가 목록에 있으면 자동 선택
        self._preselect_test(self.sim_test_id_var.get())
    
    def _on_test_select_changed(self, event=None):
        test_id = self.test_select_var.get().split(" | ")[0]
        meta = _COMPARABLE_TESTS[test_id]
        self.sim_test_id_var.set(test_id)
        self.dut_test_name_var.set(meta["dut_test_name"])
        self.test_desc_var.set(meta["desc"])
    
    def _preselect_test(self, test_id):
        if test_id not in _COMPARABLE_TESTS:
            test_id = next(iter(_COMPARABLE_TESTS))   # 목록의 첫 시험 항목으로 대체 (빈칸 방지)
        label = f"{test_id} | {_COMPARABLE_TESTS[test_id]['label']}"
        self.test_select_var.set(label)
        self.sim_test_id_var.set(test_id)
        self.dut_test_name_var.set(_COMPARABLE_TESTS[test_id]["dut_test_name"])
        self.test_desc_var.set(_COMPARABLE_TESTS[test_id]["desc"])

    def _build_result_frame(self):
        frame = ttk.LabelFrame(self, text="비교 결과 (seq 단위)")
        frame.pack(fill="both", expand=True, padx=8, pady=6)

        cols = ("seq", "sim_value", "dut_value", "value_match",
                "delta_sec", "interval_sec", "status", "note")
        self.tree = ttk.Treeview(frame, columns=cols, show="headings", height=16)
        widths = {"seq": 50, "sim_value": 90, "dut_value": 90, "value_match": 80,
                  "delta_sec": 80, "interval_sec": 90, "status": 100, "note": 260}
        headers = {"seq": "SEQ", "sim_value": "SIM 검침값", "dut_value": "DUT 검침값",
                   "value_match": "일치", "delta_sec": "송수신차(s)", "interval_sec": "주기(s)",
                   "status": "상태", "note": "비고"}
        for c in cols:
            self.tree.heading(c, text=headers[c])
            self.tree.column(c, width=widths[c], anchor="center" if c != "note" else "w")

        vsb = ttk.Scrollbar(frame, orient="vertical", command=self.tree.yview)
        self.tree.configure(yscrollcommand=vsb.set)
        self.tree.pack(side="left", fill="both", expand=True)
        vsb.pack(side="left", fill="y")

        self.tree.tag_configure("mismatch", background="#ffd6d6")
        self.tree.tag_configure("missing", background="#ffe9b3")
        self.tree.tag_configure("timedelta", background="#fff3b0")
        self.tree.tag_configure("ok", background="#ffffff")

        bottom = ttk.Frame(self); bottom.pack(fill="x", padx=8, pady=(0, 8))
        ttk.Button(bottom, text="리포트 CSV로 저장", command=self._on_save_report).pack(side="right")

    # ---------- 이벤트 핸들러 ----------
    def _browse_sim(self):
        path = filedialog.askopenfilename(title="시뮬레이터 CSV 선택",
                                           filetypes=[("CSV", "*.csv"), ("All files", "*.*")], parent=self)
        self.sim_path_var.set(path)
        self.lift()           # 대화상자 닫힌 뒤 비교창을 다시 앞으로
        if path:
            self.focus_force()

    def _browse_dut(self):
        path = filedialog.askopenfilename(title="DUT 로그 선택",
                                           filetypes=[("Text/Log", "*.txt;*.log"), ("All files", "*.*")], parent=self)
        if path:
            self.dut_path_var.set(path)
            self.lift()
            self.focus_force()

    def _on_run(self):
        sim_path = self.sim_path_var.get().strip()
        dut_path = self.dut_path_var.get().strip()
        if not sim_path or not os.path.isfile(sim_path):
            messagebox.showerror("오류", "시뮬레이터 CSV 경로가 올바르지 않습니다.", parent=self)
            return
        if not dut_path or not os.path.isfile(dut_path):
            messagebox.showerror("오류", "DUT 로그 경로가 올바르지 않습니다.", parent=self)
            return
        try:
            seq_offset = int(self.seq_offset_var.get())
            exclude_first_n = int(self.exclude_first_n_var.get())
            interval = float(self.interval_var.get())
            interval_tol = float(self.interval_tol_var.get())
            time_tol = float(self.time_tol_var.get())
        except ValueError:
            messagebox.showerror("오류", "파라미터 값(오프셋/제외건수/주기/허용오차)은 숫자여야 합니다.", parent=self)
            return

        self.run_btn.configure(state="disabled")
        self.progress.start(12)
        self.summary_var.set("비교 실행 중...")

        def worker():
            try:
                rows, summary = run_compare(
                    sim_path, dut_path,
                    sim_test_id=self.sim_test_id_var.get().strip(),
                    dut_test_name=self.dut_test_name_var.get().strip(),
                    seq_offset=seq_offset, interval=interval,
                    interval_tol=interval_tol, time_tol=time_tol,
                    exclude_first_n=exclude_first_n,
                )
                self.result_queue.put(("ok", rows, summary))
            except CompareError as e:
                self.result_queue.put(("error", str(e), None))
            except Exception as e:
                self.result_queue.put(("error", f"예상치 못한 오류: {e}", None))

        self._worker = threading.Thread(target=worker, daemon=True)
        self._worker.start()

    def _poll_result_queue(self):
        try:
            while True:
                kind, payload, summary = self.result_queue.get_nowait()
                self.progress.stop()
                self.run_btn.configure(state="normal")
                if kind == "error":
                    messagebox.showerror("비교 오류", payload, parent=self)
                    self.summary_var.set("오류로 인해 비교가 중단되었습니다.")
                else:
                    self._render_result(payload, summary)
        except queue.Empty:
            pass
        finally:
            self.after(100, self._poll_result_queue)

    def _render_result(self, rows, summary):
        self._last_rows = rows
        for item in self.tree.get_children():
            self.tree.delete(item)

        for r in rows:
            tag = "ok"
            if r["status"] == "MISMATCH":
                tag = "mismatch"
            elif r["status"] in ("MISSING_SIM", "MISSING_DUT"):
                tag = "missing"
            elif r["status"] == "TIME_DELTA":
                tag = "timedelta"
            self.tree.insert("", "end", values=(
                r["seq"], r["sim_value"], r["dut_value"], r["value_match"],
                r["delta_sec"], r["interval_sec"], r["status"], r["note"],
            ), tags=(tag,))

        parts = [
            f"SIM {summary['sim_count']}건 / DUT {summary['dut_count']}건",
            f"값불일치 {summary['value_mismatches']}",
            f"DUT누락 {summary['missing_in_dut']}",
            f"SIM누락 {summary['missing_in_sim']}",
        ]
        if summary.get('excluded_count'):
            parts.append(
                f"부팅 검침 제외 {summary['excluded_count']}건 "
                f"(sim seq={summary['excluded_sim_seqs']}, dut seq={summary['excluded_dut_seqs']})"
            )
        if summary['alarm_interval_avg'] is not None:
            parts.append(f"AlarmA 평균주기 {summary['alarm_interval_avg']:.2f}s")

        verdict = "PASS" if summary["overall_ok"] else "FAIL"
        color = "#1a7f37" if summary["overall_ok"] else "#c62828"
        self.summary_var.set(" | ".join(parts) + f"  =>  판정: {verdict}")
        self.summary_label.configure(foreground=color)

        if summary["alarm_issues"] or summary["interval_issues"]:
            detail = "\n".join(summary["alarm_issues"] + summary["interval_issues"])
            messagebox.showwarning("주기 이상 감지", detail, parent=self)

    def _on_save_report(self):
        if not self._last_rows:
            messagebox.showinfo("안내", "먼저 비교를 실행해주세요.", parent=self)
            return
        path = filedialog.asksaveasfilename(title="리포트 저장", defaultextension=".csv",
                                             filetypes=[("CSV", "*.csv")], parent=self)
        self.lift()                  # 즉시 앞으로 가져오기
        self.focus_force()           # 키보드 포커스도 강제로 가져오기
        if path:
            save_report_csv(self._last_rows, path)
            messagebox.showinfo("완료", f"리포트를 저장했습니다:\n{path}", parent=self)

