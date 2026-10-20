#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Thalles Campagnani
"""Drive the eMMC monitor of FSBL-gpl-emmc.bin over the UART.

  emmc_tool.py boot FSBL.bin          FDL load, wait for the prompt
  emmc_tool.py cmd "scan 1 0 8192"    one command, print the answer
  emmc_tool.py dump PART BLK COUNT -o FILE
                                      copy blocks to FILE, crc32 checked
"""

import argparse
import re
import subprocess
import sys
import time
import zlib

import serial

PROMPT = b"K1EMMC> "
BLOCK = 512
UART_BYTES_PER_S = 11000


def open_tty(args) -> serial.Serial:
    return serial.Serial(args.tty, args.baud, timeout=0.2)


def read_until_prompt(ser: serial.Serial, timeout: float, log) -> bytes:
    buf = bytearray()
    deadline = time.time() + timeout
    while time.time() < deadline:
        data = ser.read(65536)
        if data:
            buf.extend(data)
            log.write(data)
            log.flush()
            if buf.endswith(PROMPT):
                return bytes(buf)
    raise TimeoutError(f"no prompt after {timeout:.0f} s, {len(buf)} bytes")


def do_boot(args) -> int:
    devs = subprocess.run(["fastboot", "devices"], capture_output=True, text=True)
    if not devs.stdout.strip():
        print("no fastboot device: the board is not in FDL")
        return 2
    with open_tty(args) as ser, open(args.log, "ab") as log:
        ser.reset_input_buffer()
        for cmd in (["fastboot", "stage", args.fsbl], ["fastboot", "continue"]):
            r = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
            print(f"[emmc] {' '.join(cmd[:2])}: rc {r.returncode} {r.stderr.strip()}")
            if r.returncode:
                return 3
        out = read_until_prompt(ser, args.timeout, log)
    sys.stdout.write(out.decode(errors="replace"))
    return 0


def send(args, line: str, timeout: float) -> str:
    with open_tty(args) as ser, open(args.log, "ab") as log:
        ser.reset_input_buffer()
        ser.write(line.encode() + b"\r")
        out = read_until_prompt(ser, timeout, log)
    return out.decode(errors="replace")


def do_cmd(args) -> int:
    out = send(args, args.line, args.timeout)
    sys.stdout.write(out)
    return 1 if re.search(r"^E ", out, re.M) else 0


def do_dump(args) -> int:
    count = int(args.count, 0)
    blk0 = int(args.blk, 0)
    timeout = 30 + count * BLOCK * 2.4 / UART_BYTES_PER_S
    out = send(args, f"dump {args.part} {args.blk} {args.count}", timeout)
    data = bytearray(count * BLOCK)
    seen = set()
    crc_line = None
    for line in out.splitlines():
        f = line.split()
        if not f:
            continue
        if f[0] in ("Z", "F") and len(f) == 2:
            b = int(f[1], 16) - blk0
            fill = 0 if f[0] == "Z" else 0xFF
            data[b * BLOCK:(b + 1) * BLOCK] = bytes([fill]) * BLOCK
            seen.add(b)
        elif f[0] == "D" and len(f) == 4:
            b = int(f[1], 16) - blk0
            off = b * BLOCK + int(f[2], 16)
            chunk = bytes.fromhex(f[3])
            data[off:off + len(chunk)] = chunk
            seen.add(b)
        elif f[0] == "C" and len(f) == 5:
            crc_line = f
        elif f[0] == "E":
            print(line)
            return 1
    if crc_line is None or len(seen) != count:
        print(f"incomplete dump: {len(seen)}/{count} blocks, crc line {crc_line}")
        return 1
    want = int(crc_line[4], 16)
    got = zlib.crc32(data) & 0xFFFFFFFF
    if got != want:
        print(f"crc mismatch: board {want:08x}, host {got:08x}")
        return 1
    with open(args.out, "wb") as f:
        f.write(data)
    print(f"[emmc] part {args.part} blk {args.blk} count {args.count}: "
          f"{len(data)} bytes, crc32 {got:08x} -> {args.out}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tty", default="/dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--log", required=True)
    ap.add_argument("--timeout", type=float, default=180)
    sub = ap.add_subparsers(dest="what", required=True)
    b = sub.add_parser("boot")
    b.add_argument("fsbl")
    c = sub.add_parser("cmd")
    c.add_argument("line")
    d = sub.add_parser("dump")
    d.add_argument("part")
    d.add_argument("blk")
    d.add_argument("count")
    d.add_argument("-o", "--out", required=True)
    args = ap.parse_args()
    return {"boot": do_boot, "cmd": do_cmd, "dump": do_dump}[args.what](args)


if __name__ == "__main__":
    sys.exit(main())
