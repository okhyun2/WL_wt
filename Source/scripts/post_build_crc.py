#!/usr/bin/env python3
"""
post_build_crc.py
Calculate CRC32 after STM32 firmware build and insert into .bin and .hex files.

Usage:
    python post_build_crc.py <firmware.bin> <firmware.hex> --base <address>

Example:
    python post_build_crc.py firmware.bin firmware.hex --base 0x08004000
    python post_build_crc.py firmware.bin firmware.hex --base 0x08008000
"""

import struct
import sys
import os
import argparse
from typing import Dict, Tuple

# -------------------------------------------------------
# Constants
# -------------------------------------------------------
MAGIC_NUMBER_VALUE = 0xDEADC0DE
SEARCH_RANGE       = 0x200       # Magic number search range
MAGIC_MIN_OFFSET   = 0x40        # Search start offset (minimum vector table size)


# -------------------------------------------------------
# CRC32 calculation (STM32 S/W Byte CRC method - CRC-32/MPEG-2)
# -------------------------------------------------------
def calc_crc32_stm32(data: bytes) -> int:
    """
    CRC-32/MPEG-2 byte-by-byte
    Polynomial : 0x04C11DB7
    Init value : 0xFFFFFFFF
    Input      : byte order as stored in memory (no endian swap)
    """
    crc = 0xFFFFFFFF
    for byte in data:
        crc ^= (byte << 24)
        for _ in range(8):
            if crc & 0x80000000:
                crc = (crc << 1) ^ 0x04C11DB7
            else:
                crc <<= 1
            crc &= 0xFFFFFFFF
    return crc

# -------------------------------------------------------
# Find header offset by magic number in BIN
# -------------------------------------------------------
def find_magic_offset_bin(data: bytearray) -> int:
    for offset in range(MAGIC_MIN_OFFSET, SEARCH_RANGE, 4):
        val = struct.unpack_from('<I', data, offset)[0]
        if val == MAGIC_NUMBER_VALUE:
            print(f"[BIN] magic found at offset : 0x{offset:03X}")
            return offset

    raise ValueError(
        f"Magic number 0x{MAGIC_NUMBER_VALUE:08X} not found in BIN.\n"
        f"  -> Check magicNumber = 0x{MAGIC_NUMBER_VALUE:08X} in app_header.c\n"
        f"  -> Check .image_header section in linker script"
    )


# -------------------------------------------------------
# Find header offset by magic number in HEX
# -------------------------------------------------------
def find_magic_offset_hex(memory: Dict[int, int], base: int) -> int:
    for offset in range(MAGIC_MIN_OFFSET, SEARCH_RANGE, 4):
        abs_addr = base + offset
        val = struct.unpack('<I', bytes([
            memory.get(abs_addr + 0, 0xFF),
            memory.get(abs_addr + 1, 0xFF),
            memory.get(abs_addr + 2, 0xFF),
            memory.get(abs_addr + 3, 0xFF),
        ]))[0]

        if val == MAGIC_NUMBER_VALUE:
            print(f"[HEX] magic found at : 0x{abs_addr:08X} "
                  f"(base=0x{base:08X}, offset=0x{offset:03X})")
            return offset

    raise ValueError(
        f"Magic number 0x{MAGIC_NUMBER_VALUE:08X} not found in HEX "
        f"at base 0x{base:08X}.\n"
        f"  -> Check --base address is correct\n"
        f"  -> Check section attribute in app_header.c\n"
        f"  -> Check .image_header section in linker script"
    )


# -------------------------------------------------------
# Parse Intel HEX file
# -------------------------------------------------------
def parse_hex(hex_path: str) -> Dict[int, int]:
    memory: Dict[int, int] = {}
    upper_address = 0

    with open(hex_path, 'r') as f:
        lines = f.readlines()

    for line in lines:
        line = line.strip()
        if not line.startswith(':'):
            continue

        raw        = bytes.fromhex(line[1:])
        byte_count = raw[0]
        address    = (raw[1] << 8) | raw[2]
        rec_type   = raw[3]
        data       = raw[4:4 + byte_count]
        checksum   = raw[4 + byte_count]

        calc_sum = (256 - (sum(raw[:-1]) & 0xFF)) & 0xFF
        if calc_sum != checksum:
            raise ValueError(f"HEX checksum error: {line}")

        if rec_type == 0x04:
            # Extended Linear Address Record
            upper_address = ((data[0] << 8) | data[1]) << 16

        elif rec_type == 0x00:
            # Data Record
            abs_address = upper_address | address
            for i, byte in enumerate(data):
                memory[abs_address + i] = byte

        elif rec_type == 0x01:
            # EOF Record
            break

    return memory


# -------------------------------------------------------
# Calculate Intel HEX checksum
# -------------------------------------------------------
def calc_hex_checksum(byte_count: int, address: int,
                      rec_type: int, data: bytes) -> int:
    total  = byte_count
    total += (address >> 8) & 0xFF
    total += address & 0xFF
    total += rec_type
    total += sum(data)
    return (256 - (total & 0xFF)) & 0xFF


