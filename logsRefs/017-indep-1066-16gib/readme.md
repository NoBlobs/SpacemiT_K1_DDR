<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# 017 — Independent eye at 1066 MT/s (16 GiB Micron)

- **Date:** 2026-11-20
- **Goal:** same `K1_INDEP_EYE` blob, SPL `RATE=1066`. Final fp is 1; boot
  walks fp0/fp1/fp2, then DFC back to fp1.
- **Firmware:** `FSBL-gpl-full-1066.bin`. Hashes in `ddr_fw.sha256` /
  `fsbl.sha256`.
- **Configuration:** FDL `usb:1-1`, UART `/dev/ttyUSB1`, `MR8 0x58`, 16384 MB.

## Result

`k1-ddr: rc 0` in **3.34 s**, `fp1`, PLL1 2400 pair. Independent verify **OK**
on fp0, fp2, then fp1 after the final DFC. DRAM probes 0 errors.

| fp | eye time | CS0 WL | CS1 WL | RX A/B | TX MR14 |
|---|---|---|---|---|---|
| 0 | 1.35 s | 19 21 18 21 | 19 21 18 21 | 6 / 4 | 28 |
| 2 | 1.41 s | 20 24 19 22 | 20 23 19 23 | 6 / 5 | 23 |
| 1 | 1.34 s | 19 20 19 20 | 19 20 19 20 | 4 / 3 | 21 |

## Conclusion

- **Confirmed — 1066 independent eye on this Micron 16 GiB**, parked at fp1.
