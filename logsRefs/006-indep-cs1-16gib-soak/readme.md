<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# 006 — CS1 independent eye soak (same FSBL as 005)

- **Date:** 2026-11-19
- **Goal:** Second FDL of `FSBL-eye-cs1.bin` (F5 image) on the 16 GiB board.
  F6 firmware was not ready; use the FDL window as a soak.
- **Firmware:** same hashes as `logsRefs/005-indep-cs1-16gib`.
- **Configuration:** FDL `usb:1-1`, UART `/dev/ttyUSB1`, `MR8 0x58`, 16384 MB.

## Procedure

Same as 005 (`--timeout 240 --reset h --done 'k1-ddr: rc'`), log
`/tmp/k1-16g-eye-cs1-soak.log`.

## Result

Both ranks, all fps, **verify OK**. `k1-ddr: rc 0` in **63.5 s**.

| fp | CS0 WL (width) | CS1 WL (width) | RX Vref A/B | TX MR14 | time |
|---|---|---|---|---|---|
| 0 | 19 21 18 21 (24 21 23 21) | 19 21 18 21 (24 21 23 21) | 4 / 4 | 21 | 29.2 s |
| 1 | 19 20 19 20 (26 23 26 23) | 19 20 19 20 (26 23 26 23) | 3 / 4 | 22 | 33.6 s |
| 2 | 20 22 19 22 (10 9 10 10) | 20 23 19 22 (10 9 10 10) | 6 / 6 | 27 | 24.0 s |

vs 005: fp2 CS0 b1 WL 24→22, CS1 b1 WL 24→23; TX MR14 at fp1 21→22. Pattern
verify still 0 errors.

## Conclusion

- **Confirmed — F5 image is repeatable** on a second cold FDL.
- Next: F6 (cache/flush, two-stage, production path).
