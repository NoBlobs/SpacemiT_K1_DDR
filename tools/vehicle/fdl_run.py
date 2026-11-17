#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Thalles Campagnani
"""Load FSBLs through the K1 BootROM fastboot (FDL) and capture the UART.

One FSBL and --runs 1 writes --log as given. Otherwise run i loads
fsbl[i % len(fsbl)] and logs to <log stem>-NN<suffix>. Images built with
the vehicle reset (doc 32) ask for a reset path after K1DDRT-DONE; the
answer is --reset, and the run then waits for the BootROM to be back in
download mode, so the next run needs nobody at the board. With --ymodem
the SPL (MODE=uboot) gets the next stage over the UART instead, and the run
ends at the U-Boot prompt after --cmd.
"""

import argparse
import pathlib
import re
import subprocess
import sys
import threading
import time

import serial

import ymodem

PROMPT = b"K1VEH: reset?"
BACK_IN_FDL = b"usb download handler"
HALTED = b"Please RESET"
AUTOBOOT = b"Hit any key to stop autoboot"
P1_RESET = "p1-reset"


class Tap:
    """Serial reader that appends to whichever log is current."""

    def __init__(self, ser: serial.Serial, echo: bool) -> None:
        self.ser = ser
        self.echo = echo
        self.lock = threading.Lock()
        self.buf = bytearray()
        self.q: bytearray | None = None
        self.f = None
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()

    def _run(self) -> None:
        while not self.stop.is_set():
            data = self.ser.read(self.ser.in_waiting or 1)
            if not data:
                continue
            with self.lock:
                self.buf.extend(data)
                if self.q is not None:
                    self.q.extend(data)
                if self.f:
                    self.f.write(data)
                    self.f.flush()
            if self.echo:
                sys.stdout.buffer.write(data)
                sys.stdout.buffer.flush()

    def switch(self, path: pathlib.Path) -> None:
        with self.lock:
            if self.f:
                self.f.close()
            self.f = open(path, "wb")
            self.buf = bytearray()

    def wait(self, *needles: bytes, timeout: float, start: int = 0) -> bytes | None:
        deadline = time.time() + timeout
        while time.time() < deadline:
            with self.lock:
                for n in needles:
                    if self.buf.find(n, start) >= 0:
                        return n
            time.sleep(0.05)
        return None

    def mark(self) -> int:
        with self.lock:
            return len(self.buf)

    def capture(self, on: bool) -> None:
        """Queue the received bytes for getc() (a protocol on the line)."""
        with self.lock:
            self.q = bytearray() if on else None

    def getc(self, timeout: float) -> int | None:
        deadline = time.time() + timeout
        while True:
            with self.lock:
                if self.q:
                    c = self.q[0]
                    del self.q[0]
                    return c
            if time.time() >= deadline:
                return None
            time.sleep(0.001)

    def text(self) -> str:
        with self.lock:
            return self.buf.decode("utf-8", "replace")

    def close(self) -> None:
        self.stop.set()
        self.thread.join(1)
        with self.lock:
            if self.f:
                self.f.close()
                self.f = None


def fastboot_ready(timeout: float) -> bool:
    deadline = time.time() + timeout
    while True:
        r = subprocess.run(["fastboot", "devices"], capture_output=True, text=True)
        if r.stdout.strip():
            return True
        if time.time() >= deadline:
            return False
        time.sleep(0.5)


