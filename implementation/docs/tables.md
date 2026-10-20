<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 Thalles Campagnani -->

[← Previous: The Training Pass](training-pass.md) | [Index](README.md#index) | [Next: Acceptance Checks and Logging →](acceptance-and-logging.md)

# Data Tables (`lib/k1_ddr_tables.c`)

All multi-word register data of the bring-up lives in one file, separate from
the code that sequences it. Single values (offsets, masks, command words, PLL
words, CFG parameters) are named constants in `lib/k1_ddrt_regs.h`. Keeping
data apart lets a host tool print every value and compare it with the
specification ([testing.md](testing.md), `check-tables`).

## Types (`lib/k1_ddr_tables.h`)

| Type | Fields | Meaning |
|---|---|---|
| `struct k1_reg_val` | `off`, `val` | One MC word |
| `struct k1_bank_row` | `off`, `io`, `fp[4]` | One banked MC word, a value per frequency point; `io = true` means "computed from the IO configuration" |
| `struct k1_tbl_entry` | `data`, `addr`, `idx` | One DFC table line: DATA, ADDR (MC offset + flags), INDEX |
| `struct k1_io_cfg` | `ca_vref`, `tx_vref`, `soc_odt`, `tx_odt`, `rx_drv`, `tx_pdrv`, `tx_ndrv`, `rx_odt`, `rx_vref`, `rx_pu_cal` | Electrical configuration of one memory type |

Rows written with the `ROW(off, f3, f2, f1, f0)` macro list the columns as
**fp3, fp2, fp1, fp0** (the order of the specification tables) but store them
by index (`fp[3]`…`fp[0]`), so code indexes `fp[n]` naturally.

## Tables

| Symbol | Entries | Content | Consumer |
|---|---|---|---|
| `k1_mc_globals` | 20 | Frequency-independent MC words; entry 0 must be `CTL0` (read back as the "MC is clocked" check) | `k1_mc_program` |
| `k1_mc_timing` | 24 | Banked words per fp; three IO rows (`0x30c`, `0x310`, `0x314`) | `k1_mc_program` |
| `k1_mc_x8_overlay` | 2 | `0x13e4` and `0x3b4` per fp for x8 dies | `k1_mc_program(x8)` |
| `k1_mc_wdqs` | 4 | `ddr+0x400` per fp, LPDDR4 only | `k1_mc_program` |
| `k1_mc_refresh` | 3 | `0x380`/`0x384`/`0x388`, written once after fp0 is selected | `k1_mc_program` |
| `k1_clksel` | 4 | PMU CLKSEL word per fp (`0x3b50`, `0x3b04`, `0x3b40`, `0x3b00`) | `k1_ddr_pll_init`, `k1_ddr_dfc` |
| `k1_io_lpddr4x` | — | `ca_vref 0x9d`, `tx_vref 0x19`, `soc_odt 4`, `tx_odt 3`, `rx_drv 4`, `tx_pdrv 6`, `tx_ndrv 6`, `rx_odt 4`, `rx_vref 0x55`, `rx_pu_cal 0` | `k1_write_io`, `k1_phy_analog` |
| `k1_io_lpddr4` | — | `ca_vref 0xb2`, `tx_vref 0xa7`, `soc_odt 3`, `tx_odt 2`, `rx_drv 4`, `tx_pdrv 5`, `tx_ndrv 4`, `rx_odt 2`, `rx_vref 0x33`, `rx_pu_cal 0` | same (not reachable from the boot today) |
| `k1_mr_init1` | 5 | MR1, MR2, MR13, MR3, MR22 — before ZQ | `k1_dram_start` |
| `k1_mr_init2` | 4 | MR12, MR14, MR11, MR23 — after ZQ | `k1_dram_start` |
| `k1_dfc_flow` | 29 | DFC entry flow (`0x00`–`0x1b`, `0x12e`) | `k1_ddr_dfc_table_load` |
| `k1_dfc_exit` | 15 | DFC exit flow (`0x180`–`0x18e`) | `k1_ddr_dfc_table_load` |
| `k1_dfc_snap_globals` | 22 | MC offsets captured once in the snapshot | `k1_ddr_dfc_table_load` |
| `k1_dfc_snap_bank` | 25 | `CTL_FREQ` plus the 24 banked offsets captured per fp | `k1_ddr_dfc_table_load` |
| `k1_dfc_snap_tail` | 15 | Fixed entries `0x27b`–`0x289` | `k1_ddr_dfc_table_load` |
| `k1_dfc_marks` | 3 × 3 | PHY marker triplets (DATA, ADDR, INDEX) | `k1_ddr_dfc_table_load` |

Resistance codes in the IO configurations: 1 = 240 Ω, 2 = 120 Ω, 3 = 80 Ω,
4 = 60 Ω, 5 = 48 Ω, 6 = 40 Ω.

Counts are declared as `static constexpr size_t K1_N_*` in the header and the
arrays are declared with those sizes, so a missing or extra row is a compile
error. The snapshot layout is additionally checked by `static_assert`s in
`k1_ddr_init.c`.

## Two copies of the banked-register list

The 24 banked offsets appear twice: as the rows of `k1_mc_timing` (what
Device_Init writes) and inside `k1_dfc_snap_bank` (what the DFC snapshot
reads back). They are the same set in a different order: the snapshot list
places `0x400` (WDQS) among the timings and `0x34c` near the end, following the
snapshot layout, while Device_Init writes `0x400` only for LPDDR4. The test
model has a third copy (`banked_off[]` in `model.c`) that decides which offsets
are per-fp in the simulated MC.

## Inspecting the tables

`tools/test/dump_tables.c` prints every fact in a line-oriented format, one
fact per line (265 lines today), for example:

```
global 0x44 0x40300
freq 2 0xa0800400
timing 0x300 0xc18 0xc18 0x80e 0x80e
timing 0x30c io
overlay 2 0x13e4 0x15000802
clksel 2 0x3b40
hwslp 2 0x40200210
pll 2400 0x2a203b00 0x50dd6432
io lpddr4x tx_pn 0xd8
flow 0x0 0x40303 0x44
exit 0x180 0x40b43 0x44
snap_bank 2 0x8 0x249
mark 0 0x1100 0x10000 0x20
size 0xd 512
geom x16 4 1024 0xe 15
rate lpddr4x 1066 1
mr_init1 1 2 13 3 22
mr21 0x13020095
mr21_after flow 0x18 0x1b
```

Fact kinds and counts: `flow` 29, `snapb` 25, `timing` 24, `snapg` 22,
`rate` 22, `global` 20, `io` 18, `geom` 16, `snapt` 15, `exit` 15, `size` 9,
`overlay` 8, and one to four lines each of `wdqs`, `snap_bank`, `hwslp`,
`freq`, `clksel`, `tbl_flag`, `refresh`, `pll`, `mark`, `mr21_after`,
`swfc_slp`, `snap_first`, `snap_end`, `snap_ctl0_bits`, `mr_init1`,
`mr_init2`, `mr21` and `dfc_pre`.

It also prints derived facts produced by the code, not just the table
contents: `k1_freq_bank(fp)`, the HW-DFC `HW_SLP` word per fp, the CTL_FREQ
value stored in each snapshot block, size codes, geometries from every MR8
density, and the final fp of a list of data rates. `check_tables.py` compares
this set with the set it extracts from the specification and reports the
differences in both directions ([testing.md](testing.md)).

## Changing a table

1. Change the value in `k1_ddr_tables.c` (or the constant in
   `k1_ddrt_regs.h`).
2. If an entry is added or removed, update the `K1_N_*` count; the snapshot
   `static_assert`s must still hold.
3. Run `make host-test` (the model checks the snapshot lines it receives
   against its own copy of the layout) and `make check-tables` when the
   specification source is available.
4. If the change alters behaviour, add or adjust a mutant in `mutate.py` so the
   new value is pinned by a test.

---

[← Previous: The Training Pass](training-pass.md) | [Index](README.md#index) | [Next: Acceptance Checks and Logging →](acceptance-and-logging.md)
