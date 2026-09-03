# main.py
import argparse
import random
import serial

from protocol import (DeviceProfile, parse_short_frame, build_long_frame,
                       build_meter_userdata)
from comm_logger import CommLogger
from scenarios import TEST_CATALOG


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


def parse_args():
    p = argparse.ArgumentParser(description="계량기 시뮬레이터")
    p.add_argument("--device-config", required=True, help="device_profile.yaml 경로")
    p.add_argument("--test", required=False, help="시험 항목 ID (미지정 시 목록 출력)")
    p.add_argument("--log", default="comm_log.csv", help="통신 로그 저장 경로")
    return p.parse_args()


def print_catalog():
    print("사용 가능한 시험 항목:")
    for key, factory in TEST_CATALOG.items():
        scenario = factory()
        print(f"  --test {key:<10} : {scenario.description}")


def run(device_path: str, test_id: str, log_path: str):
    profile = DeviceProfile.from_yaml(device_path)
    scenario = TEST_CATALOG[test_id]()
    logger = CommLogger(log_path, test_id=scenario.test_id)
    meter_value = profile.initial_meter_value

    print(f"[Simulator] test={test_id} ({scenario.description})")
    print(f"[Simulator] device: addr={profile.address}, id={profile.id_str}, "
          f"diameter={profile.diameter_mm}mm, port={profile.port}")

    with serial.Serial(profile.port, profile.baudrate, bytesize=8,
                        parity='N', stopbits=1, timeout=0.05) as ser:
        buf = bytearray()
        req_index = 0
        try:
            while True:
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
                        logger.log(req, "request_checksum_error", None)
                        continue
                    if req['c'] != 0x5B or req['a'] != profile.address:
                        logger.log(req, "not_for_me", None)
                        continue

                    req_index += 1
                    mode = scenario.decide(req_index)

                    if mode == "no_response":
                        logger.log(req, mode, None)
                        continue

                    meter_value += random.randint(0, 2)
                    user_data = build_meter_userdata(profile, meter_value)
                    resp = build_long_frame(c_field=0x08, address=profile.address,
                                             user_data=user_data)
                    if mode != "normal":
                        resp = inject_error(resp, mode)

                    ser.write(resp)
                    logger.log(req, mode, resp, note=f"meter_value={meter_value}")

                if len(buf) > 64:
                    buf = buf[-8:]
        except KeyboardInterrupt:
            print("\n[Simulator] 종료")
        finally:
            logger.close()


if __name__ == "__main__":
    args = parse_args()
    if not args.test:
        print_catalog()
    else:
        run(args.device_config, args.test, args.log)

