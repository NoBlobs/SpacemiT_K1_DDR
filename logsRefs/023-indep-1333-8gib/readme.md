<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# 023 — Independent eye at 1333 MT/s (8 GiB Samsung)

- **Date:** 2026-11-20
- **Goal:** SPL `RATE=1333` on the 8 GiB board. Final fp is 0 with the 2400 PLL
  (not the 2666 VCO).
- **Firmware:** `FSBL-gpl-full-1333.bin`. Hashes in `ddr_fw.sha256` /
  `fsbl.sha256`.
- **Configuration:** FDL `usb:1-1`, UART `/dev/ttyUSB0`, `MR8 0x52`.

## Result

`cs_num 2`, 8192 MB, PLL1 `0x2a203b00` (2400 VCO), final **fp0**,
`clksel 0x00033b50`. Independent verify **OK** both ranks at fp1, fp2 and fp0.
`k1-ddr: rc 0` in **3.38 s**. DRAM 0 errors after boot and PI retrain.

## Conclusion

- **Confirmed — 1333 request on this Samsung 8 GiB parks at fp0 / 1200 MT/s**,
  same mapping as the 16 GiB run.
