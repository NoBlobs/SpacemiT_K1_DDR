<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# 024 — Independent eye at 1200 MT/s (8 GiB Samsung)

- **Date:** 2026-11-20
- **Goal:** SPL `RATE=1200` on the 8 GiB board. Final fp is 0.
- **Firmware:** `FSBL-gpl-full-1200.bin`. Hashes in `ddr_fw.sha256` /
  `fsbl.sha256`.
- **Configuration:** FDL `usb:1-1`, UART `/dev/ttyUSB0`, `MR8 0x52`.

## Result

`cs_num 2`, 8192 MB, PLL1 `0x2a203b00`, final **fp0**, `clksel 0x00033b50`.
Independent verify **OK** both ranks. `k1-ddr: rc 0` in **3.36 s**. DRAM 0
errors after boot and PI retrain.

## Conclusion

- **Confirmed — 1200 independent eye on this Samsung 8 GiB.**
