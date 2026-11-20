<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# 013 — F6 two-stage short probes (D-cache off)

- **Date:** 2026-11-19
- **Goal:** F6.3 after F6.2 CMO trap (`012`): binary edges on 64–256 B,
  confirm with 4 KiB, D-cache left off.
- **Firmware:** `FSBL-eye-f6b.bin`. Hashes in `ddr_fw.sha256` / `fsbl.sha256`.
- **Configuration:** FDL `usb:1-1`, UART `/dev/ttyUSB1`, `MR8 0x58`, 16384 MB.

## Result

Both ranks, all fps, **verify OK**. `k1-ddr: rc 0` in **3.16 s** (was ~64 s).

| fp | eye time | CS0 WL | CS1 WL | RX A/B | TX MR14 |
|---|---|---|---|---|---|
| 0 | 1.27 s | 19 21 18 21 | 19 21 18 21 | 4 / 6 | 28 |
| 1 | 1.24 s | 19 20 18 20 | 19 20 19 20 | 4 / 3 | 21 |
| 2 | 1.31 s | 20 23 19 22 | 20 23 19 23 | 6 / 5 | 23 |

## Conclusion

- **Confirmed — two-stage short patterns hit the 1–3 s budget** without CMO.
- F6.2 cache flush remains unavailable in this slot (`012`).
