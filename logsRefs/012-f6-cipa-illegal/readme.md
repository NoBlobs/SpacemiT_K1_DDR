<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# 012 — F6.2 T-Head `dcache.cipa` is illegal in ddr_fw

- **Date:** 2026-11-19
- **Goal:** F6.2 enable D-cache (MHCR) and flush each probe with T-Head
  `dcache.cipa` (Zicbom already known to trap in the SPL).
- **Firmware:** `FSBL-eye-f6.bin`.
- **Result:** PI bring-up OK, then:

```
indep: dcache MHCR 0x60000010f
indep: eye training at fp0, 2 rank(s)
Unhandled exception: Illegal instruction
EPC: ... TVAL: 0000000002b5000b
```

`0x02b5000b` is `dcache.cipa a0`. THEADISAEE (mxstatus bit 22) was set.
F6.2 cache flush is **not available** in this slot. Two-stage short
patterns with D-cache left off remain (F6.3).
