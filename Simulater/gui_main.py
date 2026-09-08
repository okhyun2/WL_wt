import os
import re
import threading
import queue
import time
import random
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
from self_diagnosis_gui import SelfDiagnosisWindow

import serial
import serial.tools.list_ports
import yaml

from protocol import (DeviceProfile, parse_short_frame, build_long_frame,
                       build_meter_userdata)
from scenarios import (NormalOnlyScenario, Test1NormalCollection, Test2ChecksumError,
                        Test3SelfDiagnosisCase, Test11EnduranceCycle)
from comm_logger import CommLogger

from log_compare_gui import CompareWindow

APP_VERSION = "1.0.0"

# ------------------------------------------------------------------
# 시험 항목 정의 (GUI에서 파라미터를 받아 시나리오 인스턴스를 생성)
# ------------------------------------------------------------------
# 배터리 전압 → 5bit 코드(4,3,2,1,0) 변환 (프로토콜 규격 표 기준)
# code 0        : 3.7V 이상
# code 1~30     : (3.7 - 0.1*code) 이상 ~ (3.8 - 0.1*code) 미만
# code 31       : 0.7V 미만
BATTERY_VOLT_MAX_CODE = 31

def battery_volt_to_code(volt: float) -> int:
    """전압(V) -> 5bit 코드. 0.1V 단위 정수 연산으로 부동소수점 오차 방지."""
    tenths = round(volt * 10)          # 3.7V -> 37
    code = 37 - tenths
    return max(0, min(BATTERY_VOLT_MAX_CODE, code))

def battery_code_to_volt_label(code: int) -> str:
    """5bit 코드 -> 대표 전압 문자열 (콤보박스 표시/역매핑용)."""
    if code <= 0:
        return "3.7"
    if code >= BATTERY_VOLT_MAX_CODE:
        return "0.6"                   # "0.7V 미만"의 대표값
    tenths = 37 - code
    return f"{tenths / 10:.1f}"

# 콤보박스에 표시할 선택 가능한 전압 목록: 3.7 ~ 0.6 (0.1V 단위, 0.6은 "0.7V 미만" 대표)
BATTERY_VOLT_CHOICES = [f"{t / 10:.1f}" for t in range(37, 5, -1)]

from test_defs import TEST_META

TEST_DEFS = {
    "0":  {**TEST_META["0"],  "factory": lambda p: NormalOnlyScenario()},
    "1":  {**TEST_META["1"],  "factory": lambda p: Test1NormalCollection()},
    "2":  {**TEST_META["2"],  "factory": lambda p: Test2ChecksumError()},
    "3":  {**TEST_META["3"],  "factory": lambda p: Test3SelfDiagnosisCase(case=p["case"])},
    "11": {**TEST_META["11"], "factory": lambda p: Test11EnduranceCycle()},
}

def inject_error(long_frame: bytes, error_type: str) -> bytes:
    frame = bytearray(long_frame)
    core_start, core_end = 4, len(frame) - 2
    cs_pos = len(frame) - 2
    if error_type == "single_bit":
        pos = random.randrange(core_start, core_end)
        frame[pos] ^= (1 << random.randrange(8))
    elif error_type == "multi_bit":
        positions = random.sample(range(core_start, core_end),
                                   k=min(3, core_end - core_start))
        for pos in positions:
            frame[pos] ^= (1 << random.randrange(8))
    elif error_type == "checksum_only":
        frame[cs_pos] ^= 0xFF
    return bytes(frame)


