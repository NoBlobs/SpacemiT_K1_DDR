<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# logsRefs — experiment reference logs

One directory per hardware experiment whose result matters for the
documentation, numbered in order: `000-<name>`, `001-<name>`, …

Each directory contains:

- `readme.md` — filled from [TEMPLATE.md](TEMPLATE.md);
- `serial*.log` — raw UART capture (`/dev/ttyUSB0`, 115200), carriage returns
  stripped;
- `ddr_fw.sha256`, `fsbl.sha256` — hashes of the firmware blob and the signed
  FSBL that were loaded;
- `uboot.rev` — the U-Boot tree, revision and build options used to package and
  sign the image.

Experiments are run on a Banana Pi BPI-F3 over the BootROM's USB download mode
(FDL): `fastboot stage FSBL.bin` / `fastboot continue`, UART captured on the
host. Pages in `docs/` cite these as `logsRefs/NNN-<name>/readme.md`. Negative
results that close a hypothesis are recorded too; runs without a relevant
finding are not versioned.

| # | Experiment | Status | Main finding |
|---|---|---|---|
| 000 | [indep-eye-memory-pattern-sweep](000-indep-eye-memory-pattern-sweep/readme.md) | done | Independent RX/TX eye **training** works on the 8 GiB board: the firmware centres all 32 RX and 32 TX lanes from a memory-pattern sweep alone (no PI harvest), applies the centres and the DRAM verifies with **0 errors** at fp2/2400. Centres match `docsClean/eye-training.md` and the PI taps (RX 17–19, TX 33–37). Still PI: Vref, write leveling, the DQS gate (which software cannot position); only fp2/CS0 so far. |
| 001 | [indep-eye-16gib-micron-baseline](001-indep-eye-16gib-micron-baseline/readme.md) | done | Same delay-only image on the 16 GiB Micron board: `MR8 0x58`, 16384 MB, fp2/2400, **verify OK (0/3)**. RX centres 17–19 (width 9–11). TX per-lane spread is larger (b1 37–38 vs b2 33–34). ~36 s linear sweep. |
| 002 | [indep-vref-edge-16gib](002-indep-vref-edge-16gib/readme.md) | done | F1+F6.1 on 16 GiB: independent RX Vref A=6/B=4, TX MR14 27, then delay re-centre. **verify OK, MR14 check rc 0, 14.5 s** (vs 36 s linear). |
| 003 | [indep-wl-vref-16gib](003-indep-wl-vref-16gib/readme.md) | done | F2 on 16 GiB: independent WL `20 23 19 22` (widths 10 9 10 10) then Vref+delay. **verify OK, 15.0 s**. PI had CS0 b1 WL 24; independent centred at 23. |
| 004 | [indep-all-fps-16gib](004-indep-all-fps-16gib/readme.md) | done | F4 on 16 GiB CS0: independent WL+Vref+delay at **fp0, fp1 and fp2**, each verify OK. Total 45 s. |
| 005 | [indep-cs1-16gib](005-indep-cs1-16gib/readme.md) | done | F5 on 16 GiB both ranks: CS1 at `0x288000000`, all fps **verify OK**. Total 64 s. |
| 006 | [indep-cs1-16gib-soak](006-indep-cs1-16gib-soak/readme.md) | done | Same FSBL as 005, second FDL: both ranks all fps **verify OK**. Total 63.5 s. |
| 007 | [indep-cs1-16gib-soak3](007-indep-cs1-16gib-soak3/readme.md) | done | Third FDL of 005 FSBL: **verify OK**. Total 63.9 s. |
| 008 | [indep-cs1-16gib-soak4](008-indep-cs1-16gib-soak4/readme.md) | done | Fourth FDL of 005 FSBL: **verify OK**. Total 64.3 s. |
| 009 | [indep-cs1-16gib-soak5](009-indep-cs1-16gib-soak5/readme.md) | done | Fifth FDL of 005 FSBL: **verify OK**. Total 63.2 s. |
| 010 | [indep-cs1-16gib-soak6](010-indep-cs1-16gib-soak6/readme.md) | done | Sixth FDL of 005 FSBL: **verify OK**. Total 65.3 s. |
| 011 | [indep-cs1-16gib-soak7](011-indep-cs1-16gib-soak7/readme.md) | done | Seventh FDL of 005 FSBL: **verify OK**. Total 63.6 s. |
| 012 | [f6-cipa-illegal](012-f6-cipa-illegal/readme.md) | done | F6.2: `dcache.cipa` **illegal instruction** (TVAL `0x02b5000b`) after MHCR.DE. |
| 013 | [indep-f6-two-stage-16gib](013-indep-f6-two-stage-16gib/readme.md) | done | F6.3 two-stage, cache off: both ranks all fps **verify OK** in **3.16 s**. |
| 014 | [indep-2666-16gib](014-indep-2666-16gib/readme.md) | done | Independent eye at **2666 MT/s** (`RATE=2666`, PLL1 `0x203c5555`). **verify OK**, `ddr_init rc 0`, 3.32 s. PI fp2 mins 9/8. |
| 015 | [indep-1333-16gib](015-indep-1333-16gib/readme.md) | done | `RATE=1333` parks at **fp0** with the 2400 PLL (not the 2666 VCO). Independent verify OK, DFC back to fp0, DRAM 0 errors. |
| 016 | [indep-1200-16gib](016-indep-1200-16gib/readme.md) | done | `RATE=1200`, final **fp0**. Independent verify OK, 3.33 s, DRAM 0 errors. |
| 017 | [indep-1066-16gib](017-indep-1066-16gib/readme.md) | done | `RATE=1066`, final **fp1**. Independent verify OK, 3.34 s, DRAM 0 errors. |
| 018 | [indep-cs-num-1-16gib](018-indep-cs-num-1-16gib/readme.md) | done | `CS=1` on the 2-rank 16 GiB package: independent CS0 verify OK, 8192 MB. After a PI retrain, two mid-map probes lose kept content; fresh writes still 0 errors. |
