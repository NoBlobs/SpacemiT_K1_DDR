<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# 018 — Independent eye with cs_num 1 on the 16 GiB Micron (2-rank package)

- **Date:** 2026-11-20
- **Goal:** SPL `CS=1` so `ddr_init` trains one rank on a two-rank part. CS1
  stays at reset delays (`docsClean/training-pass.md`).
- **Firmware:** `FSBL-gpl-full-cs1.bin`. Hashes in `ddr_fw.sha256` /
  `fsbl.sha256`.
- **Configuration:** FDL `usb:1-1`, UART `/dev/ttyUSB1`, 2400 MT/s, `MR8 0x58`.

## Result

`k1-ddr: rc 0` in **2.16 s** (one rank). Map 8192 MB, CS1 present bit clear
(`cs1 0x00110000`). Independent verify **OK** on CS0 at fp0/fp1/fp2. DRAM
probes after `ddr_init`: 0 errors. After the vehicle PI retrain: two probes
lost kept content (`0x40000000` and `0x1c0000000`, 16384 kept-errors each);
fresh writes still 0 errors; retrain `rc 0`.

| fp | eye time | CS0 WL | RX A/B | TX MR14 |
|---|---|---|---|---|
| 0 | 0.85 s | 19 21 18 21 | 4 / 6 | 28 |
| 1 | 0.86 s | 19 20 18 20 | 2 / 3 | 21 |
| 2 | 0.87 s | 20 23 19 23 | 4 / 5 | 23 |

## Conclusion

- **Confirmed — `cs_num 1` independent eye on this 16 GiB package.**
- PI retrain on one rank of a two-rank part does not keep those two probe
  patterns; the independent path itself verified clean.
