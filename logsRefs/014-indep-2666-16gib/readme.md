<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# 014 — Independent eye at 2666 MT/s (16 GiB Micron)

- **Date:** 2026-11-20
- **Goal:** same `K1_INDEP_EYE` blob as 2400, SPL `RATE=2666` (PLL1 2666
  recipe). 16 GiB board, `cs_num 2`.
- **Firmware:** `FSBL-gpl-full-2666.bin`. Hashes in `ddr_fw.sha256` /
  `fsbl.sha256`.
- **Configuration:** FDL `usb:1-1`, UART `/dev/ttyUSB1`, `MR8 0x58`, 16384 MB.

## Result

`k1-ddr: rc 0` in **3.32 s**. PLL1 `0x203c5555` / `0x50dd6538` (not the 2400
pair). PI fp2 mins 9/8 (the 2666 CK). Independent verify **OK** both ranks,
all fps. DRAM probes 0 errors.

| fp | eye time | CS0 WL | CS1 WL | RX A/B | TX MR14 |
|---|---|---|---|---|---|
| 0 | 1.39 s | 19 22 18 22 | 19 22 18 21 | 4 / 4 | 22 |
| 1 | 1.35 s | 19 20 19 20 | 19 20 19 20 | 3 / 3 | 21 |
| 2 | 1.42 s | 20 22 19 21 | 20 22 19 22 | 5 / 5 | 28 |

## Conclusion

- **Confirmed — 2666 independent eye on this Micron 16 GiB.**
- Vehicle `--reset p` left FDL and booted GNU from MMC; remaining rates
  need a fresh BootROM download.
