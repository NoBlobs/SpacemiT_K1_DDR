<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 Thalles Campagnani -->

[← Previous: Entry Points and Runtime Glue](entry-points.md) | [Index](README.md#index) | [Next: Controller Bring-up →](controller-init.md)

# Boot Sequence (`lib/k1_ddr_boot.c`)

`k1_ddr_boot()` decides **what** happens and **in which order**. It owns no
register knowledge beyond reading a few status words for the report: every
hardware action is a call into `k1_ddr_init.c` or `k1_ddrt.c`.

```c
int k1_ddr_boot(const struct k1_ddr_boot_cfg *cfg,
                const struct k1_ddrt_env *env,
                struct k1_ddr_boot_report *rep);
```

## Steps

| # | Action | Call | Failure step |
|---|---|---|---|
| 0 | Validate arguments | — | `args` (−EINVAL) |
| 0b | Refuse LPDDR4 (non-X) | — | `args` (−ENOSYS) |
| 1 | Build the hardware context, compute the final fp | `k1_ddr_hw_init`, `k1_ddr_final_fp` | — |
| 2 | PLL1/PLL2, lock, SW-FC to fp0, MC clock | `k1_ddr_pll_init` | `pll-lock` / `sw-fc` |
| 3 | Device_Init with x16 timings | `k1_ddr_device_init(x8 = false)` | `mc-program` / `dfi-init` / `dram-init` |
| 4 | First training pass at fp0 | `k1_boot_pass(0)` | `training` |
| 5 | Read MR5 and MR8 (rank 0, channel 0) | `k1_ddrt_mr_read` ×2 | `mr-read` |
| 6 | Derive size and geometry from MR8 | `k1_ddr_geometry` | `address-map` (−EINVAL) |
| 7 | If x8: MC reset, Device_Init again with x8 overlays | `k1_ddr_mc_reset`, `k1_ddr_device_init(x8 = true)` | `mc-program` / `dfi-init` / `dram-init` |
| 8 | Program the address map | `k1_ddr_map_apply` | — |
| 9 | Load the DFC table (MR21 variant when the size is 16 GiB) | `k1_ddr_dfc_table_load` | — |
| 10 | Second training pass at fp0 | `k1_boot_pass(0)` | `training` |
| 11 | For each fp of the plan: DFC, then a pass | `k1_boot_dfc(fp)`, `k1_boot_pass(fp)` | `dfc` / `training` |
| 12 | 16 GiB only: MR21 write | `k1_ddr_mr_write(0x95)` | — |
| 13 | Final DFC to the final fp | `k1_boot_dfc(final)` | `dfc` |

Each failure returns at once through `k1_boot_end()`, which fills the report
and logs `k1-ddr: <step> failed, rc <rc>`.

### Argument validation

Refused with `-EINVAL` (step `args`) when: `rep` is `NULL` (nothing is
written), `cfg` is `NULL`, `ddr`, `pmu` or `dfc_win` is 0, `cs_num` is not 1
or 2, `env` is `NULL`, or `env->now_us` is `NULL`. No register is touched in
any of these cases (a test checks that the model saw zero MMIO writes).

`type == K1_DDR_LPDDR4` returns `-ENOSYS` before touching hardware. The lower
layers already implement the LPDDR4 IO table, WDQS words, PHY LDO setting and
the fp3 pass plan, but the boot keeps them disabled
([implementation-notes.md](implementation-notes.md)).

### Rate handling — `k1_ddr_final_fp()`

| `data_rate` | Final fp |
|---|---|
| 2400, 2666 | 2 |
| 1066 | 1 |
| 2133 | 3 for LPDDR4, 0 for LPDDR4X |
| anything else (1200, 1600, 0, 2667, 2700, 3200, 4266, …) | 0 |

The PLL1 programming depends only on `data_rate == 2666` (see
[controller-init.md](controller-init.md)); every other value uses the
2400 setting.

### Pass plan — `k1_ddr_dfc_plan()`

The frequency points trained after the second fp0 pass, in order:

| Type | Plan |
|---|---|
| LPDDR4X | 1, 2 |
| LPDDR4 | 1, 3, 2 |

So an LPDDR4X boot always runs **four** passes (fp0, fp0, fp1, fp2) and
**three** DFCs (fp1, fp2, final), regardless of the requested rate: the final
DFC is issued even when the final fp is the one just trained (it is
idempotent), and for a 1066/1200 request the final DFC goes back down from fp2.
The report keeps up to five pass summaries (enough for LPDDR4).

### x8 handling

`k1_ddr_geometry()` sets `x8` when `MR8[7:6] == 1`. In that case the boot sets
`rep->byte_mode`, pulses the MC reset and re-runs the whole Device_Init with
the x8 overlays. The PLLs are not touched again. The first pass ran with x16
timings on purpose: it only has to make MR8 readable.

### `k1_boot_pass()`

Runs `k1_ddrt_train_pass()` with `{ ddr, cs_num, boot_pp = pp }`, appends a
`k1_ddr_pass_summary` to the report, calls `cfg->on_pass(pass_ctx, &res, pp)`
when set, and returns the pass result. The full `struct k1_ddrt_result` lives
in this function's frame and is discarded after the observer returns.

### `k1_boot_dfc()`

1. `k1_ddr_dfc(hw, fp)`; on failure returns its code.
2. One **discarded** MR14 read on rank 0 / channel 0. After a hardware
   frequency change the first mode-register read may return 0 whatever the
   register; this read absorbs it.
3. `k1_ddrt_check_mr14()` on every (rank, channel). A mismatch is only a
   warning (`k1-ddr: warning: MR14 differs from the fp<N> bank`): the DFC
   engine re-sends MR14 from the new bank, so this is a cheap check that the
   engine ran its flow, not a failure condition.

## The report

`struct k1_ddr_boot_report` is zeroed at entry and filled as the boot goes:

| Field | Filled |
|---|---|
| `final_pp` | after argument checks |
| `mr5`, `mr8` | after step 5 |
| `byte_mode` | step 7 |
| `size_mb` | step 8 (`area_mb × cs_num`) |
| `n_pass`, `pass[]` | after every pass |
| `rc`, `fail_step` | on return |
| `clksel` | on return, whenever the PMU base is known |
| `status`, `ctl_freq` | on return, only after the MC clock is on (reading the MC before the SW-FC and MC clock enable is not safe, so a PLL failure leaves them 0) |
| `total_us` | on return |

## Logging

Besides the per-pass observer, the boot logs:

- `k1-ddr: LPDDR4X, cs_num N, R MT/s, final fpF`
- `k1-ddr: MR5 0x.. MR8 0x..`
- `k1-ddr: <MB> MB (x8|x16, ROW_n), cs0 0x.. cs1 0x.. 0x.. geom 0x..`
- every line of the lower layers (PLL, Device_Init, DFC; see
  [acceptance-and-logging.md](acceptance-and-logging.md)).

## Error-injection hooks

Built only with explicit flags (never in the production image):

| Flag | Effect in this file |
|---|---|
| `K1_EXP_DFC_TIMEOUT` | Skips `k1_ddr_dfc_table_load()`; the first DFC then hangs and times out |

The other two flags live in `k1_ddr_init.c` and `k1_ddrt.c`
([testing.md](testing.md)).

---

[← Previous: Entry Points and Runtime Glue](entry-points.md) | [Index](README.md#index) | [Next: Controller Bring-up →](controller-init.md)