def summary(text: str) -> str:
    def first(pat: str) -> str:
        m = re.findall(pat, text)
        return m[0] if m else "?"

    def last(pat: str) -> str:
        m = re.findall(pat, text)
        return m[-1] if m else "?"

    rc = first(r"K1DDRT: ddr_init rc (-?\d+)")
    pp2 = first(r"pp2 ok: (rx vref idx \d+ \(min \d+\), tx mr14 \d+ \(min \d+\))")
    errs = sum(int(a) + int(b) for a, b in
               re.findall(r"kept-errors (\d+) fresh-errors (\d+)", text))
    sys_next = last(r"\nsys: (0x[0-9a-f]+)")
    uboot = "U-Boot prompt" if re.search(r"\nU-Boot 20[^\n]*\n[\s\S]*\n=> ", text) else "no U-Boot"
    mtest = re.findall(r"with (\d+) errors", text)
    return (f"ddr_init rc {rc}, first pp2 {pp2}, dram errors {errs}, {uboot}, "
            f"mtest errors {mtest or '-'}, next sys {sys_next}")


def next_stage(tap: Tap, args) -> bool:
    """Send --ymodem to the SPL, stop autoboot, run --cmd at the prompt."""
    path = pathlib.Path(args.ymodem)
    step = max(1, path.stat().st_size // 10)
    shown = [0]

    def progress(done: int, total: int) -> None:
        if done - shown[0] >= step or done == total:
            shown[0] = done
            print(f"[ymodem] {done}/{total}", flush=True)

    t0 = time.time()
    tap.capture(True)
    try:
        ok = ymodem.send(path, tap.ser.write, tap.getc, progress)
    finally:
        tap.capture(False)
    print(f"[ymodem] {'sent' if ok else 'FAILED'} in {time.time() - t0:.0f} s",
          flush=True)
    if not ok:
        return False
    prompt = args.uboot_prompt.encode()
    if tap.wait(AUTOBOOT, prompt, timeout=args.timeout) == AUTOBOOT:
        tap.ser.write(b"\n")
    if not tap.wait(prompt, timeout=10):
        print("[fdl] no U-Boot prompt")
        return False
    for cmd in args.cmd:
        if cmd == P1_RESET:
            return p1_reset(tap, args)
        if uboot_cmd(tap, args, cmd) is None:
            return False
    return True


def uboot_cmd(tap: Tap, args, cmd: str) -> str | None:
    """Run cmd at the U-Boot prompt; its output, or None without a prompt."""
    start = tap.mark()
    tap.ser.write(cmd.encode() + b"\n")
    prompt = args.uboot_prompt.encode()
    if not tap.wait(prompt, timeout=args.cmd_timeout, start=start + len(cmd)):
        print(f"[fdl] no prompt after {cmd!r}")
        return None
    return tap.text()[start:]


def p1_reset(tap: Tap, args) -> bool:
    """Doc 32 PMIC reset from the prompt: P1[0xAB] |= 0x04, P1[0x7E] |= 0x02.

    The pmic command stops at the driver's register count, below 0xAB, so
    this goes through i2c on the P1 bus.
    """
    if uboot_cmd(tap, args, f"i2c dev {args.p1_bus}") is None:
        return False
    for reg, bit in ((0xAB, 0x04), (0x7E, 0x02)):
        out = uboot_cmd(tap, args, f"i2c md 0x41 0x{reg:x} 1")
        m = re.search(rf"00{reg:02x}: ([0-9a-f]{{2}})", out or "")
        if not m:
            print(f"[fdl] could not read P1 0x{reg:x}")
            return False
        val = int(m.group(1), 16) | bit
        print(f"[fdl] P1 0x{reg:x}: 0x{m.group(1)} -> 0x{val:02x}", flush=True)
        if reg == 0x7E:
            tap.ser.write(f"i2c mw 0x41 0x{reg:x} 0x{val:02x}\n".encode())
            return tap.wait(BACK_IN_FDL, timeout=args.back_timeout) is not None
        if uboot_cmd(tap, args, f"i2c mw 0x41 0x{reg:x} 0x{val:02x}") is None:
            return False
    return False


def run_one(tap: Tap, fsbl: str, log: pathlib.Path, args) -> bool:
    tap.switch(log)
    for cmd in (["fastboot", "stage", fsbl], ["fastboot", "continue"]):
        try:
            r = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
        except subprocess.TimeoutExpired:
            print(f"[fdl] {' '.join(cmd[:2])}: TIMEOUT after 60 s", flush=True)
            return False
        print(f"[fdl] {' '.join(cmd[:2])}: rc {r.returncode} {r.stderr.strip()}",
              flush=True)
        if r.returncode:
            return False
    if not tap.wait(args.done.encode(), timeout=args.timeout):
        print(f"[fdl] TIMEOUT waiting for {args.done}")
        return False
    if args.ymodem:
        return next_stage(tap, args)
    if tap.wait(PROMPT, HALTED, timeout=8) == PROMPT:
        tap.ser.write(args.reset.encode())
        if args.reset != "h":
            if not tap.wait(BACK_IN_FDL, timeout=args.back_timeout):
                print("[fdl] the BootROM did not come back in download mode")
                return False
            return True
    tap.wait(HALTED, timeout=3)
    return True


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("fsbl", nargs="+")
    ap.add_argument("--log", required=True)
    ap.add_argument("--runs", type=int, default=1)
    ap.add_argument("--reset", choices="pwh", default="p",
                    help="answer to the vehicle reset prompt")
    ap.add_argument("--tty", default="/dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--timeout", type=float, default=120)
    ap.add_argument("--fdl-wait", type=float, default=30,
                    help="seconds to wait for the fastboot device per run")
    ap.add_argument("--back-timeout", type=float, default=20)
    ap.add_argument("--wait-banner", type=float, default=0,
                    help="first wait this long for the BootROM banner on the UART "
                         "(a stale USB gadget still lists in fastboot devices)")
    ap.add_argument("--done", default="K1DDRT-DONE")
    ap.add_argument("--ymodem", metavar="ITB",
                    help="after --done, send this next stage to the SPL by ymodem")
    ap.add_argument("--uboot-prompt", default="=> ")
    ap.add_argument("--cmd", action="append", default=[],
                    help="U-Boot command to run at the prompt (repeatable); "
                         f"'{P1_RESET}' resets through the PMIC (doc 32) and "
                         "waits for the BootROM")
    ap.add_argument("--cmd-timeout", type=float, default=60)
    ap.add_argument("--p1-bus", type=int, default=8, help="i2c bus of the P1")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    total = args.runs
    base = pathlib.Path(args.log)
    single = total == 1 and len(args.fsbl) == 1
    # nothing brings the board back to the BootROM: no further run
    stays_up = args.reset == "h" if not args.ymodem else P1_RESET not in args.cmd

    ser = serial.Serial(args.tty, args.baud, timeout=0.2)
    ser.reset_input_buffer()
    tap = Tap(ser, echo=not args.quiet)
    ok = 0
    try:
        if args.wait_banner:
            tap.switch(base.with_name(f"{base.stem}-bootrom{base.suffix}"))
            print(f"[fdl] waiting {args.wait_banner:.0f} s for the BootROM banner", flush=True)
            if not tap.wait(BACK_IN_FDL, timeout=args.wait_banner):
                print("[fdl] no BootROM banner")
                return 1
            time.sleep(1)
        for i in range(total):
            fsbl = args.fsbl[i % len(args.fsbl)]
            log = base if single else base.with_name(f"{base.stem}-{i:02d}{base.suffix}")
            if not fastboot_ready(args.fdl_wait):
                print(f"[fdl] run {i}: no fastboot device after {args.fdl_wait:.0f} s")
                break
            good = run_one(tap, fsbl, log, args)
            print(f"[fdl] run {i} {pathlib.Path(fsbl).name} -> {log}: "
                  f"{summary(tap.text())}{'' if good else ' (FAILED)'}", flush=True)
            if not good:
                break
            ok += 1
            if stays_up:
                break
    finally:
        tap.close()
        ser.close()
    print(f"[fdl] {ok}/{total} run(s) finished")
    return 0 if ok == total or (stays_up and ok) else 1


if __name__ == "__main__":
    sys.exit(main())