# -------------------------------------------------------
# Patch HEX memory at specific address
# -------------------------------------------------------
def patch_hex_memory(memory: Dict[int, int],
                     address: int, value: int, size: int = 4):
    pack_fmt = {1: '<B', 2: '<H', 4: '<I'}[size]
    packed = struct.pack(pack_fmt, value)
    for i, byte in enumerate(packed):
        memory[address + i] = byte


# -------------------------------------------------------
# Convert memory dict to Intel HEX string
# -------------------------------------------------------
def memory_to_hex(memory: Dict[int, int], bytes_per_line: int = 16) -> str:
    if not memory:
        return ":00000001FF\n"

    sorted_addresses = sorted(memory.keys())
    lines = []
    current_upper = -1
    i = 0

    while i < len(sorted_addresses):
        abs_addr = sorted_addresses[i]
        upper    = (abs_addr >> 16) & 0xFFFF

        if upper != current_upper:
            current_upper = upper
            data = struct.pack('>H', upper)
            chk  = calc_hex_checksum(2, 0, 0x04, data)
            lines.append(f":02000004{upper:04X}{chk:02X}")

        chunk     = []
        base_addr = abs_addr

        while (len(chunk) < bytes_per_line and
               i < len(sorted_addresses) and
               sorted_addresses[i] == base_addr + len(chunk) and
               ((sorted_addresses[i] >> 16) & 0xFFFF) == current_upper):
            chunk.append(memory[sorted_addresses[i]])
            i += 1

        addr16 = base_addr & 0xFFFF
        data   = bytes(chunk)
        chk    = calc_hex_checksum(len(chunk), addr16, 0x00, data)
        lines.append(f":{len(chunk):02X}{addr16:04X}00{data.hex().upper()}{chk:02X}")

    lines.append(":00000001FF")
    return "\n".join(lines) + "\n"


# -------------------------------------------------------
# Process .bin file
# -------------------------------------------------------
def process_bin(bin_path: str) -> Tuple[bytearray, int, int, int]:
    with open(bin_path, 'rb') as f:
        data = bytearray(f.read())

    header_offset        = find_magic_offset_bin(data)
    firmware_size_offset = header_offset + 4
    firmware_crc_offset  = header_offset + 8

    # Pad to 4-byte alignment
    if len(data) % 4 != 0:
        data += b'\xFF' * (4 - len(data) % 4)

    firmware_size = len(data)

    struct.pack_into('<I', data, firmware_size_offset, firmware_size)
    struct.pack_into('<I', data, firmware_crc_offset,  0x00000000)

    crc = calc_crc32_stm32(bytes(data))
    struct.pack_into('<I', data, firmware_crc_offset, crc)

    with open(bin_path, 'wb') as f:
        f.write(data)

    return data, firmware_size, crc, header_offset


# -------------------------------------------------------
# Process .hex file
# -------------------------------------------------------
def process_hex(hex_path: str, base: int,
                firmware_size: int, crc: int) -> int:
    memory        = parse_hex(hex_path)
    header_offset = find_magic_offset_hex(memory, base)
    abs_header    = base + header_offset

    patch_hex_memory(memory, abs_header + 4, firmware_size)
    patch_hex_memory(memory, abs_header + 8, crc)

    with open(hex_path, 'w') as f:
        f.write(memory_to_hex(memory))

    return header_offset


# -------------------------------------------------------
# Main
# -------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(
        description='STM32 Post-Build CRC Inserter'
    )
    parser.add_argument('bin',
                        help='Firmware .bin file path')
    parser.add_argument('hex',
                        help='Firmware .hex file path')
    parser.add_argument('--base',
                        type=lambda x: int(x, 0),
                        required=True,
                        help='Slot base address (e.g. 0x08004000)')

    args = parser.parse_args()

    if not os.path.exists(args.bin):
        print(f"[ERROR] BIN not found: {args.bin}")
        sys.exit(1)

    if not os.path.exists(args.hex):
        print(f"[ERROR] HEX not found: {args.hex}")
        sys.exit(1)

    print("=" * 55)
    print(" STM32 Post-Build CRC Inserter")
    print("=" * 55)
    print(f"[INFO] base address  : 0x{args.base:08X}")

    try:
        _, firmware_size, crc, bin_offset = process_bin(args.bin)
        print(f"[BIN] header offset  : 0x{bin_offset:03X}")
        print(f"[BIN] firmware size  : {firmware_size} bytes "
              f"({firmware_size / 1024:.1f} KB)")
        print(f"[BIN] CRC32          : 0x{crc:08X}")
        print(f"[BIN] patched        : {args.bin} ")

        hex_offset = process_hex(args.hex, args.base, firmware_size, crc)
        print(f"[HEX] header offset  : 0x{hex_offset:03X}")
        print(f"[HEX] patched        : {args.hex} ")

    except ValueError as e:
        print(f"\n[ERROR] {e}")
        sys.exit(1)

    print("=" * 55)
    print(" Done!")
    print("=" * 55)


if __name__ == '__main__':
    main()
