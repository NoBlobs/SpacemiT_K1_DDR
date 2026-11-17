<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 Thalles Campagnani -->

[← Previous: DFC Table and Hardware Frequency Change](dfc.md) | [Index](README.md#index) | [Next: Data Tables →](tables.md)

# The Training Pass (`lib/k1_ddrt.c`)

`k1_ddrt.c` trains **one** frequency point that is already running. The PI
engine in the controller finds the edges; this code arms it, sweeps the Vref,
applies the max-min rule and re-runs the PI at the winning Vref. The file also
provides the wait/log/time primitives shared with `k1_ddr_init.c`, mode-register
reads and the MR14 check.

```c
int k1_ddrt_train_pass(const struct k1_ddrt_cfg *cfg,
                       const struct k1_ddrt_env *env,
                       struct k1_ddrt_result *res);
```

Returns `K1_DDRT_OK` only if every stage succeeded, self-refresh was left, the
PHY training status is 0 and every gate run passed. Any other value must stop
the boot.

## Shared primitives

| Function | Behaviour |
|---|---|
| `k1_log(env, fmt, …)` | Calls `env->vlog` if both exist; otherwise nothing |
| `k1_now(env)` | `env->now_us(ctx)`, or 0 without a clock |
| `k1_udelay(env, us)` | Busy-waits on `now_us`; a no-op without a clock |
| `k1_wait_n(env, addr, mask, want, budget_us, &last, &polls)` | Polls `addr` until `(v & mask) == want`. Gives up after `budget_us` (only when a clock exists) or after `poll_limit` reads (default 100 000 000), whichever comes first. Returns `OK` or `-ETIMEDOUT`, the last value read and the number of polls |
| `k1_wait(…)` | `k1_wait_n` without the poll count |

Budgets used in this file: PI operation 1 s (the engine has no hardware
timeout), self-refresh 10 ms, MR read 10 ms.

## Internal state

```c
struct k1_pass {
        env, res, pi (stats of the current stage),
        ddr, phy = ddr + 0x40000, slice = phy + (boot_pp << 14),
        cs_num, poll_limit
};
```

Built by `k1_pass_init()` after `k1_cfg_ok()` accepted the configuration
(`ddr != 0`, `cs_num` 1–2, `boot_pp` ≤ 3). The address of byte `b` of rank
`cs0` (0-based) is `slice + K1_BYTE_OFF[b] + (cs0 << 8)` with
`K1_BYTE_OFF = { 0x0000, 0x1000, 0x0200, 0x1200 }`.

## Flow

```
res = {}                                  zeroed even on bad arguments
check cfg                                 → -EINVAL, stage args, no MMIO
sr_stat[0] = STATUS
SR enter (wait mask set, 10 ms)           → on failure: stage sr-enter, SR exit
sr_stat[1] = STATUS
PHY_INIT = 512
for stage in [WL, gate, Rx, Tx]:          each timed into stage_us[i],
        run; stop at the first failure      PI stats into pi[i]
phy_status = PHY_STATUS                   read before leaving SR
SR exit (wait mask clear, 10 ms)          always attempted
sr_stat[2] = STATUS
collect taps / gate / WL / Vref words     always (useful after a failure)
return: stage error, else SR-exit error (stage sr-exit),
        else PHY_STATUS != 0 (-EIO, stage phy-status), else OK
```

Self-refresh commands: `USER_0 = 0x10000000 | (mode << 24) | (1 << 6)` to enter
and `| (2 << 6)` to exit, `mode = 3` with two ranks and 1 with one; the wait
mask is `0x44` or `0x04`. The exit only waits for those bits to drop; it never
waits for a particular full STATUS value.

## The PI wrapper — `k1_pi_op()`

```c
static int k1_pi_op(struct k1_pass *p, uint32_t op, uint32_t cs,
                    uint32_t done, uint32_t *status, uint32_t *first);
```

1. `PI_CMD = op | (cs << 24)` with a **1-based** `cs`.
2. Reads `PI_STATUS` once right away (`snap`). Issuing a command clears that
   operation's done bits; if they still read as set, the read is counted as a
   *stale first read* in the statistics.
3. `k1_wait_n(PI_STATUS, done, done, 1 s)`; stores the last value in
   `res->pi_status`.
4. Updates the stage statistics: operations, stale first reads, max polls,
   max µs.

`snap` is returned to the gate stage, which needs it.

## Stages

### Write leveling — `k1_stage_wl()`

`PI_WL_CFG = 0x0a641e44` once, then `WL` (`0x10400000`, done `0x01`) for each
rank 1..`cs_num`. No result check of its own; failures surface in `PHY_STATUS`.

### Read gate — `k1_stage_gate()`

For each rank: `RGATE` (`0x10100000`, done `0x02`), then decides PASS or
TIMEOUT:

- if bit 2 of `snap` (the first `PI_STATUS` read after the command) was 0:
  PASS requires `(st & 6) == 6`; anything else is a timeout → `-EIO`;
- if bit 2 was already set (left over by an earlier read-training operation),
  that test cannot tell, so the decision is `PHY_STATUS` bit 4 (gate error) →
  `-EIO` if set.

After all ranks pass, every gate code of every rank and byte must look trained:
bit 16 set and the two low bytes equal (`(v & 0xff) == ((v >> 8) & 0xff)`);
otherwise `-EIO` with the offending rank and byte logged.

With `-DK1_EXP_GATE_TIMEOUT` the stage never sends RGATE and waits for its done
bit until the 1 s budget expires, then fails as a timeout on rank 1.

### Read training — `k1_stage_rx()`

```
VREF0 &= ~bit4
PI_RD_CFG = 0x5070
for step 0..15:
        VREF0 = VREF1 = (VREF0 & 0xffff) | pack_vref(step)
        for cs 1..cs_num:
                RD (0x10200000, done 0x06)
                m = worst DQ margin of harvest RX     (copied right away)
                read RX DQ taps (mask 31)              (for the verbose log)
                rx_min_cs[cs-1][step] = m
        rx_min[step] = min over ranks
PHY_STATUS &= ~bit2                                     (read-leveling error)
best = max_min(rx_min)
VREF0 = VREF1 = (VREF0 & 0xffff) | pack_vref(best)
for cs 1..cs_num: RD                                    (re-apply)
VREF0 |= bit4
```

### Write training — `k1_stage_tx()`

```
PI_WR_CFG = 0x1810
for i 0..15:
        set_mr14(21 + i)          TXVREF[21:16] = value; CA_CMD = 0x1302000e
        for cs 1..cs_num:
                WR (0x10080000, done 0x60)
                m = worst DQ margin of harvest TX
                read TX DQ taps (mask 63)
                tx_min_cs[cs-1][i] = m
        tx_min[i] = min over ranks
best = max_min(tx_min)
if tx_min[best] == 0: best = 4                          (MR14 = 25)
set_mr14(21 + best)
for cs 1..cs_num: WR                                    (re-apply)
```

### Harvest — `k1_harvest_take()`

Reads the 8 DQ words of the harvest area (4 margins per word, byte `k` of word
`g` is DQ `4g + k`) and the DM word at `+0x20`. Returns the minimum over the 32
DQ margins only; the DM margins are stored but never weigh in. It is called
after every operation of every rank, because the harvest only holds the last
operation.

### The max-min rule — `k1_ddrt_max_min()`

Returns the first index holding the largest of the 16 minima (strict `>`), so
on a plateau the lowest step wins. Exposed publicly so the tests and other
callers use the same rule.

## Result collection — `k1_collect()`

Fills, for every populated rank and byte: `wl`, `gate`, `tx_dm`, the 8 RX DQ
taps (mask 31) and the 8 TX DQ taps (mask 63); plus `vref0`, `vref1` and
`txvref`. It runs after the SR exit, also when a stage failed.

`k1_ddrt_snapshot()` runs the same collection without touching the PI (and
also reads `PHY_STATUS` and `PI_STATUS`); the re-train entry uses it to print
the state before a pass.

## Mode registers

- `k1_ddrt_mr_read(cfg, env, cs, ch, mr, &val)`: rejects `cs ≥ 2`, `ch > 1`,
  `mr > 0xff`; writes `CA_CMD = 0x10010000 | (cs + 1) << 24 | ch << 18 | mr`,
  waits for `MR_DATA` bit 31 (10 ms) and returns the low byte. Must not be
  called in self-refresh.
- `k1_ddrt_check_mr14(cfg, env)`: reads MR14 on every (rank, channel) and
  compares it with `CTL_TXVREF[21:16]`; logs every mismatch and returns `-EIO`
  if any; a read error is returned as is.

## Logging

| Line | When |
|---|---|
| `k1-ddrt: pass pp<N> cs_num <n>` | start of a pass |
| `k1-ddrt:   rx step S cs C min M dq0 …` / `tx vdq V cs C min M dq0 …` | every step and rank, only with `env->verbose` |
| `k1-ddrt: read-gate TIMEOUT on cs C (pi_status …)` | gate timeout |
| `k1-ddrt: read-gate error on cs C (phy_status …)` | gate error via PHY status |
| `k1-ddrt: cs C byte B gate 0x… not trained` | gate code check |
| `k1-ddrt: <stage> failed: cs C rc R pi_status …` | any failure (`k1_fail`) |
| `k1-ddrt: PHY training status 0x…` | non-zero PHY status |
| `k1-ddrt: ppN ok: rx vref idx I (min M), tx mr14 V (min M)` | success |
| `k1-ddrt: MR14 csC chH = 0x.., TXVREF has 0x..` | MR14 check mismatch |

## What the tests pin down

Among the checks in `tools/test/test_main.c` ([testing.md](testing.md)):
the exact event order (SR enter, PHY_INIT, WL, gate, Rx, Tx, SR exit), each CFG
written exactly once and before its first operation, 17 MR14 commands, 17 RD
and 17 WR operations per rank, the re-applied winner's taps left in the PHY,
first-of-plateau selection, the Tx all-zero fallback to MR14 25, the minimum
taken over every rank and DQ, SR exit after every kind of failure, and that bad
arguments cause no register access.

---

[← Previous: DFC Table and Hardware Frequency Change](dfc.md) | [Index](README.md#index) | [Next: Data Tables →](tables.md)
