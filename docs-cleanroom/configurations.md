<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 Thalles Campagnani -->

[← Previous: The Complete `ddr_init` Sequence](boot-sequence.md) | [Index](README.md#index) | [Next: Acceptance Criteria and Reference Measurements →](reference-measurements.md)

# Configurations: Byte Mode, Rank Count, Capacity, Rates, LPDDR4

Only one configuration was measured on hardware: **LPDDR4X, 2 ranks, x8
byte-mode dies, 8 GiB, 2400 and 2666 MT/s** (BPI-F3). Everything else in this
file is derived from command formats, encodings and JEDEC, and is marked as
such.

## x8 byte mode (measured)

`MR8[7:6] != 0` ⇒ x8 dies. After the first fp0 pass, out of SR:

1. **MC reset**: `pmu+0xb0` bit 21 low ≈100 µs, high ≈100 µs. The PLLs stay
   locked; do not repeat the PLL sequence. The PHY returns to its post-reset
   state (`PHY_INIT` reads `0x100`, taps `0xf`).
2. **Device_Init again**, with the x8 overlays written in every bank **after**
   its timings and **before** fp0 is selected in `CTL_FREQ`.

Overlays (select the bank with `CTL_FREQ`, then write both words):

| fp | `CTL_FREQ` | `ddr+0x13e4` | `ddr+0x3b4` |
|---|---|---|---|
| 3 | `0xF0800400` | `0x19000A02` | `0x08001400` |
| 2 | `0xA0800400` | `0x15000802` | `0x08001000` |
| 1 | `0x50800400` | `0x0c000402` | `0x08000A00` |
| 0 | `0x00800400` | `0x0c000402` | `0x08000A00` |

The x16 values these replace are in the timing table of
[device-init.md](device-init.md) (fp2: `0x13000802` / `0x08000e00`, …).

Effect (measured): the second fp0 pass gate drops from ≈105–118 to ≈32–40 taps.

## x16 dies

`MR8[7:6] == 0`: **no** MC reset, **no** overlays, **no** second Device_Init.
The second fp0 pass still exists (after the map adjustment and the DFC table).
*Not measured on hardware.*

## One rank (typical 4 GiB SKUs) — not measured

- Self-refresh: `mode = 1`, mask `0x04` (bit 2 only); commands
  `0x11000040` / `0x11000080`.
- `cs_sel = 1`: USER_0 init `0x11000001`, MRW `0x11020000 | mr`, ZQCAL only for
  CS0, MR14 write `0x1102000e`.
- PI commands for `cs = 1` only.
- DFC flows: the `0x13` rank byte of USER_0 / CA_CMD / PI entries becomes
  `0x11` (*inferred*).
- Map: a 4 GiB single rank is typically x16 16 Gb dies × 2 channels →
  `area_mb = 4096`, code `0x10`, ROW_17. MR8 expected `[7:6] = 0`,
  `[5:2] = 6`.
- CS1 is still written (start = `area_mb`, same code) or clears its
  present bit ([address-map.md](address-map.md)).
- No x8 reset, no fp3.

## 16 GiB (2 ranks × 8 GiB, x8 16 Gb dies) — not measured

- Size code `0x11` (8 GiB per rank); geometry ROW_18, COL_10, BANK_8.
- After all training passes: `CA_CMD = 0x13020095` (MR21).
- MR21 is also inserted in both DFC flows (entry flow after index `0x18`,
  shifting the rest; exit flow after `0x18b`) ([dfc-engine.md](dfc-engine.md)).

## 2666 MT/s (measured)

Only the PLL1 VCO changes (`0x203c5555` / `0x50dd6538`,
[clocks-and-pmu.md](clocks-and-pmu.md)). **Nothing** changes in the MC
banks, IO table, RL/WL or DFC table; fp2 is the same slice and bank. fp0 then
runs at ≈1333 MT/s. Training margins at fp2 are 8–9 instead of 10 (11 % shorter
UI). 2666 is the SoC ceiling.

## Rates above 2666 MT/s — not supported

No PLL1 setting, no MC bank, no RL/WL or MR2 value and no DFC flow exists for
2700–3200 MT/s. The DRAM's speed bin (4266 MT/s for the reference part) does not
lift the K1 PHY limit. Any other `data_rate` is treated as 1200.

## 2133 MT/s

LPDDR4 only (fp3 = PLL2/1, clksel `0x3b00`). On LPDDR4X there is no fp3: the
request falls back to 1200 (measured: the final DFC from fp2 back to fp0
completes).

## LPDDR4 (non-X) — derived, not measured

Needs a **separate image**: the ABI does not pass the memory type
and nothing in the silicon reveals it ([boot-interface.md](boot-interface.md)).

Differences in Device_Init:

| Item | LPDDR4X | LPDDR4 |
|---|---|---|
| CA Vref (`ddr+0x30c[31:24]`) | `0x9D` | `0xB2` |
| Tx Vref seed (`ddr+0x30c[23:16]`) | `0x19` | `0xA7` |
| SoC ODT (`ddr+0x30c[15:12]`) | 60 Ω (4) | 80 Ω (3) |
| DRAM Tx ODT (`+0x310/314[22:20]`) | 80 Ω (3) | 120 Ω (2) |
| DRAM Rx drive (`+0x310/314[19:16]`) | 60 Ω (4) | 60 Ω (4) |
| PHY Tx drive p/n (`COMMON+0xc[15:8]`) | 40/40 Ω (`0xD8`) | 48/60 Ω (`0xB0`) |
| PHY Rx ODT | 60 Ω | 120 Ω |
| PHY Rx Vref (`COMMON+0x4`) | `0x55` | `0x33` |
| VOH (`ddr+0x2c8[17]`) | 0.6 V | 0.6 V |
| PHY supply (`pmu+0x398`) | `[11:10] = 3` | `[11:10] = 2`, `[13:12] = 3` (LDO ≈0.88 V) |
| WDQS (`ddr+0x400`, per bank) | not written | fp3 `0x80001e06`, fp2 `0x80001b04`, fp1 `0x80001500`, fp0 `0x80001500` |
| Trained points | fp0, fp1, fp2 | fp0, fp1, **fp3**, fp2 |

WDQS encoding (*inferred*): `bit 31 | (n << 8) | m`, e.g. fp3
`0x80000000 | (0x1E << 8) | 6`.

Orchestration:

```
PLL, SW-FC fp0
Device_Init(fp0)
train(pp0)                 cold, read MR8
[MC reset + Device_Init if x8]
adjust map
fill DFC table
train(pp0)
DFC(1); train(pp1)         1066
DFC(3); train(pp3)         2133 — after pp1, before pp2
DFC(2); train(pp2)
DFC(final)
```

Training differences:

- MR14 on LPDDR4 uses the JEDEC range bit (bit 6) and a different absolute
  scale; the 4X sweep 21..36 must **not** be copied. The correct LPDDR4 sweep is
  unknown.
- CA training may be needed ([ca-training.md](ca-training.md)).
- Acceptance criteria are not established (no board). `PHY_STATUS = 0`, gate
  PASS and a usable DRAM still apply.

## Tx ODT on other LPDDR4X SKUs

Default 80 Ω for LPDDR4X, 120 Ω for LPDDR4. An EEPROM may specify another value
for another 4X SKU, but the 4-argument ABI does not pass it: an image for the
mainline slot can only tie the Tx ODT 1:1 to the type it is built for.

## Hynix x8, 2-rank LPDDR4X SKU — derived, not measured

In addition to the x8 overlays, this SKU enables CA ODT 60 Ω:
`COMMON+0x8[15:8] = 0xD8`, CA ODT config 2 in `ddr+0x310/314[30:28]`, bit 23 of
the same words, and CA Vref `0x19`. The reference board is Samsung: **do not**
apply this there.

## Gaps

These are the gaps with the highest practical impact: each one blocks a class
of boards.

| Gap | Blocks | How it could be closed |
|---|---|---|
| LPDDR4 (non-X): correct MR14 sweep (range bit 6, absolute values), acceptance ranges, whether CA training is needed | Any LPDDR4 board | Bring-up on an LPDDR4 K1 board through FDL, using the IO table and the fp3 ordering above |
| One rank / x16 dies (4 GiB): SR mask `0x04`, map, DFC flows with rank byte `0x11` | 4 GiB SKUs | Repeat the measurements of [reference-measurements.md](reference-measurements.md) on a one-rank board |
| 16 GiB (x8 16 Gb dies): size code `0x11`, ROW_18, MR21 (`0x13020095`) and its insertion in both DFC flows; meaning of bit 7 in the MR21 command | 16 GiB SKUs | Measurements on a 16 GiB board |
| DRAM vendors other than Samsung, including the Hynix x8 CA-ODT variant | Other SKUs | Measurements on such boards |
| A SKU that needs a different Tx ODT or the Hynix CA ODT cannot be identified through the current ABI | One binary for all boards | Needs an SPL change or a dedicated binary per SKU ([boot-interface.md](boot-interface.md)) |

---

[← Previous: The Complete `ddr_init` Sequence](boot-sequence.md) | [Index](README.md#index) | [Next: Acceptance Criteria and Reference Measurements →](reference-measurements.md)
