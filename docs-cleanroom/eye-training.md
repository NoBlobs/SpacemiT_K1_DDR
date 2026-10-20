<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 Thalles Campagnani -->

[← Previous: DQS Read Gate](read-gate.md) | [Index](README.md#index) | [Next: CA Training →](ca-training.md)

# Eye Training: Read and Write Vref Sweeps

Both eye-training stages follow the same pattern:

1. for each of 16 Vref steps, set the Vref and run the PI operation on every
   rank;
2. after each operation, copy the per-DQ margins from the harvest and update
   the minimum of that step;
3. choose the step with the **max-min** rule;
4. set the winning Vref and **run the PI operation again** so the PHY ends up
   with the taps of the winning step.

The difference is which Vref is swept:

| Stage | Vref swept | Range | PI operation | Done mask | Harvest |
|---|---|---|---|---|---|
| Read (Rx) | PHY Rx Vref, COMMON VREF0/VREF1 | index 0..15 | RD `0x10200000` | `0x6` | `phy+0x18200` |
| Write (Tx) | DRAM Vref(DQ), MR14 | 21..36 | WR `0x10080000` | `0x60` | `phy+0x18300` |

## Read training (Rx Vref + RX DQ delays)

With a fixed Vref, the PI leaves:

- margins in harvest RX (0 = eye closed on that DQ);
- RX DQ taps in `byte + 0x30 + 4*lane` (mask 31).

```
VREF0 = slice + 0x3004 ; VREF1 = slice + 0x3204

VREF0 &= ~0x10
PI_RD_CFG (ddr+0x13ec) = 0x00005070
for step in 0 .. 15:
    v = (VREF0 & 0xffff) | PACK_VREF(step)
    VREF0 = v ; VREF1 = v
    min[step] = +inf
    for cs in 1 .. cs_num:
        PI_CMD = 0x10200000 | (cs << 24)
        wait (PI_STATUS & 0x6) == 0x6
        copy harvest RX now            /* the next rank overwrites it */
        min[step] = min(min[step], all 32 DQ margins)
PHY_STATUS &= ~(1 << 2)                /* clear read-leveling error before choosing */
best = max_min(min)
VREF0 = VREF1 = (VREF0 & 0xffff) | PACK_VREF(best)
for cs in 1 .. cs_num:
    RD operation                       /* programs the PHY at the winning Vref */
VREF0 |= 0x10
```

`PACK_VREF(s) = (s<<28) | (s<<24) | (s<<20) | (s<<16)`; for example
`PACK_VREF(5) = 0x55550000`, which is also the init value. The low 16 bits are
preserved by the read-modify-write.

Notes:

- **16 steps** are needed: at 2400/2666 MT/s, indices 13–15 close the eye
  (margin 0 on every DQ), and the "typical" 0.3·VDDQ (index 5) is not the best
  on every board.
- **VREF0 and VREF1** are the two sub-PHYs of the same slice. Writing
  `slice + 0x1000 + 0x3004` would land in the next slice.
- **Bit 4** is cleared only during the sweep: observed `0xa937` → `0xa927`
  → at the end `0xa937` with `[31:16]` = the winner (VREF1 keeps bit 4 clear).
- **Clear `PHY_STATUS` bit 2 before choosing**: the sweep can set the
  read-leveling error bit (seen at 2666) at bad Vref steps even though the
  re-kick at the good Vref is valid. Do not clear the other error bits here.
- **Re-kick**: the last loop step leaves the taps of step 15 in the PHY; the
  final RD operations restore the winner's taps.
- The worst DQ is **not** a fixed pin (on this board it moves among DQ 0, 1, 16,
  17, 19, 20), which is why a per-step minimum over all DQs is used rather than
  a static mask.
- Read training does not touch MR12 / Vref(CA).
- Printing all 16 margin matrices during boot only lengthens the init.

## Write training (Tx Vref = MR14 + TX DQ/DM delays)

Same structure, but the Vref is the **DRAM's** Vref(DQ) (MR14), not a PHY
index. The PI centres TX DQ and DM.

```
seed[i] = 21 + i                       /* 21..36, fits in 6 bits */
PI_WR_CFG (ddr+0x13f8) = 0x00001810
for i in 0 .. 15:
    CTL_TXVREF (ddr+0x30c) = (CTL_TXVREF & 0xffc0ffff) | (seed[i] << 16)
    CA_CMD (ddr+0x24) = 0x1302000e     /* MRW MR14 while in SR (0x1102000e with 1 rank) */
    min[i] = +inf
    for cs in 1 .. cs_num:
        PI_CMD = 0x10080000 | (cs << 24)
        wait (PI_STATUS & 0x60) == 0x60
        copy harvest TX now
        min[i] = min(min[i], all 32 DQ margins)
best = seed[max_min(min)]
CTL_TXVREF[21:16] = best
CA_CMD = 0x1302000e
for cs in 1 .. cs_num:
    WR operation
```

Notes:

- The MR14 command is required at **every** step: the eye being measured is the
  die's. Changing only `CTL_TXVREF` measures the old Vref.
- **Why 21..36:** a 16-point window of the LPDDR4X Vref(DQ) range starting at
  21; the post-init MR14 on this board is `0x19` (25), near the middle. The
  starting MR14 value does not enter the max-min; the sweep in SR writes every
  value itself, so no MR14 seed is needed after a DFC.
- After leaving SR, MR14 read from every (rank, channel) must equal
  `CTL_TXVREF[21:16]` (after a dummy MRR if a DFC happened,
  [mode-registers.md](mode-registers.md)).
- Typical result at 2400 MT/s: the margin-10 plateau starts at index 8–10
  (MR14 29–31) and the first maximum is chosen. At 2666 the eye is 8–9 and the
  first maximum often falls around 23–25.
- Sanity: TX taps ≈30–36 and RX taps ≈17–19 at the final fp on this board. A
  TX tap still `0xf` means the operation did not run. Do not write these
  numbers by hand.
- LPDDR4 (non-X) uses a different MR14 range (range bit 6); 21..36 must not
  be reused there ([configurations.md](configurations.md)).

## Max-min Vref selection

The same rule serves both sweeps (Rx index 0..15, Tx seed 21 + i):

```
for step in 0 .. 15:
    min[step] = min(margin[step][cs][dq] for every cs, every one of the 32 DQ)
best = 0
for step in 1 .. 15:
    if min[step] > min[best]:          /* strict '>' */
        best = step
```

Rules:

- No interpolation between steps.
- DM margins are not weighted: they may be stored, but do not enter the
  minimum.
- No averages; no "best DQ"; not only CS0.
- Copy the harvest **per rank**, not once per step (it latches only the last
  operation).

Why max-min: one bad DQ decides. An average hides a pin at 0 while the others
are at 20, and the worst DQ moves among pins on this board.

Why `>` and not `>=`: on a plateau (several steps with the same minimum) the
**first** step is kept. Using `>=` picks the last one and moves MR14/Vref with no
measured gain.

All minimums 0 (eye closed across the whole band): the strict rule returns
index 0. For the write sweep prefer index 4 (MR14 = 25), the centre of 21..36
and the post-init value, over 21. Such a pass is a failure anyway; check
`PHY_STATUS`.

## Typical results (reference board)

| Pass | Rate | Rx index (min margin) | Tx MR14 (min margin) |
|---|---|---|---|
| fp0, first pass | 1200 | 5–6 (24) | 26–36 (20–21) |
| fp0, second pass | 1200 | 5–6 (24) | 26–36 (20–21) |
| fp1 | 1066 | 3–4 (26) | 31–35 (24) |
| fp2 | 2400 | 5–6 (10) | 29–31 (10) |
| fp2 | 2666 | 5–7 (8–9) | 23–35 (8–9) |

Rx per-step minimums at fp2 / 2400 MT/s across six repeated passes:
`7 7 8 8–9 9 9 10 10 9 9 8 7 6–7 0 0 0` (winner always index 6).
More data in [reference-measurements.md](reference-measurements.md).

## Gaps

- Unit of the harvest margins (taps or something else).
- Meaning of VREF bit 4 and of the low 16 bits of VREF0/VREF1.
- Whether the four nibbles of `PACK_VREF` are independent controls (e.g. DQ
  vs DQS, per byte) that could be swept separately; the sequence always writes
  the same index in all four.
- Absolute voltage of each Rx index and of each MR14 value on this board.
- LPDDR4 write-sweep range.

---

[← Previous: DQS Read Gate](read-gate.md) | [Index](README.md#index) | [Next: CA Training →](ca-training.md)
