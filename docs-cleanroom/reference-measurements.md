<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 Thalles Campagnani -->

[← Previous: Configurations](configurations.md) | [Index](README.md#index) | [Next: Test Platform →](test-platform.md)

# Acceptance Criteria and Reference Measurements

Board: Banana Pi BPI-F3, 8 GiB, LPDDR4X Samsung K4UCE3Q4AA-MGCL, 2 ranks, x8
dies. Every value below is what the **hardware** of this board produces when it
is programmed as described in [boot-sequence.md](boot-sequence.md) and the
files it references. The values serve as acceptance ranges; they are not the
algorithm. Do not hard-code them.

## Acceptance criteria

The hardware has reached the expected state on this board when:

- `PHY_STATUS == 0` after every training pass (four passes for LPDDR4X).
- Gate: right after RGATE, `(PI_STATUS & 6) == 6` **if** bit 2 was clear in
  the first read after the command (otherwise `PHY_STATUS` bit 4 == 0), and
  the code is `0x0001ggtg` with equal bytes. At the end of the pass both bytes
  are in range (equality not required).
- After the second fp0 pass the gate is ≈32–40 taps (not the ≈105–118 of the
  first pass). 32 (`0x20`) is within range.
- At 2400 MT/s the first fp2 pass gives Rx index 4–6 with minimum margin ≥10
  and Tx MR14 `0x1d`–`0x1f` (29–31) with minimum margin ≥10.
  Margins of 8/9 there mean the clock is actually 2666 (check the PLL1 recipe,
  [clocks-and-pmu.md](clocks-and-pmu.md)); at 2666 MT/s 8/9 is the expected result.
- MR14 read after the SR exit equals `CTL_TXVREF[21:16]` on all four
  (rank, channel) pairs.
- TX DQ0 ≈30–36 taps and RX DQ0 ≈17–19 taps at the final fp.
- WL in `+0x24` distinct per byte and per rank, ≈17–23.
- The capacity derived from MR8 is 8192 MB.
- The whole DRAM holds data without errors or aliasing, and code can be loaded
  into it and executed.

## Per-pass results at 2400 MT/s

PLL1 programmed with the 2400 read-modify-write recipe; 9 runs of the full
sequence. Gate in taps; index / MR14 with the minimum margin in parentheses;
MR14 in decimal.

| Pass | fp | Gate | Rx index (min) | Tx MR14 (min) | PHY_STATUS |
|---|---|---|---|---|---|
| fp0, first | 0 | 111–118 | 5–6 (24) | 26–36 (20–21) | 0 |
| fp0, second | 0 | 32–38 | 5–6 (24) | 26–36 (20–21) | 0 |
| fp1 | 1 | 32–38 | 3–4 (26) | 31–35 (24) | 0 |
| fp2 | 2 | 34–40 | 5–6 (10) | 29–31 (10) | 0 |

In all 9 runs:

- end of fp2: WL 19–23 distinct per byte and rank, Rx DQ0 18–19, Tx DQ0 32–36;
- end state STATUS `0x33`, `CTL_FREQ = 0xa0800400`, capacity 8192 MB;
- MR14 `0x1e`–`0x1f` on all four pairs, always equal to `CTL_TXVREF[21:16]`;
- DFC to fp1 and fp2 in 29–30 µs, final DFC to fp2 in 25–26 µs.

A wider envelope at 2400 MT/s (other measurement sets on the same board):

| Pass | Gate | Rx index / margin | Tx MR14 | PHY_STATUS |
|---|---|---|---|---|
| fp0, first | ≈105–118 | 4–7 / 24 | ≈`0x19`–`0x1c` | 0 |
| fp0, second | ≈32–40 | 4–5 / 24 | ≈`0x1a`–`0x1c` | 0 |
| fp1 | ≈33–38 | 3–5 / 26 | ≈`0x1f` | 0 |
| fp2 | ≈35–40 | 4–6 / 10 | ≈`0x1d`–`0x1f` | 0 |

### Repeated training passes at fp2, 2400 MT/s (6 runs, 2 ranks)

A training pass ([training-pass.md](training-pass.md)) run again on fp2 of a controller
that is already initialised and trained:

- Rx winner always index 6; per-step minimums
  `7 7 8 8–9 9 9 10 10 9 9 8 7 6–7 0 0 0`.
- Tx plateau at margin 10 from index 8–10 on (MR14 29–31).
- WL CS0 `19 22 20 23`, CS1 `19 21 20 23`.
- Rx DQ0 18–19, Tx DQ0 32–36.
- Each PI operation ≤17 µs.
- DRAM contents survive the pass.

Register state after such a pass:

| Register | Value |
|---|---|
| `CTL_FREQ` | `0xa0800400` |
| `PI_CFG` | `0x8` |
| CS0/CS1 map | `0x00100001` |
| `PI_STATUS` | `0x80008067` |
| VREF0 / VREF1 | `0x6666a937` / `0x6666a927` |
| `CTL_TXVREF` | `0x9d1f4314` (MR14 field `0x1f`, CA Vref `0x9d`) |
| `PHY_STATUS` | 0 |
| MR5/6/8/12/14 | `0x01 / 0x08 / 0x52 / 0x5d / 0x1f` |

### Tap snapshots at 2400 MT/s

- TX DQ0 per byte after write training, CS0: `33 35 33 36`.
- RX DQ0 per byte after read training, CS0: `18 18 18 17`.
- WL: CS0 `20 22 20 23`, CS1 `19 22 20 23`.

## Results at 2666 MT/s

PLL1 `+0x39c = 0x203c5555`, `+0x3a0 = 0x50dd6538`; `PHY_STATUS` 0 in all four
passes; DRAM usable.

| Pass | Rate | Rx best (margin) | Tx best (margin) | MR14 | UI |
|---|---|---|---|---|---|
| fp0, first | ≈1333 | idx 4 (21) | 29 (17) | `0x1d` | 750 ps |
| fp0, second | ≈1333 | idx 4 (21) | 28 (17) | `0x1c` | 750 ps |
| fp1 | 1066 | idx 5 (27) | 33 (24) | `0x21` | 938 ps |
| fp2 | 2666 | idx 6 (9) | 32 (9) | `0x20` | 375 ps |

Over five runs of the full sequence at 2666: fp0 Rx 4–7 / 21–22,
Tx 26–28 / 17; fp1 Rx 3 / 26, Tx 31–34 / 24; fp2 Rx 5–7 / 8–9,
Tx MR14 23–35 / 8–9. In all of them `PHY_STATUS` was 0, the capacity 8192 MB
and the DRAM pattern tests error-free.

Compared with 2400: RX 9 vs 10, TX 9 vs 10 at fp2. Rx indices 13–15 still have
margin 0. TX/RX taps at fp2 sit in the middle of the UI (TX 32–36, RX 17–18) —
the same tap counts as at 2400.

WL (b0 b1 b2 b3) at 2666: CS0 `19 21 19 23`, CS1 `19 21 20 22`; at 1200:
CS0 `18 21 17 22`, CS1 `17 20 17 21`.

In one of two 2666 runs, RX DQ0 of CS1 byte 1 settled at 20 taps, just above
the 17–19 range; this was not observed at 2400.

## fp0 as the final point

This is what a 2133 MT/s request maps to on LPDDR4X ([boot-interface.md](boot-interface.md)).
After fp0/fp1/fp2 are trained, the final DFC from fp2 back to fp0 completes in
29–30 µs and leaves `CTL_FREQ = 0x00800400`, clksel `0x3b50`. Latency confirms
1200 MT/s (seq / rand 32 MiB 333.6–334.1 / 374.0–374.4 ns, next to the
332.2 / 373.0 ns measured when fp0 is selected directly). DRAM pattern tests are
error-free.

## Register milestones (2400 MT/s, 2 ranks)

| Point | Values |
|---|---|
| Entry to `ddr_init` | `pmu+0x04 = 0x00019249`, `+0xb0 = 0xa0610c00`, `+0x3b4 = 0x00073340`, `+0x39c = 0x2a200000`, `+0x3a0 = 0x50dd6432`, `+0xe8 = 0x40001000` |
| After PLL + SW-FC | `+0x3b4 = 0x00033b50`, `+0xb0 = 0x40600400`, `+0xe8 = 0x40001003` |
| After Device_Init | STATUS `0x33`, `CTL_FREQ = 0x00800400`, `PI_CFG = 0`, `PHY_INIT = 0x100`, `PI_STATUS = 0x80008000`, COMMON+0xc `0x36e4d800` |
| After MR8 read | MR5/MR8 `0x01 / 0x52` → x8, 8192 MB, ROW_17, map `0x00100001`, geometry `0x05030732` |
| After DFC(1) / DFC(2) | STATUS `0x33`; `CTL_FREQ` `0x50800400` / `0xa0800400`; `PI_CFG` 4 / 8; clksel `0x00033b04` / `0x00033b40` |
| End of sequence | STATUS `0x33`, `CTL_FREQ = 0xa0800400`, `PI_CFG = 8`, clksel `0x00033b40` |

No PI operation took more than 34 µs; no gate timeout and no stale first read
of `PI_STATUS` in any operation.

## DRAM latency (frequency meter)

Pointer chase, 2²⁰ dependent loads, 64 B nodes, `mtime` at 24 MHz
([test-platform.md](test-platform.md)).

Data cache **off**, CPU at 307 MHz (in this state the cache does not filter:
16 KiB costs the same as 32 MiB). ns per load:

| Rate | fp | `pmu+0x39c` | Seq 32 MiB | Rand 32 MiB |
|---|---|---|---|---|
| 2400 | 2 | `0x2a203b00` | 266.10–266.13 | 291.04–291.39 |
| 2666 | 2 | `0x203c5555` | 250.00–250.01 | 277.39–277.40 |
| 1200 | 0 | `0x2a203b00` | 332.17–332.20 | 373.01–373.05 |
| 1066 | 1 | `0x2a203b00` | 361.97 | 404.09–404.14 |

fp2 drops 6.0 % (seq) and 4.8 % (rand) from 2400 to 2666. With the full word
`0x203b5555` the "2400" line is identical to the 2666 one, and fp0 measures as
≈1333 (310.7 / 350.0 ns).

Data cache **on**, CPU at 1599 MHz, random 32 MiB: see the table in
[clocks-and-pmu.md](clocks-and-pmu.md) (fp0 299.65 / 278.66 ns, fp1
331.31 / 331.35 ns, fp2 213.35 / 198.94 ns for 2400 / 2666).

## DRAM integrity

With the full sequence completed at 2400 MT/s:

- pattern tests: 0 errors over 64 MiB at `0x10000000` and at `0x200000000`, and
  over 4 MiB at `0x100000000` and at `0x27f000000`;
- address markers written at `0x20000000`, `0x100000000`, `0x180000000`,
  `0x200000000` and `0x27f800000` each read back their own value (no aliasing
  between the low window and the high window).

## Hardware behaviour in failure conditions

| Condition | What the hardware shows |
|---|---|
| PLL enable bits left cleared | CLKSEL reads `0x00003040`; the lock bits never assert |
| RGATE done bit awaited without issuing the command | Bit 1 of `PI_STATUS` never sets; the register stays `0x80008001` |
| HW-DFC kicked without a DFC table | `pmu+0x04` stays `0x00400000` (bit 22 stuck), STATUS `0x77`, `CTL_FREQ = 0x00800400` (unchanged) |

## Gaps

- No reference data exists for LPDDR4, x16, one rank or 16 GiB.
- Margins are only compared between runs; their absolute unit is unknown.
- No temperature or voltage variation was characterised.

---

[← Previous: Configurations](configurations.md) | [Index](README.md#index) | [Next: Test Platform →](test-platform.md)
