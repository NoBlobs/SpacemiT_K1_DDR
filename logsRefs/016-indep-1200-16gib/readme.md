<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# 016 — Independent eye at 1200 MT/s (16 GiB Micron)

- **Date:** 2026-11-20
- **Goal:** same `K1_INDEP_EYE` blob, SPL `RATE=1200`. Final fp is 0; boot
  walks fp0/fp1/fp2, then DFC back to fp0.
- **Firmware:** `FSBL-gpl-full-1200.bin`. Hashes in `ddr_fw.sha256` /
  `fsbl.sha256`.
- **Configuration:** FDL `usb:1-1`, UART `/dev/ttyUSB1`, `MR8 0x58`, 16384 MB.

## Result

`k1-ddr: rc 0` in **3.33 s**, `fp0`, PLL1 2400 pair. Independent verify **OK**
on fp1, fp2, then fp0 after the final DFC. DRAM probes 0 errors.

| fp | eye time | CS0 WL | CS1 WL | RX A/B | TX MR14 |
|---|---|---|---|---|---|
| 1 | 1.34 s | 19 20 19 20 | 19 20 19 20 | 3 / 3 | 21 |
| 2 | 1.41 s | 20 22 19 22 | 20 22 19 22 | 6 / 5 | 22 |
| 0 | 1.37 s | 19 21 19 21 | 19 21 19 21 | 4 / 5 | 29 |

## Conclusion

- **Confirmed — 1200 independent eye on this Micron 16 GiB**, parked at fp0.
