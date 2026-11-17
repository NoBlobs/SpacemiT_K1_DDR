<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 Thalles Campagnani -->

[← Previous: Controller Bring-up](controller-init.md) | [Index](README.md#index) | [Next: The Training Pass →](training-pass.md)

# DFC Table and Hardware Frequency Change (`lib/k1_ddr_init.c`)

Two functions: `k1_ddr_dfc_table_load()` writes the command table the hardware
DFC engine executes, once, after the address map; `k1_ddr_dfc()` kicks one
frequency change and verifies it.

## Writing one table entry

```c
static void k1_tbl_put(hw, idx, data, addr)
{
        k1_w32(hw->win + 0x74, data);   /* DATA  */
        k1_w32(hw->win + 0x78, addr);   /* ADDR  */
        k1_w32(hw->win + 0x70, idx);    /* INDEX: commits the entry */
}
```

Always through `hw->win` (`0xf0000000` in the image), never through `ddr`.
`k1_ddr_dfc_table_load()` logs whether the window aliases the MC by comparing
`CTL0` read through both addresses (`alias` / `separate`); the code works the
same either way.

## Rank adaptation — `k1_tbl_data()`

The command flows in the tables are written for two ranks (`0x13` in the top
byte). When `cs_sel != 3`, entries whose ADDR offset is `USER_0` (`0x20`),
`CA_CMD` (`0x24`) or `PI_CMD` (`0x13d0`) and whose DATA top byte is `0x13` get
it replaced by `0x10 | cs_sel` (`0x11` for one rank). Other entries are
written unchanged. The same rewrite applies to the snapshot tail.

## `k1_ddr_dfc_table_load()`

```c
void k1_ddr_dfc_table_load(const struct k1_ddr_hw *hw, bool mr21);
```

Order of writes:

1. Reads the live `CTL_FREQ` and `CTL0`, and `CTL0` through the window (for the
   alias log line).
2. **Entry flow**: every entry of `k1_dfc_flow` (29 entries, indices
   `0x00`–`0x1b` and `0x12e`) via `k1_tbl_seq()`.
3. **Exit flow**: every entry of `k1_dfc_exit` (15 entries, `0x180`–`0x18e`).
4. **Snapshot header** at `0x200`: `CTL0 | (1 << 2) | (1 << 12)`, ADDR
   `0x44 | REQ_PMU`.
5. **Globals** `0x201`–`0x216`: for each offset in `k1_dfc_snap_globals`, the
   live MC value, ADDR = offset.
6. **Four bank blocks** (fp0..fp3, 25 entries each, from `0x217`): selects the
   bank on the live MC by rewriting `CTL_FREQ[31:28]` (both fields = fp, the
   rest of the word as read in step 1), then stores each offset of
   `k1_dfc_snap_bank` with its live value. The `CTL_FREQ` entry is stored with
   `[29:28]` cleared.
7. Restores `CTL_FREQ` to the value read in step 1.
8. **Tail** `0x27b`–`0x289`: the 15 fixed entries of `k1_dfc_snap_tail`
   (through `k1_tbl_data()`).
9. **End** at `0x28a`: the original `CTL0`, ADDR `0x44 | EOP`.
10. **PHY markers**: three triplets written to `win + 0x40000 + 0x10104`
    (DATA), `+0x10108` (ADDR), `+0x10100` (INDEX).

`static_assert`s pin the layout: `0x200 + 1 + 22 + 4 × 25 == 0x27b` and
`0x27b + 15 == 0x28a`.

### MR21 variant — `k1_tbl_seq()`

With `mr21 = true` (16 GiB parts), an extra entry
`CA_CMD = 0x13020095` (MR21 write; rank byte adapted like the others) is
inserted right after index `0x18` of the entry flow and after `0x18b` of the
exit flow, and the following entries **up to the end of that run**
(`0x1b` / `0x18e`) move up by one index. The copy at `0x12e` and the snapshot
do not move. The insertion points are `K1_DFC_FLOW_MR21_AFTER`,
`K1_DFC_FLOW_RUN_END`, `K1_DFC_EXIT_MR21_AFTER` and `K1_DFC_EXIT_RUN_END` in
`k1_ddr_tables.h`.

## `k1_ddr_dfc()`

```c
int k1_ddr_dfc(const struct k1_ddr_hw *hw, uint32_t fp);
```

### Refusals (nothing is written)

| Condition | Result |
|---|---|
| `fp > 3` | `-EINVAL` |
| STATUS has a self-refresh bit set (`0x44` with 2 ranks, `0x04` with 1) | `-EIO`, logged "ranks in self-refresh" |
| `CC_AP` bit 22 already set (a previous DFC is stuck) | `-EIO`, logged "bit 22 still held" |

### Kick

```
AP_IMR    |= 1 << 4                    (mask the DCLK frequency-change IRQ)
ddr+0x148  = 0x80ac0000
CLKSEL     = k1_clksel[fp]
HW_SLP     = 0x40200200 | fp << 3
(snapshot of the diagnostic registers: "before")
CC_AP      = 1 << 22                   (whole-word write)
wait (CC_AP & (1 << 22)) == 0          10 ms
```

### Success path

Reads `CTL_FREQ`, `PI_CFG`, STATUS and CLKSEL, logs
`k1-ddr: DFC fp<N> in <µs> us: …`, and returns `-EIO` unless all of these hold:

- `CTL_FREQ[31:28]` equals `k1_freq_bank(fp)[31:28]`;
- `PI_CFG[3:2] == fp`;
- both PLL lock bits are set in CLKSEL;
- no self-refresh bit is set in STATUS.

### Timeout path

1. Reads the diagnostic registers again ("stuck") and logs both snapshots:
   STATUS, CTL0, MISC, `ddr+0x148`, `CTL_FREQ`, `PI_CFG`, `PI_STATUS`,
   `PHY_INIT`, `CC_AP`, `AP_ISR`, `HW_SLP`, CLKSEL, and `CTL0` through the
   window.
2. `k1_dfc_unstick()`: issues a self-refresh exit for `cs_sel` and waits up to
   10 ms for the SR bits to clear, so the DRAM is accessible again; logs
   whether the ranks are still parked and whether bit 22 is still held
   ("MC reset before any DFC").
3. Returns `-ETIMEDOUT`.

After a timeout the boot stops (`k1_ddr_boot()` returns step `dfc`). The guard
on bit 22 makes sure a later caller cannot kick another DFC on top of a stuck
one; only an MC reset would clear it.

## How the boot uses it

`k1_ddr_boot()` calls `k1_ddr_dfc_table_load()` once (after the map), then
`k1_boot_dfc()` → `k1_ddr_dfc()` for fp1, fp2 and the final fp, each followed
by a discarded MR14 read and an MR14 check ([boot-sequence.md](boot-sequence.md)).

## Error-injection hook

`K1_EXP_DFC_TIMEOUT` (in `k1_ddr_boot.c`) skips the table load, which makes the
first DFC hang and exercises the timeout path on real hardware.

---

[← Previous: Controller Bring-up](controller-init.md) | [Index](README.md#index) | [Next: The Training Pass →](training-pass.md)
