# crc_error_app.py
"""
시험 2 - 체크섬(CRC) 오류 검출 성능 전용 독립 실행 시뮬레이터.
python crc_error_app.py 로 바로 실행한다.
기존 protocol.py / comm_logger.py를 재사용하므로 Simulater 폴더 안에 두고 실행해야 한다.

오류 유형은 체크섬 오류 1종류로 고정되어 있으며,
실제로 체크섬 오류 프레임이 전송되는 순간에만
정상 데이터와 오류 데이터를 비교해 달라진 부분만 화면에 표시한다.
"""
import threading
import queue
import tkinter as tk
from tkinter import ttk, filedialog, messagebox

import serial
import serial.tools.list_ports

from protocol import DeviceProfile, parse_short_frame, build_long_frame, build_meter_userdata
from comm_logger import CommLogger
from crc_error_core import (
    CrcErrorScenario, CrcErrorStats, CrcErrorConfigError,
    MODE_ALL_NORMAL, MODE_ALL_ERROR,
    describe_diff_index,
)

APP_TITLE = "체크섬(CRC) 오류 검출 시뮬레이터 (시험2 전용)"


class CrcErrorApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title(APP_TITLE)
        self.geometry("900x700")

        self._serial_thread = None
        self._stop_flag = threading.Event()
        self._ui_queue = queue.Queue()
        self._stats = CrcErrorStats()
        self._logger = None
        self._profile = None

        self.device_config_var = tk.StringVar(value="device_profile.yaml")
        self.port_var = tk.StringVar(value="")
        self.mode_var = tk.StringVar(value=MODE_ALL_NORMAL)
        self.log_path_var = tk.StringVar(value="crc_test_log.csv")
        self.status_var = tk.StringVar(value="대기 중")

        self._build_device_frame()
        self._build_mode_frame()
        self._build_action_frame()
        self._build_compare_frame()
        self._build_result_frame()

        self._refresh_ports()
        self.after(150, self._drain_ui_queue)
        self.protocol("WM_DELETE_WINDOW", self._on_close)

    # ---------- 장치/포트 설정 ----------
    def _build_device_frame(self):
        frame = ttk.LabelFrame(self, text="DUT 연결 설정")
        frame.pack(fill="x", padx=8, pady=6)

        row1 = ttk.Frame(frame); row1.pack(fill="x", padx=6, pady=3)
        ttk.Label(row1, text="device_profile.yaml", width=18).pack(side="left")
        ttk.Entry(row1, textvariable=self.device_config_var).pack(side="left", fill="x", expand=True, padx=4)
        ttk.Button(row1, text="찾아보기", command=self._browse_device_config).pack(side="left", padx=4)

        row2 = ttk.Frame(frame); row2.pack(fill="x", padx=6, pady=3)
        ttk.Label(row2, text="시리얼 포트", width=18).pack(side="left")
        self.port_combo = ttk.Combobox(row2, textvariable=self.port_var, width=20, state="readonly")
        self.port_combo.pack(side="left", padx=4)
        ttk.Button(row2, text="새로고침", command=self._refresh_ports).pack(side="left", padx=4)

        row3 = ttk.Frame(frame); row3.pack(fill="x", padx=6, pady=3)
        ttk.Label(row3, text="통신 로그 저장 경로", width=18).pack(side="left")
        ttk.Entry(row3, textvariable=self.log_path_var).pack(side="left", fill="x", expand=True, padx=4)
        ttk.Button(row3, text="찾아보기", command=self._browse_log_path).pack(side="left", padx=4)

    def _browse_device_config(self):
        path = filedialog.askopenfilename(
            title="device_profile.yaml 선택",
            filetypes=[("YAML", "*.yaml;*.yml"), ("All files", "*.*")], parent=self)
        if path:
            self.device_config_var.set(path)

    def _browse_log_path(self):
        path = filedialog.asksaveasfilename(
            title="통신 로그 저장 경로", defaultextension=".csv",
            filetypes=[("CSV", "*.csv")], parent=self)
        if path:
            self.log_path_var.set(path)

    def _refresh_ports(self):
        ports = [p.device for p in serial.tools.list_ports.comports()]
        self.port_combo["values"] = ports
        if ports and not self.port_var.get():
            self.port_var.set(ports[0])

    # ---------- 시험 모드 설정 (정상 / 체크섬 오류, 둘 중 하나) ----------
    def _build_mode_frame(self):
        frame = ttk.LabelFrame(self, text="시험2 모드 선택 (오류 유형은 체크섬 오류 1종류로 고정)")
        frame.pack(fill="x", padx=8, pady=6)

        ttk.Radiobutton(frame, text="시험2-시험1 : 정상 프로토콜만 응답 (CRC 정상 확인용)",
                         variable=self.mode_var, value=MODE_ALL_NORMAL).pack(anchor="w", padx=6, pady=2)
        ttk.Radiobutton(frame, text="시험2-시험2 : 체크섬 오류 프로토콜만 응답 (CRC 오류 확인용)",
                         variable=self.mode_var, value=MODE_ALL_ERROR).pack(anchor="w", padx=6, pady=2)

    # ---------- 실행 버튼 ----------
    def _build_action_frame(self):
        frame = ttk.Frame(self)
        frame.pack(fill="x", padx=8, pady=(0, 6))
        self.start_btn = ttk.Button(frame, text="시작", command=self._on_start)
        self.start_btn.pack(side="left")
        self.stop_btn = ttk.Button(frame, text="정지", command=self._on_stop, state="disabled")
        self.stop_btn.pack(side="left", padx=6)
        ttk.Label(frame, textvariable=self.status_var, foreground="#333333").pack(side="left", padx=16)

    # ---------- 오류 전송 시 비교 패널 (오류 부분만 표시) ----------
    def _build_compare_frame(self):
        frame = ttk.LabelFrame(self, text="체크섬 오류 전송 시 비교 (오류 부분만 표시)")
        frame.pack(fill="x", padx=8, pady=6)

        self.compare_idle_var = tk.StringVar(
            value="아직 전송된 체크섬 오류 데이터가 없습니다. (오류가 전송되면 여기에 표시됩니다)")
        self.compare_idle_label = ttk.Label(frame, textvariable=self.compare_idle_var,
                                             foreground="gray")
        self.compare_idle_label.pack(anchor="w", padx=6, pady=4)

        self.compare_detail_frame = ttk.Frame(frame)
        # 오류 발생 시에만 pack되며, 평소에는 보이지 않는다.

        row1 = ttk.Frame(self.compare_detail_frame); row1.pack(fill="x", padx=6, pady=2)
        ttk.Label(row1, text="오류 위치(바이트 인덱스)", width=20).pack(side="left")
        self.diff_index_var = tk.StringVar(value="-")
        ttk.Label(row1, textvariable=self.diff_index_var, font=("Consolas", 11, "bold")).pack(side="left")

        row2 = ttk.Frame(self.compare_detail_frame); row2.pack(fill="x", padx=6, pady=2)
        ttk.Label(row2, text="해당 영역", width=20).pack(side="left")
        self.diff_area_var = tk.StringVar(value="-")
        ttk.Label(row2, textvariable=self.diff_area_var).pack(side="left")

        row3 = ttk.Frame(self.compare_detail_frame); row3.pack(fill="x", padx=6, pady=2)
        ttk.Label(row3, text="정상 데이터 값", width=20).pack(side="left")
        self.diff_normal_var = tk.StringVar(value="-")
        ttk.Label(row3, textvariable=self.diff_normal_var,
                  foreground="#1a7f37", font=("Consolas", 12, "bold")).pack(side="left")

        row4 = ttk.Frame(self.compare_detail_frame); row4.pack(fill="x", padx=6, pady=2)
        ttk.Label(row4, text="오류 데이터 값", width=20).pack(side="left")
        self.diff_error_var = tk.StringVar(value="-")
        ttk.Label(row4, textvariable=self.diff_error_var,
                  foreground="#c62828", font=("Consolas", 12, "bold")).pack(side="left")

    def _show_compare_idle(self, message=None):
        self.compare_detail_frame.pack_forget()
        if message:
            self.compare_idle_var.set(message)
        self.compare_idle_label.pack(anchor="w", padx=6, pady=4)

    def _show_compare_detail(self, diff_index, diff_area, normal_byte, error_byte):
        self.compare_idle_label.pack_forget()
        self.diff_index_var.set(f"[{diff_index}]" if diff_index is not None else "-")
        self.diff_area_var.set(diff_area)
        self.diff_normal_var.set(f"0x{normal_byte:02X}" if normal_byte is not None else "-")
        self.diff_error_var.set(f"0x{error_byte:02X}" if error_byte is not None else "-")
        self.compare_detail_frame.pack(fill="x", padx=6, pady=(0, 6))

    # ---------- 결과 표시 ----------
    def _build_result_frame(self):
        frame = ttk.LabelFrame(self, text="실시간 응답 로그 (최근 200건)")
        frame.pack(fill="both", expand=True, padx=8, pady=6)

        cols = ("seq", "mode", "meter_value", "diff_byte", "diff_area")
        self.tree = ttk.Treeview(frame, columns=cols, show="headings", height=12)
        headers = {"seq": "SEQ", "mode": "응답모드", "meter_value": "검침값",
                   "diff_byte": "달라진 바이트 (정상→오류)", "diff_area": "영역"}
        widths = {"seq": 60, "mode": 140, "meter_value": 110, "diff_byte": 220, "diff_area": 180}
        for c in cols:
            self.tree.heading(c, text=headers[c])
            self.tree.column(c, width=widths[c], anchor="center" if c != "diff_byte" else "w")
        vsb = ttk.Scrollbar(frame, orient="vertical", command=self.tree.yview)
        self.tree.configure(yscrollcommand=vsb.set)
        self.tree.pack(side="left", fill="both", expand=True)
        vsb.pack(side="left", fill="y")
        self.tree.tag_configure("normal", background="#dff5e1")
        self.tree.tag_configure("error", background="#ffe0b2")

        stats_frame = ttk.LabelFrame(self, text="누적 통계")
        stats_frame.pack(fill="x", padx=8, pady=(0, 8))
        self.stats_tree = ttk.Treeview(stats_frame, columns=("item", "value"),
                                        show="headings", height=5)
        self.stats_tree.heading("item", text="항목")
        self.stats_tree.heading("value", text="값")
        self.stats_tree.column("item", width=220, anchor="w")
        self.stats_tree.column("value", width=120, anchor="center")
        self.stats_tree.pack(fill="x", padx=6, pady=6)

    # ---------- 시작/정지 ----------
    def _on_start(self):
        try:
            self._profile = DeviceProfile.from_yaml(self.device_config_var.get())
        except Exception as e:
            messagebox.showerror("오류", f"device_profile.yaml 로드 실패:\n{e}", parent=self)
            return

        port = self.port_var.get()
        if not port:
            messagebox.showerror("오류", "시리얼 포트를 선택해주세요.", parent=self)
            return

        try:
            scenario = CrcErrorScenario(mode=self.mode_var.get())
        except CrcErrorConfigError as e:
            messagebox.showerror("설정 오류", str(e), parent=self)
            return

        for item in self.tree.get_children():
            self.tree.delete(item)
        self._stats = CrcErrorStats()
        self._render_stats()
        self._show_compare_idle("아직 전송된 체크섬 오류 데이터가 없습니다. (오류가 전송되면 여기에 표시됩니다)")

        self._logger = CommLogger(self.log_path_var.get(), test_id="2")
        self._stop_flag.clear()
        self.status_var.set(f"실행 중: {scenario.description}")
        self.start_btn.configure(state="disabled")
        self.stop_btn.configure(state="normal")

        self._serial_thread = threading.Thread(
            target=self._serial_loop, args=(port, self._profile, scenario), daemon=True)
        self._serial_thread.start()

    def _on_stop(self):
        self._stop_flag.set()

    def _on_close(self):
        self._stop_flag.set()
        if self._serial_thread is not None:
            self._serial_thread.join(timeout=1.0)
        self.destroy()

    # ---------- 시리얼 루프 (백그라운드 스레드) ----------
    def _serial_loop(self, port, profile, scenario):
        meter_value = profile.initial_meter_value
        req_index = 0
        try:
            with serial.Serial(port, profile.baudrate, bytesize=8, parity='N',
                                stopbits=1, timeout=0.05) as ser:
                buf = bytearray()
                while not self._stop_flag.is_set():
                    chunk = ser.read(64)
                    if chunk:
                        buf += chunk
                    idx = buf.find(b'\x10')
                    if idx == -1 or len(buf) - idx < 5:
                        continue
                    frame = bytes(buf[idx:idx + 5])
                    buf = buf[idx + 5:]
                    req = parse_short_frame(frame)
                    if req is None or not req['valid']:
                        continue
                    if req['c'] != 0x5B or req['a'] != profile.address:
                        continue

                    req_index += 1
                    meter_value += 1
                    user_data = build_meter_userdata(profile, meter_value)
                    normal_frame = build_long_frame(c_field=0x08, address=profile.address, user_data=user_data)
                    sent_frame, mode_label, diffs = scenario.build_response(normal_frame)

                    ser.write(sent_frame)
                    self._logger.log(req, mode_label, sent_frame, note=f"meter_value={meter_value}")

                    self._ui_queue.put(("row", req_index, mode_label, meter_value,
                                         normal_frame, sent_frame, diffs))

                    if len(buf) > 64:
                        buf = buf[-8:]
        except Exception as e:
            self._ui_queue.put(("error", str(e), None, None, None, None, None))
        finally:
            if self._logger:
                self._logger.close()
            self._ui_queue.put(("stopped", None, None, None, None, None, None))

    # ---------- UI 큐 처리 (메인 스레드) ----------
    def _drain_ui_queue(self):
        try:
            while True:
                kind, a, b, c, d, e, f = self._ui_queue.get_nowait()
                if kind == "row":
                    seq, mode_label, meter_value, normal_frame, sent_frame, diffs = a, b, c, d, e, f
                    self._stats.update(mode_label)
                    tag = "normal" if mode_label == "normal" else "error"

                    if diffs:
                        d0 = diffs[0]
                        if d0.get("index") is not None:
                            diff_byte_text = f"[{d0['index']}] 0x{d0['normal_byte']:02X} -> 0x{d0['sent_byte']:02X}"
                            diff_area_text = describe_diff_index(len(sent_frame), d0["index"])
                        else:
                            diff_byte_text = d0.get("note", "")
                            diff_area_text = "-"
                    else:
                        diff_byte_text = "(차이 없음)"
                        diff_area_text = "-"

                    self.tree.insert("", "end", values=(seq, mode_label, meter_value,
                                                          diff_byte_text, diff_area_text), tags=(tag,))
                    children = self.tree.get_children()
                    if len(children) > 200:
                        self.tree.delete(children[0])
                    self.tree.yview_moveto(1.0)
                    self._render_stats()

                    # 체크섬 오류가 실제로 전송된 경우에만 비교 패널 갱신
                    if mode_label != "normal" and diffs:
                        d0 = diffs[0]
                        if d0.get("index") is not None:
                            area = describe_diff_index(len(sent_frame), d0["index"])
                            self._show_compare_detail(
                                d0["index"], area, d0["normal_byte"], d0["sent_byte"])
                        else:
                            self._show_compare_idle(d0.get("note", "오류 상세를 확인할 수 없습니다."))
                elif kind == "error":
                    messagebox.showerror("시리얼 오류", a, parent=self)
                    self._stop_running_ui()
                elif kind == "stopped":
                    self._stop_running_ui()
        except queue.Empty:
            pass
        self.after(150, self._drain_ui_queue)

    def _stop_running_ui(self):
        self.start_btn.configure(state="normal")
        self.stop_btn.configure(state="disabled")
        if "중" in self.status_var.get():
            self.status_var.set("정지됨")

    def _render_stats(self):
        for item in self.stats_tree.get_children():
            self.stats_tree.delete(item)
        for item, value in self._stats.as_rows():
            self.stats_tree.insert("", "end", values=(item, value))


if __name__ == "__main__":
    app = CrcErrorApp()
    app.mainloop()

