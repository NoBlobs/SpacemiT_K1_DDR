<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 Thalles Campagnani -->

[← Previous: The PI Training Engine](pi-training-engine.md) | [Index](README.md#index) | [Next: Write Leveling →](write-leveling.md)

# One Training Pass

A **training pass** centres the data eye for **one** frequency point that is
already running (`boot_pp`). The boot runs several passes, one per frequency
point (and fp0 twice); the orchestration is in
[boot-sequence.md](boot-sequence.md).

After Device_Init the bus works but the eye is not centred:

- DQS is not levelled with CK;
- the PHY DQS gate is not inside the read burst window;
- the read Vref (PHY) and write Vref (die, MR14) are not at the best step for
  the rate.

At 2400/2666 MT/s the eye is closed without this. Device_Init does not do it.

## Inputs

| Input | Values | Role |
|---|---|---|
| `ddr` | MC base | `0xc0000000` |
| `cs_num` | 1 or 2 | Ranks actually populated |
| `boot_pp` | 0..3 | The frequency point / PHY slice already active |
| log | optional | Text output in SRAM; may be absent |

The pass must not depend on a caller buffer and must not read the
`0xc0800000` block.

## Outputs

For the frequency point `boot_pp`:

- write-leveling taps per byte and rank (`byte + 0x24`);
- read-gate code per byte and rank (`byte + 0x70`);
- RX DQ taps (5 bits) and TX DQ/DM taps (6 bits);
- the PHY Rx Vref index in VREF0/VREF1 of the slice;
- the DRAM Vref(DQ) (MR14), equal to `CTL_TXVREF[21:16]`;
- the return value: `PHY_STATUS` (`phy+0x18000`).

Each slice keeps the taps of **its** rate. Training only the final rate leaves
the other frequency points with reset delays (`0xf`).

## What a pass does not do

- It does not touch the PLL and does not choose the rate.
- It does not program ODT, MR1–MR3, timings or the address map.
- It does not write `CTL_FREQ` or `PI_CFG`.
- It does not use DRAM for code, stack or buffers.
- It does not implement edge search itself (that is the PI's job).
- It does not read the device tree.

## Preconditions

Before entering SR and triggering the PI:

1. `dclk` stable at the rate of `boot_pp` (SW-FC for the first fp0 pass,
   HW-DFC for the others).
2. `CTL_FREQ[31:30] = boot_pp` and `[29:28] = sel` (equal in the normal boot).
3. `PI_CFG[3:2] = boot_pp` (set by init/DFC, never by the pass).
4. The PHY slice `phy + (boot_pp << 14)` is the active one.
5. MC, PHY and mode registers programmed by Device_Init.
6. `cs_num` equals the number of real ranks.
7. The DRAM is out of SR (STATUS SR bits clear).
8. After a HW-DFC, mode-register reads (if any are done before the pass)
   start with a dummy MRR ([mode-registers.md](mode-registers.md)).

If any of these fails, triggering the PI is pointless. The PI does not choose
the frequency. `cs_num = 1` on a two-rank package leaves CS1 with reset delays.

## Mandatory sequence

```
enter SR (both ranks)
PHY_INIT = 512
[CA training only if a product requires it — the LPDDR4X boot does not]
write_leveling        per rank
read_gate             per rank; a timeout fails the boot
read_training         sweep PHY Rx Vref 0..15; max-min; re-apply winner
write_training        sweep MR14 21..36;      max-min; re-apply winner
st = PHY_STATUS
exit SR
return st
```

### Why this order

| Rule | Reason |
|---|---|
| SR first | Without SR a CPU access can collide with the PI. |
| `PHY_INIT = 512` next | Arms the training FSM for this pass. |
| WL before the gate | The gate measures DQS relative to a DQS already aligned with CK. |
| Gate before read training | Without a gate, read training gets margin 0 or times out. |
| Read before write | Rx Vref (PHY) and Tx Vref (die) are independent, but the PI's CFG/STATUS leftovers on this SoC expect this order. |
| Re-apply the winning Vref while still in SR | The last sweep step would otherwise leave the wrong taps in the PHY. |
| Exit SR at the end | The boot needs the DRAM for MR reads, the DFC and the next pass. |

### Forbidden

- Reordering the stages.
- Training two frequency points at once.
- Leaving SR in the middle of a sweep.
- Initialising MC/PHY/PLL inside a pass (that belongs to `ddr_init`
  between passes).
- Reading or writing DRAM during the pass.
- Forcing taps to "nice" values in software.
- Replacing the PI with a home-made delay sweep (impossible without the RTL).

## Error handling

`PHY_STATUS` (`phy+0x18000`):

| Bit | Meaning |
|---|---|
| 1 | Write leveling failed |
| 2 | Read leveling failed |
| 3 | Write DQ failed |
| 4 | Read gate failed |

- The boot must **fail** if `PHY_STATUS != 0` at the end of the pass, or if the
  gate times out. Continuing with a bad DRAM is worse than stopping.
- `PI_STATUS = 0x80008067` at the end is not an error: those are sticky done
  bits plus the idle bits 15/31.
- Every PI wait needs a software timeout ([pi-training-engine.md](pi-training-engine.md)).
- SR: never wait for STATUS `0x33` after the exit; STATUS `0x77` at the end
  means the pass is still in SR ([self-refresh.md](self-refresh.md)).

Things not to do:

- poll `PI_CMD` (it reads 0);
- wait for `PI_STATUS` to equal an absolute value;
- assume a leftover makes a wait trivially satisfied;
- address the harvest with an absolute `0xc0058xxx`;
- write the Vref of slice N at the offsets of slice N+1;
- train only the final rate;
- skip the second fp0 pass;
- treat PI_STATUS bits 15/31 as errors.

## Working memory

Code, stack and scratch are in SRAM. The minimum scratch is 16 minimum values
for the current sweep and 32 margins per rank for the current step: the whole
sweep need not be stored if the minimum is updated on the fly and the re-kick
uses the winning step. Zero the scratch at the start of the pass.

## Pseudocode

Names are this specification's own. Offsets from [mmio-map.md](mmio-map.md)
and [phy.md](phy.md).

```python
def pack_vref(step):
    return (step << 28) | (step << 24) | (step << 20) | (step << 16)

def pi_op(ddr, op, cs, done):
    writel(ddr + 0x13d0, op | (cs << 24))
    t0 = now()
    while (readl(ddr + 0x13fc) & done) != done:
        if now() - t0 > PI_TIMEOUT:
            fail("PI timeout")
    return readl(ddr + 0x13fc)

def max_min(min_per_step):
    best = 0
    for step in range(1, 16):
        if min_per_step[step] > min_per_step[best]:   # strict
            best = step
    return best

def training_pass(ddr, cs_num, boot_pp):
    phy   = ddr + 0x40000
    slice = phy + (boot_pp << 14)

    sr_enter(ddr, cs_num)                       # self-refresh.md
    writel(phy + 0x10028, 512)                  # PHY_INIT

    # write leveling
    writel(ddr + 0x13e8, 0x0a641e44)
    for cs in range(1, cs_num + 1):
        pi_op(ddr, 0x10400000, cs, done=0x1)

    # read gate (timeout detection: read-gate.md)
    for cs in range(1, cs_num + 1):
        read_gate(ddr, phy, cs)

    # read training: PHY Rx Vref sweep
    v0, v1 = slice + 0x3004, slice + 0x3204
    writel(v0, readl(v0) & ~0x10)
    writel(ddr + 0x13ec, 0x00005070)
    rx_min = [0] * 16
    for step in range(16):
        v = (readl(v0) & 0xffff) | pack_vref(step)
        writel(v0, v); writel(v1, v)
        rx_min[step] = INF
        for cs in range(1, cs_num + 1):
            pi_op(ddr, 0x10200000, cs, done=0x6)
            m = read_harvest(phy + 0x18200)     # 32 DQ margins, right now
            rx_min[step] = min(rx_min[step], *m)
    writel(phy + 0x18000, readl(phy + 0x18000) & ~0x4)   # clear read-leveling error
    best = max_min(rx_min)
    v = (readl(v0) & 0xffff) | pack_vref(best)
    writel(v0, v); writel(v1, v)
    for cs in range(1, cs_num + 1):
        pi_op(ddr, 0x10200000, cs, done=0x6)    # leave the winner's taps
    writel(v0, readl(v0) | 0x10)

    # write training: DRAM Vref(DQ) sweep through MR14
    writel(ddr + 0x13f8, 0x00001810)
    seed = [21 + i for i in range(16)]
    tx_min = [0] * 16
    for i, vdq in enumerate(seed):
        rmw(ddr + 0x30c, 0xffc0ffff, vdq << 16)
        writel(ddr + 0x24, 0x1302000e)          # MRW MR14 (0x1102000e with 1 rank)
        tx_min[i] = INF
        for cs in range(1, cs_num + 1):
            pi_op(ddr, 0x10080000, cs, done=0x60)
            m = read_harvest(phy + 0x18300)
            tx_min[i] = min(tx_min[i], *m)
    best_vdq = seed[max_min(tx_min)]
    rmw(ddr + 0x30c, 0xffc0ffff, best_vdq << 16)
    writel(ddr + 0x24, 0x1302000e)
    for cs in range(1, cs_num + 1):
        pi_op(ddr, 0x10080000, cs, done=0x60)

    st = readl(phy + 0x18000)
    sr_exit(ddr, cs_num)
    return st                                   # nonzero -> fail the boot
```

`read_harvest(base)` reads 8 little-endian words, 4 DQ margins per word, and
must be called inside the rank loop. If all minimums are 0 in the write sweep,
the selection keeps index 0 by the strict rule; see
[eye-training.md](eye-training.md) for the fallback.

After the pass, outside SR: optionally read MR14 on every (rank, channel) and
check it equals `CTL_TXVREF[21:16]` (after a dummy MRR if a DFC happened since
the last read).

## Gaps

- The order constraint "read before write" is required by observed engine
  state, not explained.
- Whether a pass can safely skip SR (the engine works without it in some
  states, [self-refresh.md](self-refresh.md)) is not established; the
  boot always uses SR.

---

[← Previous: The PI Training Engine](pi-training-engine.md) | [Index](README.md#index) | [Next: Write Leveling →](write-leveling.md)
