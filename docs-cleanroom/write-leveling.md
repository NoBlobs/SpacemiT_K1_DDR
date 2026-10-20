<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 Thalles Campagnani -->

[← Previous: One Training Pass](training-pass.md) | [Index](README.md#index) | [Next: DQS Read Gate →](read-gate.md)

# Write Leveling

## Goal

Align DQS with CK at the DRAM, for each rank and each byte. The software does
not compute the delay: it asks the PI, and the result lands in `byte + 0x24` of
the active slice ([phy.md](phy.md)).

## Recipe

```
PI_WL_CFG (ddr+0x13e8) = 0x0a641e44        /* once, before the first rank */
for cs in 1 .. cs_num:
    PI_CMD (ddr+0x13d0) = 0x10400000 | (cs << 24)
    wait (PI_STATUS & 0x1) == 0x1          /* software timeout */
```

- The issue of the command clears bit 0 of `PI_STATUS` (measured: leftover
  `0x80008067` → `0x80008066` right after the command), so the wait is real
  even when the previous pass left bit 0 set.
- There is no pass/fail code of its own. A failure shows up as bit 1 of
  `PHY_STATUS` (`phy+0x18000`) at the end of the pass.

## Parameters

`PI_WL_CFG` resets to `0x0a301e44` (p2 = 48). Training needs `0x0a641e44`
(p2 = 100) **before** the first rank. The fields p0..p4 = 0, 10, 100, 30, 68
are sequencer parameters; their meaning is *opaque*
([pi-training-engine.md](pi-training-engine.md)).

## Effects on the PHY

- Only the `+0x24` word of each byte/rank block changes.
- TX DQ (`+0x00`) and RX DQ (`+0x30`) taps do **not** change in this stage;
  they stay `0xf` until the eye training. Do not write them.
- Each rank has its own DQS path: skipping CS1 leaves the second rank with its
  reset WL value.

## Typical results (reference board)

WL taps, bytes b0 b1 b2 b3:

| Rate | CS0 | CS1 |
|---|---|---|
| 1200 MT/s | 18 21 17 22 | 17 20 17 21 |
| 1066 MT/s | 18 20 17 22 | — |
| 2400 MT/s | 19–20 22 20 23 | 19 21–22 20 23 |
| 2666 MT/s | 19 21 19 23 | 19 21 20 22 |
| ≈1333 MT/s (fp0 with the 2666 VCO), cold first pass | 17 20 16 21 | — |

Range 17–23 taps, different per byte and per rank. Acceptance: distinct per
byte and rank, inside ≈17–23 at the final frequency point.

## Gaps

- Meaning of the WL CFG fields.
- Unit and range of the WL tap (only its typical values are known).
- WL behaviour on x16 dies and on other DRAM vendors is not measured.

---

[← Previous: One Training Pass](training-pass.md) | [Index](README.md#index) | [Next: DQS Read Gate →](read-gate.md)