class SimulatorGUI(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title(f"계량기(서울시 디지털계량기 V1.3) 시뮬레이터 v{APP_VERSION}")
        self.geometry("1180x720")
        self.state_lock = threading.Lock()
        self.live_status = {"battery_code": 0, "q3_over": False,
                     "backflow": False, "indoor_leak": False}

        # ---- 연결 상태 ----
        self.serial_conn: serial.Serial | None = None
        self.listener_thread: threading.Thread | None = None
        self.stop_event = threading.Event()

        # ---- 시나리오 상태 (연결 중 언제든 교체 가능) ----
        self.scenario_lock = threading.Lock()
        self.current_scenario = NormalOnlyScenario()
        self.req_index = 0
        self.meter_value = 0

        self.log_queue = queue.Queue()
        self.comm_logger: CommLogger | None = None
        self.log_save_enabled = tk.BooleanVar(value=False)
        self.log_save_path = tk.StringVar(value="")
        self.dut_log_path_var = tk.StringVar(value="")  # 시험 시작 전 test= 검증용 DUT 로그 파일(선택)
        self.counters = {"total": 0, "normal": 0, "error": 0, "no_resp": 0}

        self._compare_win = None                 # 팝업 중복 방지용 참조

        self._build_menu_bar()
        self._build_device_frame()
        self._build_log_save_frame()
        self._build_connection_frame()
        self._build_test_frame()
        self._build_test_control_frame()
        self._build_log_frame()

        self.protocol("WM_DELETE_WINDOW", self._on_close)
        self.after(100, self._poll_queue)

        self._compare_win = None                       # 비교 팝업 중복 방지용 참조
        self._last_completed_log_path = None            # 마지막으로 저장 완료된 CSV 경로
        self._selfdiag_win = None

    # ----------------------------------------------------------
    # 0) 메뉴바 (도구 -> 로그 비교)
    # ----------------------------------------------------------
    def _build_menu_bar(self):
        menu_bar = tk.Menu(self)
        menu_bar.add_command(label="시뮬레이터 비교", command=self._on_open_log_compare)
        menu_bar.add_command(label="자가진단", command=self._on_open_selfdiag)
        self.config(menu=menu_bar)
    
    def _on_open_selfdiag(self):
        current_log_path = self.dut_log_path_var.get().strip() or None

        if self._selfdiag_win is not None and self._selfdiag_win.winfo_exists():
            self._selfdiag_win.refresh_with_log_path(current_log_path)
            self._selfdiag_win.lift()
            self._selfdiag_win.focus_force()
            return

        self._selfdiag_win = SelfDiagnosisWindow(self, initial_log_path=current_log_path)

    def _on_open_log_compare(self):
        if self.comm_logger is not None:
            default_sim_csv = self.comm_logger.log_path
        elif self._last_completed_log_path:
            default_sim_csv = self._last_completed_log_path
        else:
            default_sim_csv = self.log_save_path.get().strip()
    
        default_test_id = self.test_var.get().split(" | ")[0] if hasattr(self, "test_var") else "1"
    
        if self._compare_win is not None and self._compare_win.winfo_exists():
            self._compare_win.lift()
            self._compare_win.focus_force()
            return
    
        self._compare_win = CompareWindow(
            self, default_sim_csv=default_sim_csv, default_test_id=default_test_id
        )

    # ----------------------------------------------------------
    # 1) 장치 설정 영역
    # ----------------------------------------------------------
    def _build_device_frame(self):
        frame = ttk.LabelFrame(self, text="장치 설정 (실제 계량기 개체 묘사)")
        frame.pack(fill="x", padx=10, pady=6)
    
        self.vars = {
            "port": tk.StringVar(value="COM6"),
            "baudrate": tk.IntVar(value=1200),
            "address": tk.IntVar(value=1),
            "id_str": tk.StringVar(value="09123456"),
            "diameter_mm": tk.IntVar(value=15),
            "initial_meter_value": tk.IntVar(value=12345678),
            "battery_code": tk.IntVar(value=0),
            "q3_over": tk.BooleanVar(value=False),
            "backflow": tk.BooleanVar(value=False),
            "indoor_leak": tk.BooleanVar(value=False),
        }
        self.battery_volt_var = tk.StringVar(value="3.7")   # 화면 표시/선택용
    
        for key in ("battery_code", "q3_over", "backflow", "indoor_leak"):
            self.vars[key].trace_add("write", self._on_status_var_changed)

        self.battery_volt_var.trace_add("write", self._on_battery_volt_changed)
    
        # row1: 포트 / Baudrate / 주소 / 기물번호 / 구경 / 초기 검침값 (요청 순서)
        row1 = ttk.Frame(frame); row1.pack(fill="x", padx=6, pady=3)
    
        ttk.Label(row1, text="포트").pack(side="left")
        self.port_combo = ttk.Combobox(row1, textvariable=self.vars["port"],
                                        width=10, values=self._list_ports())
        self.port_combo.pack(side="left", padx=4)
        ttk.Button(row1, text="포트 검색", command=self._refresh_ports).pack(side="left", padx=4)
    
        ttk.Label(row1, text="Baudrate").pack(side="left", padx=(16, 0))
        ttk.Entry(row1, textvariable=self.vars["baudrate"], width=8).pack(side="left", padx=4)
    
        ttk.Separator(row1, orient="vertical").pack(side="left", fill="y", padx=12)

        ttk.Label(row1, text="주소(A)").pack(side="left", padx=(16, 0))
        ttk.Entry(row1, textvariable=self.vars["address"], width=6).pack(side="left", padx=4)
    
        ttk.Label(row1, text="기물번호").pack(side="left", padx=(16, 0))
        ttk.Entry(row1, textvariable=self.vars["id_str"], width=12).pack(side="left", padx=4)
    
        ttk.Label(row1, text="구경(mm)").pack(side="left", padx=(16, 0))
        ttk.Combobox(row1, textvariable=self.vars["diameter_mm"], width=6,
                     values=[15, 20, 25, 32, 40, 50, 80, 100, 150, 200, 250, 300]
                     ).pack(side="left", padx=4)
    
        ttk.Label(row1, text="초기 검침값").pack(side="left", padx=(16, 0))
        ttk.Entry(row1, textvariable=self.vars["initial_meter_value"], width=12).pack(side="left", padx=4)
    
        # row2: 배터리 전압 / 체크박스 / 설정 저장·불러오기
        row2 = ttk.Frame(frame); row2.pack(fill="x", padx=6, pady=3)
        
        ttk.Label(row2, text="실시간 설정", foreground="gray").pack(side="left")
        ttk.Separator(row2, orient="vertical").pack(side="left", fill="y", padx=10)
        
        ttk.Label(row2, text="배터리 전압").pack(side="left")
        self.battery_combo = ttk.Combobox(
            row2, textvariable=self.battery_volt_var, width=6, state="readonly",
            values=BATTERY_VOLT_CHOICES,
        )
        self.battery_combo.pack(side="left", padx=4)
        ttk.Label(row2, text="V").pack(side="left")
        
        ttk.Checkbutton(row2, text="Q3 초과", variable=self.vars["q3_over"]).pack(side="left", padx=(16, 0))
        ttk.Checkbutton(row2, text="역류", variable=self.vars["backflow"]).pack(side="left", padx=10)
        ttk.Checkbutton(row2, text="옥내누수", variable=self.vars["indoor_leak"]).pack(side="left")
        
        ttk.Button(row2, text="설정 불러오기", command=self._load_config).pack(side="right", padx=4)
        ttk.Button(row2, text="설정 저장", command=self._save_config).pack(side="right", padx=4)

    def _on_status_var_changed(self, *args):
        try:
            new_status = {
                "battery_code": self.vars["battery_code"].get(),
                "q3_over": self.vars["q3_over"].get(),
                "backflow": self.vars["backflow"].get(),
                "indoor_leak": self.vars["indoor_leak"].get(),
            }
        except tk.TclError:
            # 입력창이 비어있는 등 과도기 상태 — 무시하고 다음 변경을 기다림
            return
        with self.state_lock:
            self.live_status = new_status

    def _on_battery_volt_changed(self, *args):
        try:
            volt = float(self.battery_volt_var.get())
        except (tk.TclError, ValueError):
            return
        code = battery_volt_to_code(volt)
        self.vars["battery_code"].set(code)   # 기존 trace가 자동으로 live_status 갱신까지 처리

    def _list_ports(self):
        return [p.device for p in serial.tools.list_ports.comports()]

    def _refresh_ports(self):
        self.port_combo["values"] = self._list_ports()

    def _load_config(self):
        path = filedialog.askopenfilename(filetypes=[("YAML", "*.yaml *.yml")])
        if not path:
            return
        with open(path, "r", encoding="utf-8") as f:
            data = yaml.safe_load(f)
        for key, var in self.vars.items():
            if key in data:
                var.set(data[key])
        if "battery_code" in data:
            self.battery_volt_var.set(battery_code_to_volt_label(int(data["battery_code"])))

    def _save_config(self):
        path = filedialog.asksaveasfilename(defaultextension=".yaml",
                                             filetypes=[("YAML", "*.yaml")])
        if not path:
            return
        data = {k: v.get() for k, v in self.vars.items()}
        with open(path, "w", encoding="utf-8") as f:
            yaml.safe_dump(data, f, allow_unicode=True)

    def _get_device_profile(self) -> DeviceProfile:
        return DeviceProfile(
            port=self.vars["port"].get(),
            baudrate=self.vars["baudrate"].get(),
            address=self.vars["address"].get(),
            id_str=self.vars["id_str"].get(),
            diameter_mm=self.vars["diameter_mm"].get(),
            initial_meter_value=self.vars["initial_meter_value"].get(),
            battery_code=self.vars["battery_code"].get(),
            q3_over=self.vars["q3_over"].get(),
            backflow=self.vars["backflow"].get(),
            indoor_leak=self.vars["indoor_leak"].get(),
        )

    # ----------------------------------------------------------
    # 2) 시험 항목 선택 영역
    # ----------------------------------------------------------
    def _build_test_frame(self):
        frame = ttk.LabelFrame(self, text="시험 항목 선택")
        frame.pack(fill="x", padx=10, pady=6)

        row = ttk.Frame(frame); row.pack(fill="x", padx=6, pady=4)
        ttk.Label(row, text="시험 항목").pack(side="left")

        self.test_var = tk.StringVar(value="0")
        self.test_combo = ttk.Combobox(
            row, textvariable=self.test_var, width=45, state="readonly",
            values=[f"{k} | {v['label']}" for k, v in TEST_DEFS.items()],
        )
        self.test_combo.current(0)
        self.test_combo.pack(side="left", padx=4)
        self.test_combo.bind("<<ComboboxSelected>>", self._on_test_selected)

        ttk.Label(row, text="Case (시험3 전용)").pack(side="left", padx=(20, 0))
        self.case_var = tk.IntVar(value=1)
        self.case_spin = ttk.Spinbox(row, from_=1, to=9, textvariable=self.case_var,
                                      width=5, state="disabled")
        self.case_spin.pack(side="left", padx=4)

        self.test_desc_var = tk.StringVar(value=TEST_DEFS["0"]["desc"])
        ttk.Label(row, textvariable=self.test_desc_var, foreground="gray").pack(side="left", padx=(16, 0))

        # ---- 시험 시작 전 검증용 DUT 로그 파일 (선택) ----
        row2 = ttk.Frame(frame); row2.pack(fill="x", padx=6, pady=(0, 4))
        ttk.Label(row2, text="DUT 로그 파일(선택, test= 검증)", foreground="gray").pack(side="left")
        ttk.Entry(row2, textvariable=self.dut_log_path_var, width=50, state="readonly").pack(side="left", padx=4)
        ttk.Button(row2, text="찾아보기", command=self._browse_dut_log_path).pack(side="left", padx=4)
        ttk.Button(row2, text="지정 해제", command=lambda: self.dut_log_path_var.set("")).pack(side="left", padx=4)

    def _update_test_start_btn_state(self):
        if self.serial_conn is None:
            self.test_start_btn.configure(state="disabled")
            return
        test_id = self.test_var.get().split(" | ")[0]
        self.test_start_btn.configure(state="disabled" if test_id == "0" else "normal")

    def _on_test_selected(self, event=None):
        test_id = self.test_var.get().split(" | ")[0]
        needs_case = TEST_DEFS[test_id]["needs_case"]
        self.case_spin.configure(state="normal" if needs_case else "disabled")
        self.test_desc_var.set(TEST_DEFS[test_id]["desc"])
        self._update_test_start_btn_state()

    # ----------------------------------------------------------
    # 3) 포트 연결 영역 (항상 먼저 수행)
    # ----------------------------------------------------------
    def _build_connection_frame(self):
        frame = ttk.LabelFrame(self, text="포트 연결")
        frame.pack(fill="x", padx=10, pady=6)

        self.connect_btn = ttk.Button(frame, text="연결", command=self._on_connect_toggle)
        self.connect_btn.pack(side="left", padx=6, pady=4)

        self.conn_status_var = tk.StringVar(value="○ 해제됨")
        self.conn_status_label = ttk.Label(frame, textvariable=self.conn_status_var,
                                            foreground="red")
        self.conn_status_label.pack(side="left", padx=10)

    def _build_log_save_frame(self):
        frame = ttk.LabelFrame(self, text="통신 로그 저장 (CSV)")
        frame.pack(fill="x", padx=10, pady=6)
    
        self.log_save_check = ttk.Checkbutton(
            frame, text="연결 시 CSV로 자동 저장", variable=self.log_save_enabled
        )
        self.log_save_check.pack(side="left", padx=6, pady=4)
    
        self.log_path_entry = ttk.Entry(frame, textvariable=self.log_save_path, width=50, state="readonly")
        self.log_path_entry.pack(side="left", padx=4)
    
        self.log_browse_btn = ttk.Button(frame, text="찾아보기", command=self._browse_log_path)
        self.log_browse_btn.pack(side="left", padx=4)

        self.log_size_var = tk.StringVar(value="")
        ttk.Label(frame, textvariable=self.log_size_var, foreground="gray").pack(side="left", padx=(10, 0))
    
    def _browse_log_path(self):
        default_name = f"comm_log_{time.strftime('%Y%m%d_%H%M%S')}.csv"
        path = filedialog.asksaveasfilename(
            defaultextension=".csv", initialfile=default_name,
            filetypes=[("CSV", "*.csv")],
        )
        if path:
            self.log_save_path.set(path)

    def _browse_dut_log_path(self):
        """TeraTerm 등으로 실시간 캡처 중인 DUT 로그 파일을 지정한다.
        지정해두면 시험 시작 시 test= 필드를 읽어 GUI 선택값과 자동으로 대조한다."""
        path = filedialog.askopenfilename(
            filetypes=[("Log/Text", "*.log *.txt"), ("All files", "*.*")]
        )
        if path:
            self.dut_log_path_var.set(path)

    def _extract_last_test_id_from_dut_log(self, path, tail_bytes=8192):
        """DUT 로그 파일 끝부분에서 가장 마지막 test=XXXX 값을 추출.
        파일이 크면 뒤쪽 tail_bytes만 읽어 성능 저하 없이 최근 로그만 확인한다.
        못 찾으면 None을 반환."""
        try:
            size = os.path.getsize(path)
            with open(path, "rb") as f:
                if size > tail_bytes:
                    f.seek(size - tail_bytes)
                data = f.read()
            text = data.decode("utf-8", errors="ignore")
        except OSError:
            return None
    
        matches = re.findall(r"test=(\w+)", text)
        return matches[-1] if matches else None

    def _confirm_dut_test_match(self, test_id, test_def) -> bool:
        """선택한 시험 항목과 DUT 로그에 실제로 찍히는 test= 값이 일치하는지 확인.
        DUT 로그 파일을 지정하지 않았으면 검증 없이 그대로 진행시킨다(True 반환).
        불일치/확인불가 상황에는 사용자에게 계속 진행할지 물어본다."""
        dut_log_path = self.dut_log_path_var.get().strip()
        expected = test_def.get("dut_test_name")
    
        if not dut_log_path:
            return True
    
        if not os.path.isfile(dut_log_path):
            return messagebox.askyesno(
                "DUT 로그 확인 불가",
                f"지정한 DUT 로그 파일을 찾을 수 없습니다:\n{dut_log_path}\n\n"
                "APP_EPC_ACTIVE_TEST_ID 일치 확인 없이 계속 진행하시겠습니까?"
            )
    
        found = self._extract_last_test_id_from_dut_log(dut_log_path)
    
        if found is None:
            return messagebox.askyesno(
                "DUT 로그 확인 불가",
                "DUT 로그에서 시험 항목(test=) 정보를 찾지 못했습니다.\n"
                "(아직 로그가 쌓이지 않았거나 형식이 다를 수 있습니다)\n\n"
                "확인 없이 계속 진행하시겠습니까?"
            )
    
        if expected is None:
            # 시험0(기본/정상 응답만)처럼 실제 DUT 시험 코드와 무관한 항목은 대조하지 않음
            return True
    
        if found != expected:
            return messagebox.askyesno(
                "시험 항목 불일치 경고",
                f"DUT 로그에는 test={found} 로 기록되어 있으나,\n"
                f"시뮬레이터에서 선택한 시험은 '{test_def['label']}' (test={expected}) 입니다.\n\n"
                "이 상태로 진행하면 이후 로그 비교/자가진단 분석에서\n"
                "결과가 전부 'missing_in_dut'로 표시될 수 있습니다.\n"
                "펌웨어의 APP_EPC_ACTIVE_TEST_ID 값을 확인해 주세요.\n\n"
                "그래도 계속 진행하시겠습니까?"
            )
    
        return True

    def _format_file_size(self, size_bytes: int) -> str:
        if size_bytes < 1024:
            return f"{size_bytes} B"
        elif size_bytes < 1024 * 1024:
            return f"{size_bytes / 1024:.1f} KB"
        else:
            return f"{size_bytes / (1024 * 1024):.2f} MB"

    def _on_connect_toggle(self):
        if self.serial_conn is None:
            self._on_connect()
        else:
            self._on_disconnect()

    def _on_connect(self):
        try:
            profile = self._get_device_profile()
        except Exception as e:
            messagebox.showerror("설정 오류", f"장치 설정 값을 확인하세요.\n{e}")
            return

        try:
            self.serial_conn = serial.Serial(
                profile.port, profile.baudrate, bytesize=8,
                parity='N', stopbits=1, timeout=0.05,
            )
            self.device_profile = profile

        except serial.SerialException as e:
            messagebox.showerror("포트 오류", str(e))
            self.serial_conn = None
            return

        self.comm_logger = None
        if self.log_save_enabled.get():
            path = self.log_save_path.get().strip()
            if not path:
                path = f"comm_log_{time.strftime('%Y%m%d_%H%M%S')}.csv"
                self.log_save_path.set(path)
            try:
                self.comm_logger = CommLogger(path, test_id="0")
            except OSError as e:
                messagebox.showerror("로그 파일 오류", f"로그 파일을 열 수 없습니다.\n{e}")
                self.comm_logger = None

        # 연결 중에는 저장 옵션을 잠금
        self.log_save_check.configure(state="disabled")
        self.log_path_entry.configure(state="disabled")
        self.log_browse_btn.configure(state="disabled")

        with self.state_lock:
            self.live_status = {
                "battery_code": profile.battery_code,
                "q3_over": profile.q3_over,
                "backflow": profile.backflow,
                "indoor_leak": profile.indoor_leak,
            }

        self.meter_value = profile.initial_meter_value
        self.req_index = 0
        with self.scenario_lock:
            self.current_scenario = NormalOnlyScenario()

        self.counters = {"total": 0, "normal": 0, "error": 0, "no_resp": 0}
        for item in self.tree.get_children():
            self.tree.delete(item)

        self.stop_event.clear()
        self.listener_thread = threading.Thread(
            target=self._listener_loop, args=(profile,), daemon=True
        )
        self.listener_thread.start()

        self.connect_btn.configure(text="연결 해제")
        self.conn_status_var.set("● 연결됨")
        self.conn_status_label.configure(foreground="green")
        self._update_test_start_btn_state()

    def _on_disconnect(self):
        # 시험이 실행 중이면 먼저 정지 상태(기본 시나리오)로 되돌림
        with self.scenario_lock:
            self.current_scenario = NormalOnlyScenario()

        self.stop_event.set()
        if self.listener_thread:
            self.listener_thread.join(timeout=1.0)
        if self.serial_conn:
            try:
                self.serial_conn.close()
            except Exception:
                pass
        self.serial_conn = None

        had_logger = self.comm_logger is not None
        if self.comm_logger:
            self._last_completed_log_path = self.comm_logger.log_path   # 완료된 경로 기억
            self.comm_logger.close()
            self.comm_logger = None

        self.log_size_var.set("")   # 로그 종료 시 크기 표시 초기화
        
        self.log_save_check.configure(state="normal")
        self.log_path_entry.configure(state="normal")
        self.log_browse_btn.configure(state="normal")

        self.connect_btn.configure(text="연결")
        self.conn_status_var.set("○ 해제됨")
        self.conn_status_label.configure(foreground="red")
        self.test_start_btn.configure(state="disabled")
        self.test_stop_btn.configure(state="disabled")
        self.status_var.set("대기 중 (포트 해제)")

    # ----------------------------------------------------------
    # 4) 시험 실행 영역 (연결된 상태에서만 활성화)
    # ----------------------------------------------------------
    def _build_test_control_frame(self):
        frame = ttk.Frame(self)
        frame.pack(fill="x", padx=10, pady=6)

        self.test_start_btn = ttk.Button(frame, text="시험 시작",
                                          command=self._on_test_start, state="disabled")
        self.test_start_btn.pack(side="left")
        self.test_stop_btn = ttk.Button(frame, text="시험 정지",
                                         command=self._on_test_stop, state="disabled")
        self.test_stop_btn.pack(side="left", padx=6)

        self.status_var = tk.StringVar(value="대기 중 (포트 해제)")
        ttk.Label(frame, textvariable=self.status_var, foreground="blue").pack(side="left", padx=20)

        self.stat_var = tk.StringVar(value="총 0건 | 정상 0 | 오류 0 | 무응답 0 | 성공률 -")
        ttk.Label(frame, textvariable=self.stat_var).pack(side="right")

    def _on_test_start(self):
        test_id = self.test_var.get().split(" | ")[0]
        test_def = TEST_DEFS[test_id]
    
        if not self._confirm_dut_test_match(test_id, test_def):
            return   # 사용자가 '아니오'를 선택하면 시험 시작을 취소
    
        params = {"case": self.case_var.get()} if test_def["needs_case"] else {}
        scenario = test_def["factory"](params)
    
        self.meter_value = self.device_profile.initial_meter_value
        with self.scenario_lock:
            self.current_scenario = scenario
        self.req_index = 0
        self.counters = {"total": 0, "normal": 0, "error": 0, "no_resp": 0}
    
        self.status_var.set(f"시작 중 - {test_def['label']}")
        self.test_start_btn.configure(state="disabled")
        self.test_stop_btn.configure(state="normal")
        self.test_combo.configure(state="disabled")

        self.meter_value = self.device_profile.initial_meter_value   # 값 재동기화

        with self.scenario_lock:
            self.current_scenario = scenario
        self.req_index = 0
        self.counters = {"total": 0, "normal": 0, "error": 0, "no_resp": 0}   # 시험 시작 시 카운터 초기화

        self.status_var.set(f"실행 중 - {test_def['label']}")
        self.test_start_btn.configure(state="disabled")
        self.test_stop_btn.configure(state="normal")
        self.test_combo.configure(state="disabled")

    def _on_test_stop(self):
        with self.scenario_lock:
            self.current_scenario = NormalOnlyScenario()

        self.status_var.set("연결됨 - 기본(정상 응답) 상태")
        self._update_test_start_btn_state()
        self.test_stop_btn.configure(state="disabled")
        self.test_combo.configure(state="readonly")

    def _on_clear_log(self):
        for item in self.tree.get_children():
            self.tree.delete(item)
        self.counters = {"total": 0, "normal": 0, "error": 0, "no_resp": 0}
        self.stat_var.set("총 0건 | 정상 0 | 오류 0 | 무응답 0 | 정상응답률 -")

    def _show_test_complete_dialog(self, test_label: str):
        """시험 지정 횟수 도달 시 완료 안내 팝업을 띄운다."""
        total = self.counters["total"]
        normal = self.counters["normal"]
        error = self.counters["error"]
        no_resp = self.counters["no_resp"]
        rate = (normal / total * 100) if total else 0.0
    
        win = tk.Toplevel(self)
        win.title("시험 완료")
        win.resizable(False, False)
        win.transient(self)
        win.grab_set()
    
        msg = (
            f"{test_label} 이(가) 완료되었습니다.\n\n"
            f"총 요청 수   : {total}회\n"
            f"정상 응답    : {normal}회\n"
            f"오류 응답    : {error}회\n"
            f"무응답       : {no_resp}회\n"
            f"정상 응답율  : {rate:.1f}%\n\n"
            f"시뮬레이터는 기본(정상 응답) 상태로 전환되었습니다."
        )
        ttk.Label(win, text=msg, justify="left", padding=16).pack()
    
        btn_row = ttk.Frame(win)
        btn_row.pack(pady=(0, 12))
        ttk.Button(btn_row, text="확인", command=win.destroy).pack(side="left", padx=6)
        ttk.Button(
            btn_row, text="로그 비교 열기",
            command=lambda: (win.destroy(), self._on_open_log_compare())
        ).pack(side="left", padx=6)
    
        win.update_idletasks()
        x = self.winfo_x() + (self.winfo_width() - win.winfo_width()) // 2
        y = self.winfo_y() + (self.winfo_height() - win.winfo_height()) // 2
        win.geometry(f"+{x}+{y}")
        win.focus_force()

    # ----------------------------------------------------------
    # 5) 통신 로그 표
    # ----------------------------------------------------------
    def _build_log_frame(self):
        frame = ttk.LabelFrame(self, text="DUT 통신 로그")
        frame.pack(fill="both", expand=True, padx=10, pady=6)

        toolbar = ttk.Frame(frame)
        toolbar.pack(fill="x", padx=6, pady=(4, 0))
        ttk.Button(toolbar, text="로그 지우기", command=self._on_clear_log).pack(side="left")

        columns = ("seq", "time", "req_c", "req_a", "req_valid", "mode", "resp_hex", "note")
        self.tree = ttk.Treeview(frame, columns=columns, show="headings", height=18)
        widths = {"seq": 50, "time": 90, "req_c": 50, "req_a": 50,
                "req_valid": 70, "mode": 100, "resp_hex": 320, "note": 320}
        headers = {"seq": "No", "time": "시각", "req_c": "요청C", "req_a": "요청A",
                   "req_valid": "요청유효", "mode": "응답모드", "resp_hex": "응답(Hex)", "note": "비고"}
        for col in columns:
            self.tree.heading(col, text=headers[col])
            self.tree.column(col, width=widths[col], anchor="w")
    
        vsb = ttk.Scrollbar(frame, orient="vertical", command=self.tree.yview)
        self.tree.configure(yscrollcommand=vsb.set)
        self.tree.pack(side="left", fill="both", expand=True)
        vsb.pack(side="right", fill="y")
        
    # ----------------------------------------------------------
    # 리스너 스레드: 연결 중에는 항상 동작, 시나리오만 교체됨
    # ----------------------------------------------------------
    def _listener_loop(self, profile: DeviceProfile):
        ser = self.serial_conn
        buf = bytearray()
        try:
            while not self.stop_event.is_set():
                chunk = ser.read(64)
                if chunk:
                    buf += chunk
                idx = buf.find(b'\x10')
                if idx != -1 and len(buf) - idx >= 5:
                    frame = bytes(buf[idx:idx + 5])
                    buf = buf[idx + 5:]
                    req = parse_short_frame(frame)

                    if req is None:
                        continue

                    if not req['valid']:
                        self._emit(req, "request_checksum_error", None)
                        self._write_log(req, "request_checksum_error", None, "", self._current_test_id())
                        continue
                    if req['c'] != 0x5B or req['a'] != profile.address:
                        self._emit(req, "not_for_me", None)
                        self._write_log(req, "not_for_me", None, "", self._current_test_id())
                        continue
                    
                    self.req_index += 1
                    with self.scenario_lock:
                        scenario = self.current_scenario
                        mode = scenario.decide(self.req_index)
                        current_test_id = scenario.test_id
                        current_phase = scenario.phase(self.req_index)
                        delay = scenario.response_delay(self.req_index)
                    
                    if mode == "no_response":
                        self._emit(req, mode, None, note=f"phase={current_phase}")
                        self._write_log(req, mode, None, f"phase={current_phase}", current_test_id)
                    else:
                        increment = scenario.value_increment(self.req_index)
                        self.meter_value += increment

                        with self.state_lock:
                            status_override = dict(self.live_status)

                        user_data = build_meter_userdata(profile, self.meter_value, status_override=status_override)
                        resp = build_long_frame(c_field=0x08, address=profile.address, user_data=user_data)
                        if mode != "normal":
                            resp = inject_error(resp, mode)
                    
                        if delay > 0:
                            time.sleep(delay)          # 스펙 준수 응답 지연 (1차: 고정, 2차: 지터)
                        ser.write(resp)
                    
                        note = f"phase={current_phase};delay_ms={delay*1000:.1f};meter_value={self.meter_value}"
                        self._emit(req, mode, resp, note=note)
                        self._write_log(req, mode, resp, note, current_test_id)

                        with self.scenario_lock:
                            label = scenario.phase_label(self.req_index)
                        
                        if label:
                            self.log_queue.put({"phase_update": label})
                    
                    with self.scenario_lock:
                        if self.current_scenario.is_complete(self.req_index):
                            self.log_queue.put({"scenario_complete": True})

                if len(buf) > 64:
                    buf = buf[-8:]
        except serial.SerialException as e:
            self.log_queue.put({"error": f"시리얼 포트 오류: {e}"})
        finally:
            self.log_queue.put({"finished": True})

    def _emit(self, req, mode, resp, note=""):
        self.log_queue.put({
            "time": time.strftime("%H:%M:%S"),
            "req_c": hex(req['c']) if req else "",
            "req_a": req['a'] if req else "",
            "req_valid": req['valid'] if req else "",
            "mode": mode,
            "resp_hex": resp.hex() if resp else "",
            "note": note,
        })

    def _current_test_id(self):
        with self.scenario_lock:
            return self.current_scenario.test_id

    def _write_log(self, req, mode, resp, note, test_id):
        if self.comm_logger is None:
            return
        try:
            self.comm_logger.log(req, mode, resp, note=note, test_id_override=test_id)
        except Exception as e:
            self.log_queue.put({"error": f"로그 저장 오류: {e}"})

    # ----------------------------------------------------------
    # 큐 폴링: 워커 스레드 -> GUI 갱신 (100ms 주기)
    # ----------------------------------------------------------
    def _poll_queue(self):
        try:
            while True:
                item = self.log_queue.get_nowait()

                if "finished" in item:
                    # 포트 오류 등으로 리스너가 스스로 종료된 경우 UI 정리
                    if self.serial_conn is not None:
                        self._on_disconnect()
                    continue
                if "error" in item:
                    messagebox.showerror("통신 오류", item["error"])
                    continue
                if "scenario_complete" in item:
                    with self.scenario_lock:
                        completed_test_id = self.current_scenario.test_id
                    test_label = TEST_DEFS.get(completed_test_id, {}).get("label", f"시험 {completed_test_id}")
                
                    self.status_var.set(f"시험 완료 - {test_label} (기본 상태로 전환)")
                    self._on_test_stop()
                    self._show_test_complete_dialog(test_label)
                    continue
                if "phase_update" in item:
                    self.status_var.set(f"실행 중 - {item['phase_update']}")
                    continue

                self.counters["total"] += 1
                if item["mode"] == "normal":
                    self.counters["normal"] += 1
                elif item["mode"] == "no_response":
                    self.counters["no_resp"] += 1
                elif item["mode"] in ("single_bit", "multi_bit", "checksum_only"):
                    self.counters["error"] += 1

                seq = self.counters["total"]
                self.tree.insert("", "end", values=(
                    seq, item["time"], item["req_c"], item["req_a"],
                    item["req_valid"], item["mode"], item["resp_hex"], item["note"],
                ))
                self.tree.yview_moveto(1.0)

                total = self.counters["total"]
                rate = (self.counters["normal"] / total * 100) if total else 0
                self.stat_var.set(
                    f"총 {total}건 | 정상 {self.counters['normal']} | "
                    f"오류 {self.counters['error']} | 무응답 {self.counters['no_resp']} | "
                    f"정상응답률 {rate:.1f}%"
                )
        except queue.Empty:
            pass
        finally:
            # CSV 로깅 중이면 실시간 파일 크기 갱신
            if self.comm_logger is not None:
                try:
                    size = os.path.getsize(self.comm_logger.log_path)
                    self.log_size_var.set(f"현재 크기: {self._format_file_size(size)}")
                except OSError:
                    pass
            else:
                self.log_size_var.set("")
            self.after(100, self._poll_queue)

    def _on_close(self):
        if self.listener_thread and self.listener_thread.is_alive():
            self.stop_event.set()
            self.listener_thread.join(timeout=1.0)
        if self.serial_conn:
            try:
                self.serial_conn.close()
            except Exception:
                pass
        self.destroy()


if __name__ == "__main__":
    app = SimulatorGUI()
    app.mainloop()

