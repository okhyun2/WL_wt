# protocol.py
from dataclasses import dataclass
import yaml


DIAMETER_TO_DIF_HI = {
    15: 0x1, 20: 0x2, 25: 0x3, 32: 0x4, 40: 0x5, 50: 0x6,
    80: 0x7, 100: 0x8, 150: 0x9, 200: 0xA, 250: 0xB, 300: 0xC,
}
DIAMETER_TO_DECIMAL_POS = {
    15: 3, 20: 3, 25: 3, 32: 3, 40: 3, 50: 3,
    80: 2, 100: 2, 150: 2, 200: 2, 250: 2, 300: 2,
}
DATA_LENGTH_CODE = 0x0C
VIF_UNIT_M3 = 0x1
CI_FIELD_FIXED = 0x78
MDH_FIXED = 0x0F


@dataclass
class DeviceProfile:
    port: str
    baudrate: int
    address: int
    id_str: str
    diameter_mm: int
    initial_meter_value: int
    battery_code: int = 0
    q3_over: bool = False
    backflow: bool = False
    indoor_leak: bool = False

    @classmethod
    def from_yaml(cls, path: str) -> "DeviceProfile":
        with open(path, "r", encoding="utf-8") as f:
            data = yaml.safe_load(f)
        return cls(**data)


def bcd_encode(num_str: str, n_bytes: int = 4) -> bytes:
    digits = num_str.replace('-', '').zfill(n_bytes * 2)
    be = bytes(int(digits[i:i + 2], 16) for i in range(0, len(digits), 2))
    return be[::-1]


def checksum(data: bytes) -> int:
    return sum(data) & 0xFF


def parse_short_frame(frame: bytes):
    if len(frame) != 5 or frame[0] != 0x10 or frame[4] != 0x16:
        return None
    c_field, a_field, cs = frame[1], frame[2], frame[3]
    return {'valid': checksum(frame[1:3]) == cs, 'c': c_field, 'a': a_field}


def build_long_frame(c_field: int, address: int, user_data: bytes,
                      ci_field: int = CI_FIELD_FIXED) -> bytes:
    core = bytes([c_field, address, ci_field]) + user_data
    length = len(core)
    cs = checksum(core)
    return bytes([0x68, length, length, 0x68]) + core + bytes([cs, 0x16])


def build_status_byte(q3_over, backflow, indoor_leak, battery_code) -> int:
    b = 0
    if q3_over: b |= 0x80
    if backflow: b |= 0x40
    if indoor_leak: b |= 0x20
    b |= (battery_code & 0x1F)
    return b


def build_meter_userdata(profile: DeviceProfile, meter_value_int: int,
                          status_override: dict | None = None) -> bytes:
    id_bytes = bcd_encode(profile.id_str, 4)

    if status_override:
        q3_over = status_override.get("q3_over", profile.q3_over)
        backflow = status_override.get("backflow", profile.backflow)
        indoor_leak = status_override.get("indoor_leak", profile.indoor_leak)
        battery_code = status_override.get("battery_code", profile.battery_code)
    else:
        q3_over, backflow, indoor_leak, battery_code = (
            profile.q3_over, profile.backflow, profile.indoor_leak, profile.battery_code
        )

    status_byte = build_status_byte(q3_over, backflow, indoor_leak, battery_code)
    dif = (DIAMETER_TO_DIF_HI[profile.diameter_mm] << 4) | DATA_LENGTH_CODE
    decimal_pos = DIAMETER_TO_DECIMAL_POS[profile.diameter_mm]
    vif = (VIF_UNIT_M3 << 4) | (decimal_pos & 0x0F)
    value_str = str(meter_value_int).zfill(8)
    data_bytes = bcd_encode(value_str, 4)
    return bytes([MDH_FIXED]) + id_bytes + bytes([status_byte, dif, vif]) + data_bytes


