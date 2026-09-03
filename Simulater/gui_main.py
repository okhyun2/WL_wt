import threading
import queue
import time
import random
import tkinter as tk
from tkinter import ttk, filedialog, messagebox

import serial
import serial.tools.list_ports
import yaml

from protocol import (DeviceProfile, parse_short_frame, build_long_frame,
                       build_meter_userdata)
from scenarios import (NormalOnlyScenario, Test1NormalCollection, Test2ChecksumError,
                        Test3SelfDiagnosisCase, Test11EnduranceCycle)
from comm_logger import CommLogger

APP_VERSION = "1.0.0"

# ------------------------------------------------------------------
# 시험 항목 정의 (GUI에서 파라미터를 받아 시나리오 인스턴스를 생성)
# ------------------------------------------------------------------
TEST_DEFS = {
    "0": {
        "label": "기본(정상 응답만 반복)",
        "factory": lambda p: NormalOnlyScenario(),
        "needs_case": False,
    },
    "1": {
        "label": "시험1 - 검침 데이터 수집 신뢰성",
        "factory": lambda p: Test1NormalCollection(),
        "needs_case": False,
    },
    "2": {
        "label": "시험2 - 체크섬 오류 검출 성능",
        "factory": lambda p: Test2ChecksumError(),
        "needs_case": False,
    },
    "3": {
        "label": "시험3 - 자가진단 (Case 선택)",
        "factory": lambda p: Test3SelfDiagnosisCase(case=p["case"]),
        "needs_case": True,
    },
    "11": {
        "label": "시험11 - 반복동작 내구성",
        "factory": lambda p: Test11EnduranceCycle(),
        "needs_case": False,
    },
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
        self.title(f"계량기 시뮬레이터 v{APP_VERSION}")
        self.geometry("980x720")
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
        self.counters = {"total": 0, "normal": 0, "error": 0, "no_resp": 0}

        self._build_device_frame()
        self._build_test_frame()
        self._build_connection_frame()
        self._build_log_save_frame()
        self._build_test_control_frame()
        self._build_log_frame()

        self.protocol("WM_DELETE_WINDOW", self._on_close)
        self.after(100, self._poll_queue)

    # ----------------------------------------------------------
    # 1) 장치 설정 영역
    # ----------------------------------------------------------
    def _build_device_frame(self):
        frame = ttk.LabelFrame(self, text="장치 설정 (실제 계량기 개체 묘사)")
        frame.pack(fill="x", padx=10, pady=6)

        self.vars = {
            "port": tk.StringVar(value="COM3"),
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

        for key in ("battery_code", "q3_over", "backflow", "indoor_leak"):
            self.vars[key].trace_add("write", self._on_status_var_changed)

        row1 = ttk.Frame(frame); row1.pack(fill="x", padx=6, pady=3)
        ttk.Label(row1, text="포트").pack(side="left")
        self.port_combo = ttk.Combobox(row1, textvariable=self.vars["port"],
                                        width=10, values=self._list_ports())
        self.port_combo.pack(side="left", padx=4)
        ttk.Button(row1, text="포트 검색", command=self._refresh_ports).pack(side="left", padx=4)

        ttk.Label(row1, text="Baudrate").pack(side="left", padx=(16, 0))
        ttk.Entry(row1, textvariable=self.vars["baudrate"], width=8).pack(side="left", padx=4)

        ttk.Label(row1, text="주소(A)").pack(side="left", padx=(16, 0))
        ttk.Entry(row1, textvariable=self.vars["address"], width=6).pack(side="left", padx=4)

        row2 = ttk.Frame(frame); row2.pack(fill="x", padx=6, pady=3)
        ttk.Label(row2, text="기물번호").pack(side="left")
        ttk.Entry(row2, textvariable=self.vars["id_str"], width=12).pack(side="left", padx=4)

        ttk.Label(row2, text="구경(mm)").pack(side="left", padx=(16, 0))
        ttk.Combobox(row2, textvariable=self.vars["diameter_mm"], width=6,
                     values=[15, 20, 25, 32, 40, 50, 80, 100, 150, 200, 250, 300]
                     ).pack(side="left", padx=4)

        ttk.Label(row2, text="초기 검침값").pack(side="left", padx=(16, 0))
        ttk.Entry(row2, textvariable=self.vars["initial_meter_value"], width=12).pack(side="left", padx=4)

        row3 = ttk.Frame(frame); row3.pack(fill="x", padx=6, pady=3)
        ttk.Label(row3, text="배터리코드(0~31)").pack(side="left")
        self.battery_combo = ttk.Combobox(
            row3, textvariable=self.vars["battery_code"], width=6, state="readonly",
            values=list(range(32)),
        )
        self.battery_combo.pack(side="left", padx=4)

        ttk.Checkbutton(row3, text="Q3 초과", variable=self.vars["q3_over"]).pack(side="left", padx=(16, 0))
        ttk.Checkbutton(row3, text="역류", variable=self.vars["backflow"]).pack(side="left", padx=10)
        ttk.Checkbutton(row3, text="옥내누수", variable=self.vars["indoor_leak"]).pack(side="left")

        ttk.Button(row3, text="설정 불러오기", command=self._load_config).pack(side="right", padx=4)
        ttk.Button(row3, text="설정 저장", command=self._save_config).pack(side="right", padx=4)

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

    def _on_test_selected(self, event=None):
        test_id = self.test_var.get().split(" | ")[0]
        needs_case = TEST_DEFS[test_id]["needs_case"]
        self.case_spin.configure(state="normal" if needs_case else "disabled")

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
    
    def _browse_log_path(self):
        default_name = f"comm_log_{time.strftime('%Y%m%d_%H%M%S')}.csv"
        path = filedialog.asksaveasfilename(
            defaultextension=".csv", initialfile=default_name,
            filetypes=[("CSV", "*.csv")],
        )
        if path:
            self.log_save_path.set(path)

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
        self.test_start_btn.configure(state="normal")

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

        if self.comm_logger:
            self.comm_logger.close()
            self.comm_logger = None
        
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
        params = {"case": self.case_var.get()} if test_def["needs_case"] else {}
        scenario = test_def["factory"](params)

        with self.scenario_lock:
            self.current_scenario = scenario
        self.req_index = 0

        self.status_var.set(f"실행 중 - {test_def['label']}")
        self.test_start_btn.configure(state="disabled")
        self.test_stop_btn.configure(state="normal")
        self.test_combo.configure(state="disabled")

    def _on_test_stop(self):
        with self.scenario_lock:
            self.current_scenario = NormalOnlyScenario()

        self.status_var.set("연결됨 - 기본(정상 응답) 상태")
        self.test_start_btn.configure(state="normal")
        self.test_stop_btn.configure(state="disabled")
        self.test_combo.configure(state="readonly")

    # ----------------------------------------------------------
    # 5) 통신 로그 표
    # ----------------------------------------------------------
    def _build_log_frame(self):
        frame = ttk.LabelFrame(self, text="DUT 통신 로그")
        frame.pack(fill="both", expand=True, padx=10, pady=6)

        columns = ("seq", "time", "req_c", "req_a", "req_valid", "mode", "resp_hex", "note")
        self.tree = ttk.Treeview(frame, columns=columns, show="headings", height=18)
        widths = {"seq": 50, "time": 90, "req_c": 50, "req_a": 50,
                  "req_valid": 70, "mode": 100, "resp_hex": 320, "note": 150}
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
                    self.status_var.set("시험 완료 - 1,000회 도달 (기본 상태로 전환)")
                    self._on_test_stop()
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

