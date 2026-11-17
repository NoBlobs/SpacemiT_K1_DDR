#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Thalles Campagnani
"""Summarise a vehicle UART log: rate, PLL, each pass, handoff, latency."""

from __future__ import annotations

import argparse
import pathlib
import re
import sys


def gate_bytes(text: str) -> list[int]:
    out: list[int] = []
    for a, b, c, d in re.findall(
            r"gate (0x[0-9a-f]+) (0x[0-9a-f]+) (0x[0-9a-f]+) (0x[0-9a-f]+)",
            text):
        for code in (a, b, c, d):
            v = int(code, 16)
            out += [(v >> 8) & 0xff, v & 0xff]
    return out


def ints(pat: str, text: str) -> list[int]:
    return [int(x) for t in re.findall(pat, text) for x in t]


def summarise(path: pathlib.Path) -> str:
    text = path.read_text(errors="replace").replace("\r", "")
    boot, _, rest = text.partition("K1DDRT: ddr_init rc")
    lines = [f"== {path.name}"]
    m = re.search(r"k1-ddr: LPDDR4X?, cs_num \d, (\d+) MT/s, final fp(\d)", boot)
    lines.append(f"  rate {m.group(1)} final fp{m.group(2)}" if m else "  rate ?")
    pll = re.findall(r"k1-ddr: pll1 (0x\w+ 0x\w+)", boot)
    if pll:
        lines.append("  pll1 " + " ".join(pll))
    for p in re.split(r"(?=k1-ddrt: pass pp\d)", boot)[1:]:
        ok = re.search(
            r"(pp\d) ok: rx vref idx (\d+) \(min (\d+)\), tx mr14 (\d+) \(min (\d+)\)",
            p)
        hdr = re.search(
            r"pass (\d) pp\d rc (-?\d+) .*phy_status (0x\w+), (\d+) us", p)
        gates = gate_bytes(p)
        wl = ints(r"cs\d wl\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)", p)
        rxdq = ints(r"rx dq0\s+b0:(\d+) b1:(\d+) b2:(\d+) b3:(\d+)", p)
        txdq = ints(r"tx dq0\s+b0:(\d+) b1:(\d+) b2:(\d+) b3:(\d+)", p)
        dfc = re.search(
            r"DFC fp(\d) in (\d+) us: ctl_freq (0x\w+) .*clksel (0x\w+)", p)
        if not ok or not hdr or not gates or not wl or not rxdq or not txdq:
            lines.append("  pass ? (incomplete)")
            continue
        extra = (f" | then DFC fp{dfc.group(1)} {dfc.group(2)} us "
                 f"{dfc.group(3)} {dfc.group(4)}" if dfc else "")
        lines.append(
            f"  pass {hdr.group(1)} {ok.group(1)} rc {hdr.group(2)} "
            f"phy {hdr.group(3)} rx {ok.group(2)}({ok.group(3)}) "
            f"tx {ok.group(4)}({ok.group(5)}) gate {min(gates)}-{max(gates)} "
            f"wl {min(wl)}-{max(wl)} rxdq0 {min(rxdq)}-{max(rxdq)} "
            f"txdq0 {min(txdq)}-{max(txdq)} {hdr.group(4)} us{extra}")
    fin = re.search(
        r"k1-ddr: rc (-?\d+) .* in (\d+) us: .*?(\d+) MB, fp(\d) "
        r"ctl_freq (0x\w+) status (0x\w+) clksel (0x\w+), (\d+) item",
        boot)
    if fin:
        lines.append(
            f"  final rc {fin.group(1)} {fin.group(3)} MB fp{fin.group(4)} "
            f"ctl_freq {fin.group(5)} status {fin.group(6)} "
            f"clksel {fin.group(7)} out-of-range {fin.group(8)} "
            f"in {fin.group(2)} us")
    mrs = sorted(set(re.findall(r"cs\d ch\d (MR5:.*)", boot)))
    lines.append(f"  MRs {mrs or '-'}")
    oor = re.findall(r"out of range: (.*)", text)
    warn = re.findall(r"warning[^\n]*", text)
    lines.append(f"  warnings {warn or '-'} oor-items {oor or '-'}")
    rt = re.search(r"k1-ddrt retrain: rc (-?\d+)", rest)
    errs = sum(int(a) + int(b) for a, b in
               re.findall(r"kept-errors (\d+) fresh-errors (\d+)", text))
    lat = dict(re.findall(
        r"lat (seq 32768|rand 32768|rand 16) KiB: ([\d.]+) ns", text))
    lines.append(
        f"  retrain rc {rt.group(1) if rt else '?'} dram-errors {errs} lat {lat}")
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("log", nargs="+", type=pathlib.Path)
    args = ap.parse_args()
    missing = [p for p in args.log if not p.is_file()]
    if missing:
        print("not found: " + ", ".join(str(p) for p in missing), file=sys.stderr)
        return 1
    for p in args.log:
        print(summarise(p))
    return 0


if __name__ == "__main__":
    sys.exit(main())
