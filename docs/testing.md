<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 Thalles Campagnani -->

[← Previous: Acceptance Checks and Logging](acceptance-and-logging.md) | [Index](README.md#index) | [Next: Validation Vehicle →](validation-vehicle.md)

# Testing (`tools/test/`)

The firmware is tested without hardware by compiling the library against a
behavioural model of the controller, PHY, PI engine and PMU. Four layers of
checks exist:

| Command | What it proves |
|---|---|
| `make host-test` | The suite, natively, under AddressSanitizer and UndefinedBehaviorSanitizer |
| `make test` | The same suite compiled for rv64gc and run under `qemu-riscv64` (same ISA and ABI as the image) |
| `make host-test-exp` | Each error-injection flag produces exactly the failure it is meant to produce |
| `make mutate` | The suite actually detects deliberate bugs (122 mutants) |
| `make check-tables SPEC_DOCS=…` | Every register value in the code matches the specification tables (265 facts) |

A full run of the suite prints one `ok`/`FAIL` line per test and ends with
`1426 checks, 0 failures`.

## How the code is connected to the model

`lib/k1_ddrt_io.h` has two implementations of `k1_r32`/`k1_w32`. The test
builds define `K1_DDRT_MOCK`, which turns every register access into a call to
`k1_mock_read32`/`k1_mock_write32` in `model.c`. Nothing else changes: the same
`lib/` sources run in the image and in the tests, at the same addresses
(`DDR = 0xc0000000`, `PMU = 0xd4282800`, `WIN = 0xf0000000` in `check.h`).
`blob/` is not part of the test build.

Time comes from `model_now_us()`, which advances a simulated clock by
`time_step_us` (default 7 µs) on every call, so timeouts and delays are
deterministic.

## The model (`model.c`, `model.h`)

The model is a **test double for the algorithm, not a simulator of the
silicon**. It keeps register arrays for the MC+PHY window (with four banked
copies of the per-fp registers, selected by `CTL_FREQ[31:30]` like the real
MC) and for the PMU, and reacts to writes.

### Two starting states

| Function | State |
|---|---|
| `model_reset(ddr, boot_pp, real_cs)` | Controller already up at `boot_pp`, STATUS `0x33`, PI idle; **strict** mode: only the writes a training pass is allowed to make are accepted |
| `model_reset_por(ddr, real_cs)` | Cold SoC: PLLs at the values found at power-on, MC in reset, dies not initialized, PHY at reset values, IO words unknown (`0xa5a5a5a5`) |

### What it simulates

- **PLLs and kicks**: lock appears a few CLKSEL reads after valid enable +
  VCO words; SW-FC and HW-DFC kicks complete a few `CC_AP` reads later.
- **MC reset**: clearing and setting `HW_SLP` bit 21 wipes MC/PHY state and
  the DFC table, and must last ≥100 µs.
- **Device_Init**: COMMON arming (0 then 1), DFI `init_start` handshake,
  JEDEC init setting STATUS, MR writes, ZQ START/LATCH timing.
- **DFC engine**: records every table line written through the window (or
  through the MC port when `win_alias` is set) and checks each snapshot line
  against its own copy of the layout as it is written. A kick with an
  incomplete table parks the ranks (STATUS SR bits set), keeps bit 22 and
  leaves `CTL_FREQ` untouched; a complete one switches fp, updates
  `CTL_FREQ`/`PI_CFG`/STATUS and re-sends MR14 from the target bank.
- **PI engine**: each operation clears its done bits and completes after
  `latency` status reads, writing taps and harvest from per-test margin
  functions (`rx_margin`, `tx_margin`); defaults put the Rx peak at index 5
  and the Tx peak at index 7, with the worst DQ moving with step and rank.
- **Mode registers**: MR5/6/8/12 values of an x8 8 Gb part, MR14 per die and
  channel following the MR14 commands and DFCs, MR13 reading 0, and the first
  MRR after the first DFC to fp2 reading 0.

### Knobs (per test)

`gate_timeout_cs`, `hang_op`, `arm_rl_in_sweep`, `sr_enter_stuck`,
`final_status_bits`, `latency`, `clear_delay` (stale done bits after a
command), `pll_never_locks`, `swfc_stuck`, `dfc_stuck`, `dfi_stuck`,
`dram_init_stuck`, `pmu_delay`, `freq_read_or` (stuck bits on `CTL_FREQ`
reads), `win_alias`, `dfc_no_mr14`, `time_step_us`, `rx_peak`, `tx_peak`,
custom margin functions, and `mr[]` contents (e.g. MR8).

### Violations

Every rule the code must respect is a check inside the model. Breaking one
increments `g_model.violations` and records the first message; tests end with
`CHECK_CLEAN()` (zero violations). Examples:

- any MC/PHY access before the SW-FC and MC clock enable, or within 100 µs of
  an MC reset release;
- PLL words rewritten while locked; PLL enabled without bit 11 or before its
  VCO words; a SW-FC with the wrong `HW_SLP`/CLKSEL;
- a HW-DFC kick that is not exactly `1 << 22`, with ranks in self-refresh,
  without `ddr+0x148 = 0x80ac0000`, with the DCLK IRQ unmasked, with a wrong
  `HW_SLP` or CLKSEL; writing `CC_AP` while bit 22 is stuck;
- `init_start` before every COMMON is armed; JEDEC init before the DFI
  handshake; ZQ LATCH less than 2 µs after START; back-to-back MR writes; MR
  writes right after a ZQ LATCH;
- in strict (training-only) mode: any PMU write, any MC write other than PI
  commands/CFG, USER_0, CA_CMD and the MR14 field of `TXVREF`, any PHY write
  other than `PHY_INIT`, the bit-2 clear of `PHY_STATUS` and the two Vref
  words (whose low half must stay `0xa927`/`0xa937`);
- PI commands outside self-refresh, while an operation is running, or with a
  rank field outside 1..`real_cs`; reading MRs in self-refresh; an RD with
  VREF1 different from VREF0 or with bit 4 set.

### Observations

The model records an event trace (`EV_SR_ENTER`, `EV_OP_WL`, `EV_DFC`, …),
per-event and per-rank counters, the MR/ZQ commands issued, the table lines
received, the analog markers, the DFC targets and every MR14 value read (with
the number of DFCs and SR entries before it). Tests assert on these.

## Test harness (`check.h`, `test_main.c`)

`CHECK(c)`, `CHECK_EQ(a, b)` and `CHECK_CLEAN()` count checks and print
`FAIL file:line` without stopping. `env_timed()` gives an environment with the
model clock and a log sink; `log_has()` searches everything logged since
`log_clear()` (first 64 KiB). Run with `-v` to echo the log.

## Test list

Training pass (`test_main.c`):

| Test | Checks |
|---|---|
| happy path, 2 CS, pp2 | Winners equal a brute-force max-min over the model eye; final Vref words, CFG values, MR14 on the dies, SR commands, STATUS `0x11` after exit, 17 RD/WR per rank, event order, the winner's taps left in the PHY, MR14 check pass/fail |
| happy path, 1 CS, pp0 | Single-rank SR commands (`0x11000040/80`), no CS2 operations |
| leftover done bits do not empty the waits | Starting from `PI_STATUS 0x80008067` with slow completion |
| PI counters and stale-read detector | Stage op counts (2, 2, 34, 34), STATUS snapshots, per-rank minima; a model that drops done bits late is detected |
| plateau keeps the first step | First index of a plateau wins for Rx and Tx |
| Tx eye closed everywhere falls back to MR14 25 | All-zero Tx margins |
| minimum spans every rank and DQ | A dead DQ on one rank only moves the winner |
| self-refresh entry timeout still exits SR | SR exit issued, no PHY_INIT, no PI ops |
| read-gate TIMEOUT stops the pass | Even when an older pass left trained-looking codes |
| hung PI op times out (clock / poll cap) | Time budget and poll limit both work; SR is left |
| sweep-armed read-leveling bit is cleared | Bit 2 set during the sweep does not fail the pass |
| non-zero PHY status fails the pass | Stage `phy-status`, SR left |
| bad arguments touch no register | Zero MMIO writes |
| addresses relative to ddr | The pass runs with the MC at `0x20000000` |
| mode register read, max-min rule | Unit checks |

Bring-up (`test_boot.c`):

| Test | Checks |
|---|---|
| gate TIMEOUT behind a leftover bit 2 | The PHY status path catches it |
| gate TIMEOUT over codes of an earlier pass | Stale codes do not hide it |
| data rate to final fp, DFC plan | `k1_ddr_final_fp`, `k1_ddr_dfc_plan` |
| PLL, lock and SW-FC to fp0 | Exact PMU words for 2400 and 2666; lock and SW-FC failures leave the MC untouched |
| Device_Init x16 from power-on | Every global, every bank word, IO words, PHY analog words, MR and ZQ command sequence and order |
| Device_Init refuses an unclocked or mis-selected MC | `CTL0` read-back and `CTL_FREQ` checks |
| Device_Init x8 overlays / LPDDR4 IO and WDQS | Per-bank overlay words; LPDDR4 IO words, WDQS, LDO |
| MR8 to size code and geometry, address map words | Size codes, rows, CS map words for 2 ranks, 16 GiB and 1 rank |
| DFC table: flow, snapshot, markers | Every line, header/end flags, `[29:28]` cleared in stored `CTL_FREQ`, markers through the window, nothing through the MC port |
| DFC window, refusals, a stuck engine | Alias window works; table through the wrong port hangs the engine; missing EOP or a live `CTL_FREQ` in the snapshot hangs it; single-rank SR exit after a timeout; refusal with ranks in SR |
| DFC table: one rank, 16 GiB | Rank-byte rewrite; MR21 insertion and shifting |
| hardware DFC | Register words of fp1/fp2 kicks, no table → timeout, wrong landing bank → `-EIO` |
| full boot 8 GiB x8 2 CS 2400 | 4 passes, 3 DFCs, x8 reset once, 18 MR writes, 8 ZQ commands, full order, final register state |
| full boot 4 GiB x16 1 CS 2666 | No reset, 2666 PLL words, single-rank commands, CS1 not present |
| full boot at every data rate | Final fp and CLKSEL for 1200, 1066, 2133, 2400, 1600, 0, 2667, 2700, 3200, 4266 |
| full boot 16 GiB adds MR21 | Map, MR21 in both flows, MR21 written after the last pass and before the final DFC |
| full boot: first MRR after DFC(2) | Discarded read absorbs the zero; the check passes; a stale bank or an engine that leaves MR14 alone produces the warning |
| boot failures stop at their step | PLL, DFI, JEDEC init, gate, unsupported MR8, stuck DFC (DRAM left out of SR at fp0), bad arguments, LPDDR4 |

Acceptance (`test_accept.c`): each range of
[acceptance-and-logging.md](acceptance-and-logging.md) in and out of
range, with the exact message, and the printing helpers.

## Error-injection builds (`test_exp.c`, `make host-test-exp`)

Each of `K1_EXP_PLL_NO_LOCK`, `K1_EXP_GATE_TIMEOUT` and `K1_EXP_DFC_TIMEOUT` is
compiled into its own host binary with `test_exp.c`, which runs a full boot
against the model and checks the return code, the failing step, the number of
passes and the log lines. The regular suite must not define these flags; the
same flags are what the validation vehicle builds into images for the board
(`EXP=…`, [validation-vehicle.md](validation-vehicle.md)).

| Flag | Expected |
|---|---|
| `K1_EXP_PLL_NO_LOCK` | `-ETIMEDOUT`, step `pll-lock`, 0 passes, no MC write |
| `K1_EXP_GATE_TIMEOUT` | `-EIO`, step `training`, 1 pass failing in `read-gate`, no DFC |
| `K1_EXP_DFC_TIMEOUT` | `-ETIMEDOUT`, step `dfc`, 2 passes, "DFC to fp1 not done" |

## Mutation testing (`mutate.py`)

122 mutants, each a `(name, file, old text, new text)` substitution in one
source file. For each mutant the runner copies `src/` and `tools/` to a
temporary directory, applies the substitution under `src/`, and runs
`make test` there (with `WARN=` so a mutant that only triggers a warning still
builds). A mutant is *killed* when the suite fails; the run succeeds only if
all are killed.

```
python3 tools/test/mutate.py            # all mutants, parallel (-j N)
python3 tools/test/mutate.py --check    # only verify each anchor occurs once
python3 tools/test/mutate.py pll        # mutants whose name contains "pll"
```

Mutant groups (count): `train` 14, `phy` 14, `boot` 13, `dram` 8, `dfc` 8,
`pll` 7, `mc` 7, `tbl` 7, `swfc` 5, `map` 4, gate leftover logic 2, `mr` 2,
`reset` 2, `geom` 2, `check` 2, `init` 1, `final fp` 1, `dummy` 1, and 22 on
the DFC table and kick (port and window, snapshot flags and tail, `CTL_FREQ`
handling, kick guards, SR exit after a timeout). Examples: `>=` instead of `>` in max-min, no
MR14 command, 0-based rank in `PI_CMD`, RL bit not cleared, gate timeout
ignored, only VREF0 written, map applied before the x8 re-init, the final DFC
skipped, 2666 rounded for higher rates, the discarded MRR removed.

Every anchor must match exactly once (`--check`); refactoring a mutated line
requires updating its mutant.

## Table check (`dump_tables.c`, `check_tables.py`)

`dump_tables` prints every table fact ([tables.md](tables.md)).
`check_tables.py <dump_tables> <spec dir>` extracts the same facts from the
specification source (reStructuredText simple tables and code blocks of the
specification revision the code was written against, ignoring implementer
notes), and reports facts present only in the code, only in the
specification, or duplicated. `make check-tables` needs `SPEC_DOCS` pointing to
that source; the parser does not read the Markdown in `docsClean/`.

## Adding a test

1. Put the test function in `test_main.c` (training) or `test_boot.c`
   (bring-up), declare it in `check.h` if it lives in `test_boot.c`, and add it
   to the table in `main()`.
2. Start from `model_reset()` or `model_reset_por()`; set knobs; run; assert
   with `CHECK*`; end with `CHECK_CLEAN()`.
3. If the test pins a behaviour that no other test catches, add a mutant for
   it and check it is killed.

---

[← Previous: Acceptance Checks and Logging](acceptance-and-logging.md) | [Index](README.md#index) | [Next: Validation Vehicle →](validation-vehicle.md)
