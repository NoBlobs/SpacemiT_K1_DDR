<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# 027 — True 1333 MT/s (2666 VCO at fp0) on 16 GiB Micron

- **Date:** 2026-11-20
- **Goal:** same `RATE=1333` 2666-VCO image as [026](../026-indep-1333-vco-8gib/readme.md)
  on the 16 GiB board. Contrast with [015](../015-indep-1333-16gib/readme.md)
  (2400 VCO at that request).
- **Firmware:** `FSBL-gpl-full-1333.bin`. Hashes in `ddr_fw.sha256` /
  `fsbl.sha256`.
- **Configuration:** FDL `usb:1-1`, UART `/dev/ttyUSB1`, `MR8 0x58`.

## Result

`cs_num 2`, 16384 MB. PLL1 **`0x203c5555` / `0x50dd6538`**, final **fp0**,
`clksel 0x00033b50`. PI fp2 mins **9/8** (2666 CK). Independent verify **OK**
both ranks (CS1 at `0x288000000`). `k1-ddr: rc 0` in **3.13 s**. DRAM 0 errors
after boot and PI retrain.

## Conclusion

- **Confirmed — `RATE=1333` is ≈1333 MT/s** on this Micron 16 GiB.
