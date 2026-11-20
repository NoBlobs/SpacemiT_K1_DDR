<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# 022 — Independent eye at 2666 MT/s (8 GiB Samsung)

- **Date:** 2026-11-20
- **Goal:** same `K1_INDEP_EYE` blob as 2400, SPL `RATE=2666` (PLL1 2666
  recipe). 8 GiB board, no `CS=` override.
- **Firmware:** `FSBL-gpl-full-2666.bin`. Hashes in `ddr_fw.sha256` /
  `fsbl.sha256`.
- **Configuration:** FDL `usb:1-1`, UART `/dev/ttyUSB0`, `MR8 0x52`, 8192 MB.

## Result

`k1-ddr: rc 0` in **3.34 s**. PLL1 `0x203c5555` / `0x50dd6538`. PI fp2 mins 9/8
(2666 CK). Independent verify **OK** both ranks, all fps. DRAM probes 0 errors
after boot and PI retrain.

## Conclusion

- **Confirmed — 2666 independent eye on this Samsung 8 GiB.**
