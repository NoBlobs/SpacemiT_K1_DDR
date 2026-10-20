<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 Thalles Campagnani -->

[← Previous: Eye Training](eye-training.md) | [Index](README.md#index) | [Next: The Complete `ddr_init` Sequence →](boot-sequence.md)

# CA Training (Engine Present, Unused by the LPDDR4X Boot)

The LPDDR4X boot on this SoC does **not** run CA training. Vref(CA) comes from
the init (`ddr+0x30c[31:24] = 0x9D`, MR12 = `0x5d` on the reference board).
`PI_CA0` arrives from the MC as `0x055d74ff`; the LPDDR4X pass does not write it.

In the LPDDR4X boot: do not turn the CA port on through MISC, do not issue the
CA operation and do not change the PLL inside a training pass.

## Hardware

The engine exists. It should only be enabled if a concrete product fails CA.
LPDDR4 (non-X) boards may need it ([configurations.md](configurations.md)).
The recipe below is complete as a sequence, but it has **not been exercised**
on hardware, and the bit fields of `PI_CA0/1` beyond the packing shown are
*unknown*.

```
CTL_FREQ[31:30] = boot_pp
MISC (ddr+0x28)  = 0x13010000                 /* CA on */
for n in {13, 1, 2, 3, 11, 12, 14, 22}:
    CA_CMD = 0x13020000 | n                   /* MRW MR13, 1, 2, 3, 11, 12, 14, 22 */
PI_CA0 = 255 | (20 << 8) | (56 << 13) | (56 << 19) | (1 << 26)
PI_CA1 = 25 | (1 << 7) | (15 << 8) | (59 << 26)
for cs in 1 .. cs_num:
    PI_CA0[31:28] = cs
    for pass in 0, 1:
        phy+0x18004 = 0x1008870 | pass        /* CA_STAT */
        PI_CMD = 0x10800000 | (cs << 24)
        apply_freq_change(boot_pp, wait)
        PI_CA0 ^= (1 << 27)
        apply_freq_change(4 /* external */, wait)
        PI_CA0 ^= (1 << 27)
        wait PI_STATUS bit 24
MISC   = 0x13008000                           /* CA off */
CA_CMD = 0x13020000 | 13                      /* MRW MR13 */
apply_freq_change(boot_pp, no wait)
```

## The CA-path frequency change (`apply_freq_change`)

This path changes the PLL **inside** training, using the SW-FC kick and the PI
PROG/KICK operations. It is a different mechanism from the HW-DFC used between
passes ([dfc-engine.md](dfc-engine.md)):

```
CTL_FREQ[29:28] = (sel == 4) ? 0 : sel        /* [31:30] stays boot_pp: mask 0x3fffffff */
PI_CFG[3:2]     = sel
PI_CMD = 0x13000002 (PROG);  wait PI_STATUS bit 29
pmu+0x3b4 = clksel[sel]                       /* sel 4 = external clock, 0x3b02 */
pmu+0xb0  = (sel == 1) ? 0x40600600 : 0x40600400
pmu+0x04  |= (1 << 26);      wait bit 26 clear
PI_CMD = 0x13000004 (KICK);  wait PI_STATUS bit 30
```

Here, unlike the normal boot, software writes `CTL_FREQ[29:28]` and `PI_CFG`
**before** PROG. `0x40600600` is used only on this path; the normal boot never
writes it.

## Gaps

- Bit fields of `PI_CA0` and `PI_CA1`.
- The CA-training sequence has not been exercised on silicon; its success
  criteria (CA_STAT bits, resulting CAPHY delays) are not measured.
- Whether LPDDR4 boards on the K1 require CA training.
- Meaning of the `PI_CA0` bit 27 toggling between the two frequency changes.

---

[← Previous: Eye Training](eye-training.md) | [Index](README.md#index) | [Next: The Complete `ddr_init` Sequence →](boot-sequence.md)
