<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 Thalles Campagnani -->

[← Previous: Write Leveling](write-leveling.md) | [Index](README.md#index) | [Next: Eye Training →](eye-training.md)

# DQS Read Gate

## Goal

Find, per byte and per rank, the window during which the PHY opens the DQS
gate on reads. The result is the gate code at `byte + 0x70`
([phy.md](phy.md)).

## Recipe

```
for cs in 1 .. cs_num:
    PI_CMD = 0x10100000 | (cs << 24)
    snap = PI_STATUS                         /* first read after the command */
    wait (PI_STATUS & 0x2) == 0x2            /* software timeout -> fail */
    st = PI_STATUS
    if (snap & 0x4) == 0:
        /* bit 2 was clear: the classic test is reliable */
        if (st & 6) == 6:  PASS
        elif (st & 6) == 2: TIMEOUT -> initialisation failed
        else:               fail
    else:
        /* bit 2 left over from an earlier read-training operation:
           (st & 6) == 6 is not conclusive; rely on PHY_STATUS bit 4 */
        if PHY_STATUS & (1 << 4): initialisation failed
```

There is no separate CFG register for the gate stage.

## Why the timeout test needs care

- The RGATE command clears **only bit 1** of `PI_STATUS` at issue (the same
  rule as WL clearing only bit 0).
- Bit 2 is the second done bit of the RD (read-training) operation. A leftover
  `0x80008067` from a previous pass keeps it set.
- Therefore:
  - on the first fp0 pass after Device_Init (idle `0x80008000`), bit 2 is 0:
    `(st & 6) == 2` **is** a timeout and `== 6` is a pass;
  - after any read training or DFC, bit 2 is residual: `(st & 6) == 6` holds
    even on a timeout. Use `phy+0x18000` bit 4 (gate error) and treat the
    initialisation as failed if it is set.

The wait itself must have a software timeout. Measured: when the done bit is
awaited without issuing the RGATE command, bit 1 never sets and `PI_STATUS`
stays at `0x80008001` (only the WL done bit from the previous stage). The done
bit is only produced by an issued operation. A timeout of an RGATE that was
actually issued has never been observed: on this board the operation always
completes in microseconds.

Ignoring a gate timeout leaves the gate in the wrong place.

## Gate code acceptance

```
GATE = (1 << 16) | (g << 8) | t
```

| When | Acceptance |
|---|---|
| Before the PI | `0x00000a00`: untrained default, not a valid result |
| Right after RGATE | bit 16 = 1 and the two low bytes equal (`g == t`) |
| End of the pass | bit 16 = 1 and both bytes in the trained range; `t` may differ from `g` by up to 2 (read training adjusts it, e.g. `0x00012625`, `0x00012423`) |

The PI writes the gate of all four bytes (sub-PHY A and B). Do not copy A to B.

## Typical values (reference board)

| Pass | Gate (byte value `g`) |
|---|---|
| First fp0 pass (cold PHY), 1200 MT/s | ≈105–118 taps (111–118 measured at a true 1200) |
| First fp0 pass with the 2666 VCO (fp0 ≈1333) | ≈104–110 |
| Second fp0 pass, after the x8 re-initialisation | ≈32–40 (32 = `0x20` is accepted) |
| fp1, 1066 MT/s | ≈32–38 |
| fp2, 2400 MT/s | ≈34–40 |

The cold first-pass value is not a criterion: it reflects the x16 timings
programmed on x8 dies before the byte-mode re-init. Do not compare the first
pass with the second or with fp2.

## Gaps

- Meaning of the low byte `t` and why read training moves it.
- Why the gate stage modifies `byte0/CS0 + 0x64` (`0x4349`).
- No example of a genuine RGATE timeout after a real command; the detection
  logic above covers it by construction, not by observation.

---

[← Previous: Write Leveling](write-leveling.md) | [Index](README.md#index) | [Next: Eye Training →](eye-training.md)
