<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 Thalles Campagnani -->

[← Previous: Device_Init](device-init.md) | [Index](README.md#index) | [Next: Mode Registers →](mode-registers.md)

# PHY

Base: `phy = ddr + 0x40000`. The PHY holds:

- one 16 KiB **slice** per frequency point with the delay taps and the analog
  settings of that rate: `slice(fp) = phy + (fp << 14)`;
- a global **OTHER** block (`phy + 0x10000`) with `PHY_INIT`;
- a global **TRAINING** block (`phy + 0x18000`) with the training status and
  the margins ("harvest") latched by the PI.

Slice layout and byte-lane mapping are in [system-overview.md](system-overview.md)
and [mmio-map.md](mmio-map.md).

## Per-byte register block

Address of byte `b` (0..3) of rank `c` (0..1) in a slice:

```
off_b = { 0x0000, 0x1000, 0x0200, 0x1200 }[b]
base  = slice + off_b + (c << 8)
```

32 words from `base`:

| Offset | Field | Notes |
|---|---|---|
| `+0x00` … `+0x1c` | TX DQ delay, lanes 0..7 | 6-bit tap (mask 63). Reset `0xf`. Set by write training. |
| `+0x20` | TX DM delay | 6-bit tap. Reset `0xf`. Set by write training. |
| `+0x24` | WL | Write-leveling delay; the only word changed by write leveling. |
| `+0x28`, `+0x2c` | — | 0 |
| `+0x30` … `+0x4c` | RX DQ delay, lanes 0..7 | 5-bit tap (mask 31). Reset `0xf`. Set by read training. |
| `+0x50` | — | `0xf` |
| `+0x60` | — | `0xff` on even bytes (0 and 2), 0 on odd bytes. Not changed by the gate stage. |
| `+0x64` | — | `0x4349`, **only** on byte 0 of CS0 (written by the PHY init in every slice); 0 on the other seven byte/rank blocks. The gate stage modifies it. Not a delay. *Opaque.* |
| `+0x70` | GATE | DQS read-gate code, see below |

Delay words contain only the tap (upper bits 0). Write leveling does not touch
the TX DQ (`+0x00`) or RX DQ (`+0x30`) words: they stay `0xf` until the eye
training. A TX tap still `0xf` after write training means the PI operation did
not run.

### Gate code (`+0x70`)

```
GATE = (1 << 16) | (g << 8) | t
```

| State | Value |
|---|---|
| Before the PI ran | `0x00000a00` (low ≠ high byte; **not** a trained value) |
| Right after the RGATE operation | `0x0001ggtg`: bit 16 = 1 and the two low bytes **equal** (`t == g`) |
| After read training | bit 16 = 1; `t` may differ from `g` by up to 2 (e.g. `0x00012625`, `0x00012423`) |

- `g` (`[15:8]`): the RGATE result.
- `t` (`[7:0]`): a copy of `g` right after RGATE that read training may
  adjust.
- Acceptance right after RGATE: bit 16 set and `g == t`.
- Acceptance at the end of the pass: bit 16 set and both bytes inside the
  expected range ([reference-measurements.md](reference-measurements.md));
  equality is **not** required.

The gate value depends strongly on the initialisation state: ≈105–118 taps at
1200 MT/s on the cold first pass (≈104–110 at ≈1333), ≈32–40 taps after the x8
re-initialisation. The cold value is not a final eye position and must not be
used as a criterion.

### Typical trained taps (reference board)

| Field | Typical | Notes |
|---|---|---|
| TX DQ | 30–36 taps | ≈ half a UI if 1 UI = 64 taps (*inferred*) |
| RX DQ | 17–19 taps | ≈ 0.55 UI if 1 UI = 32 taps (*inferred*) |
| WL | 17–23 taps | Different per byte and per rank |

Do not force taps to "half a UI" in software: the PI centres them. Use the
numbers only as a sanity check.

### Tap scale (*inferred* from the masks and the centring)

| fp | Rate | UI | TX tap | RX tap |
|---|---|---|---|---|
| 0 | 1200 MT/s | 833 ps | ≈13 ps | ≈26 ps |
| 1 | 1066 MT/s | 938 ps | ≈14 ps | ≈29 ps |
| 2 | 2400 MT/s | 417 ps | ≈6 ps | ≈13 ps |
| 2 | 2666 MT/s | 375 ps | ≈6 ps | ≈12 ps |

With the 2666 VCO, fp0 runs at ≈1333 MT/s (UI ≈ 750 ps). At 2400 and 2666 the
trained taps are the same in tap units, which supports the "taps scale with the
UI" reading.

## CAPHY (`slice + 0x2000`)

`+0x00` … `+0x1c` = `0xf` (default CA delays) while CA training is off — the
LPDDR4X boot never changes them.

## COMMON: analog of the slice

`C0 = slice + 0x3000` (sub-PHY A), `C1 = slice + 0x3200` (sub-PHY B). By the
same `+0x200` stride as the byte blocks, A serves bytes 0 and 1 and B serves
bytes 2 and 3 (*inferred*). The training always writes both with the same
Rx Vref.

| Offset | Content |
|---|---|
| `+0x0` | Analog / impedance-calibration arm: written 0 then 1 by the init; reads 1 |
| `+0x4` | **VREF0** (in `C0`) / **VREF1** (in `C1`): Rx Vref in `[31:16]`, pre/postamble and other bits in `[15:0]` |
| `+0x8` | CA drive / CA ODT `[15:8]` (only for a Hynix x8 SKU, [configurations.md](configurations.md)) |
| `+0xc` | Tx drive `[15:8]`, Rx ODT `[21:16]`; reads `0x36e4d800` on the reference board |
| `+0x10` | bit 28 set by the init |
| `+0x14` | `[22:21] = 3`, bit 4 cleared by the init |

### Rx Vref word (`C0+4` = VREF0, `C1+4` = VREF1)

```
[31:16]  Rx Vref: four 4-bit fields
         after init:          0x5555 (0x55 0x55: 0.3·VDDQ, "DQS and DQ")
         during the sweep:    PACK_VREF(s) = (s<<28)|(s<<24)|(s<<20)|(s<<16), s = 0..15
[15:0]   0xa927 with bit 4 clear, 0xa937 with bit 4 set
```

The init value `0x5555` is the same as `PACK_VREF(5)`.

Mandatory read-modify-write recipe of the read-training sweep
([eye-training.md](eye-training.md)):

1. `VREF0 &= ~0x10`
2. at every step: `v = (VREF0 & 0xffff) | PACK_VREF(step)`, write `v` to
   VREF0 **and** VREF1
3. at the end: `VREF0 |= 0x10`

Hence the trained state reads, for example, `VREF0 = 0x6666a937` and
`VREF1 = 0x6666a927` (winning index 6; bit 4 set only in VREF0).

The function of bit 4 and the meaning of the low 16 bits are *unknown*; keep
the read-modify-write exactly as above.

## Analog initialisation (part of Device_Init)

Programmed in all four slices (`phy + n*0x4000`, n = 0..3) and in both
sub-PHYs. Pre: PLLs locked, SW-FC to fp0, MC clock on.

### PMU side (before the PHY registers)

```
pmu+0x3a4 = (pmu+0x3a4 & 0xFFFF00FF) | (0x0F << 8)
LPDDR4X:  pmu+0x398 |= (3 << 10)
LPDDR4:   pmu+0x398 |= (2 << 10)
          pmu+0x398 = (pmu+0x398 & 0xFFFFCFFF) | (3 << 12)      /* LDO ≈0.88 V */
```

### COMMON, for every slice and both `C0` / `C1`

1. `C+0 = 0`, then `C+0 = 1`. Arms the analog / PHY impedance calibration.
   There is no done bit: the DFI `init_start` that follows in Device_Init waits
   for it.
2. Pre/postamble in `C+4`:
   `x = (x & 0xFFFF0FFF) | (1 << 11) | (1 << 13) | (1 << 15)`.
   Do not touch other bits of `[15:0]`.
3. Tx drive, `C+0xc [15:8]`:
   `d2 = (tx_pdrv << 5) | (tx_ndrv << 2)`;
   `x = (x & 0xFFFF00FF) | (d2 << 8)`.
   LPDDR4X: pull-up 6 / pull-down 6 (40/40 Ω) → `0xD8`.
4. Rx ODT, `C+0xc [21:16]` (preserve `[23:22]`):
   `d3 = (rx_odt << 3) | rx_odt`;
   `x = (x & 0xFFC0FFFF) | (d3 << 16)`.
   LPDDR4X: 60 Ω (code 4) → `0x24`. Afterwards `COMMON+0xc` reads
   `0x36e4d800` on the reference board.
5. Rx Vref, `C+4 [31:16]`:
   `x = (x & 0x0000FFFF) | (rx_vref << 16) | (rx_vref << 24)`.
   LPDDR4X `0x55`, LPDDR4 `0x33`. The training sweep later replaces `[31:16]`.
6. `C+0x14`: `x = (x & 0xFF9FFFEF) | (3 << 21)`.
7. `C+0x10`: `x |= 0x10000000`.

Then, **once**: `phy + 0x3030 = 0x1077` (COMMON A of slice 0 only; do not
spread it to other slices or to sub-PHY B).

### DATAPHY

```
phy + 0x0064 = 0x4349
phy + 0x4064 = 0x4349
phy + 0x8064 = 0x4349
phy + 0xc064 = 0x4349
```

Byte 0 of CS0 only, in each slice. Do not write it to other bytes.

### OTHER

```
phy + 0x10024 = 0
phy + 0x10000 |= 1
```

### CA drive / CA ODT

Not configured on the reference board (Samsung): CAPHY stays at `0xf`. Only a
Hynix x8 2-rank LPDDR4X SKU enables CA ODT 60 Ω ([configurations.md](configurations.md)).

### Where each IO parameter lives

| Parameter | Location | LPDDR4X | LPDDR4 |
|---|---|---|---|
| CA Vref | `ddr+0x30c[31:24]` → MR12 | `0x9D` | `0xB2` |
| Tx (DRAM) Vref | `ddr+0x30c[23:16]` → MR14 seed | `0x19` | `0xA7` |
| SoC ODT | `ddr+0x30c[15:12]` | 4 (60 Ω) | 3 (80 Ω) |
| DRAM Tx ODT | `ddr+0x310/314[22:20]` | 3 (80 Ω) | 2 (120 Ω) |
| DRAM Rx drive | `ddr+0x310/314[19:16]` | 4 (60 Ω) | 4 (60 Ω) |
| PHY Tx drive p/n | `COMMON+0xc[15:8]` | `0xD8` (40/40 Ω) | `0xB0` (48/60 Ω) |
| PHY Rx ODT | `COMMON+0xc[21:16]` | 60 Ω (`0x24`) | 120 Ω |
| PHY Rx Vref | `COMMON+0x4[31:16]` | `0x55` | `0x33` |
| VOH | `ddr+0x2c8[17]` | 0 (0.6 V) | 0 (0.6 V) |
| PHY supply | `pmu+0x398` | `[11:10] = 3` | `[11:10] = 2`, `[13:12] = 3` |

The selection is by **memory type**, not by DRAM vendor (MR5). After the
writes, these registers echo the programmed recipe, not the die.

## PHY impedance calibration

PHY side: `COMMON+0` 0 → 1 plus the DFI `init_start` wait (PI_STATUS bit 31).
DRAM side: ZQCAL ([device-init.md](device-init.md)). There is no third
calibration state machine.

## PHY_INIT (`phy + 0x10028`)

| Value | Meaning (*inferred*) |
|---|---|
| `0x100` | Analog armed, training FSM not armed. Read after Device_Init (and after an MC reset). |
| `0x200` (512) | Training FSM armed. |

- Device_Init does **not** write this register; it reads `0x100` afterwards.
- Every training pass writes **512** at its start. The word reads `0x200`
  immediately; there is nothing to wait for.
- After a DFC it already reads `0x200`; the write is idempotent. Always write
  512; do not branch on the value read.
- Writing `PHY_INIT` does **not** clear `PI_STATUS`.

## TRAINING block (global, `phy + 0x18000`)

### Status (`phy + 0x18000`)

| Bit | Error |
|---|---|
| 1 | Write leveling |
| 2 | Read leveling |
| 3 | Write DQ |
| 4 | Read gate |

0 means success; it was 0 in every pass on the reference board. The register is
writable: read training clears bit 2 before choosing its Vref
([eye-training.md](eye-training.md)).

`phy + 0x18004` (CA_STAT) reads `0x01008870` by default, also without CA
training; the CA training would write `0x1008870 | pass`.

### Harvest (margins)

| Address | Content |
|---|---|
| `phy + 0x18200` | 8 words: RX margins, 4 DQ per word, little-endian (word `g` → DQ `4g` … `4g+3`) |
| `phy + 0x18220` | 1 word: RX DM margins (4 DM) |
| `phy + 0x18300` | 8 words: TX margins |
| `phy + 0x18320` | 1 word: TX DM margins |

- The harvest is a **latch of the last PI operation**, not an OR over ranks.
  It must be copied **immediately** after each read or write operation, before
  the next rank's operation overwrites it.
- Margin 0 means the eye is closed on that DQ at that Vref.
- The words just before the harvest areas read 0 (no extra fields).
- Address it relative to `phy`, never with an absolute `0xc0058xxx`.

## Gaps

- Meaning of VREF bit 4 and of the low 16 bits (`0xa927`).
- Meaning of `byte0+0x64` (`0x4349`) and of `+0x60`, and why the gate stage
  modifies `+0x64`.
- Exact tap size: the 64-taps-per-UI (TX) and 32-taps-per-UI (RX) scale is
  consistent with the centring but not measured.
- Unit of the harvest margins (taps vs another unit) is not established; the
  values are only used comparatively by the max-min rule.
- Meaning of the low byte `t` of the gate code and of its adjustment by read
  training.
- Meaning of the bits of `PHY_INIT`, of `C+0x10` bit 28, of `C+0x14` and of
  `phy+0x3030 = 0x1077` (all required as listed).
- The LPDDR4 PHY Rx ODT code and the full LPDDR4 IO set were not measured on
  hardware.

---

[← Previous: Device_Init](device-init.md) | [Index](README.md#index) | [Next: Mode Registers →](mode-registers.md)
