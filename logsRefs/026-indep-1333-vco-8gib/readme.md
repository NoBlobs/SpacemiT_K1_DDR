<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# 026 — True 1333 MT/s (2666 VCO at fp0) on 8 GiB Samsung

- **Date:** 2026-11-20
- **Goal:** SPL `RATE=1333` with PLL1 programmed as the 2666 pair, parked at
  fp0. Contrast with [023](../023-indep-1333-8gib/readme.md), which used the
  2400 VCO (1200 MT/s) at the same request.
- **Firmware:** `FSBL-gpl-full-1333.bin` after the VCO special-case. Hashes in
  `ddr_fw.sha256` / `fsbl.sha256`.
- **Configuration:** FDL `usb:1-1`, UART `/dev/ttyUSB0`, `MR8 0x52`.

## Result

`cs_num 2`, 8192 MB. PLL1 **`0x203c5555` / `0x50dd6538`**, final **fp0**,
`clksel 0x00033b50`. PI fp0 mins 21/17; PI fp2 mins **9/8** (2666 CK, not the
2400 10/10 of 023). Independent verify **OK** both ranks. `k1-ddr: rc 0` in
**3.17 s**. DRAM 0 errors after boot and PI retrain.

## Conclusion

- **Confirmed — `RATE=1333` is ≈1333 MT/s** on this Samsung 8 GiB: 2666 VCO,
  fp0 park, fp2 trained at 2666.
