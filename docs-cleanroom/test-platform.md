<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 Thalles Campagnani -->

[← Previous: Acceptance Criteria and Reference Measurements](reference-measurements.md) | [Index](README.md#index)

# Test Platform: FDL, Software Resets, BootROM Media, Probes

This file describes the hardware facts behind the experimental setup used to
discover everything else: how images reach the SoC, how the board can be reset
by software, where the BootROM looks for the first stage, and how the real DRAM
clock can be measured. None of it is needed by `ddr_init` itself.

## FDL: BootROM USB download

The K1 BootROM tries the boot media and, when they fail (or when the FDL button
is held at reset), enters a USB download mode that speaks the fastboot
protocol. A signed first-stage image is pushed and started with:

```
fastboot stage <signed-first-stage.bin>
fastboot continue
```

The image runs from SRAM exactly as if it had been loaded from a boot medium.
An experiment image is the mainline SPL with a DDR image in the `ddr_fw.bin`
slot (the `u-boot-spl-ddr.bin` of [boot-interface.md](boot-interface.md)), signed with the
vendor signing tool.

Observations useful for automation:

- The BootROM prints `usb download handler` on the UART when it is in FDL;
  that string is a reliable "ready for the next run" signal.
- `fastboot devices` may still list a stale gadget after the SPL starts, so it
  is not a reliable readiness signal.
- UART: 115200 baud.
- Without a board EEPROM read (common in FDL), the mainline SPL prints
  `Fail to detect board:-2` and uses 2 ranks / 2400 MT/s.
- If the strap selects SPI NOR and none is fitted, the mainline SPL stops with
  `Unsupported Boot Device` after DDR init — harmless for DDR experiments.

### Boot sequence printed by the BootROM

| Reset source | `sys` value printed | Path taken |
|---|---|---|
| RST button | `0x0` | `bm:3` (SD, fails) → `bm:0` (eMMC; `invalid bootinfo magic code:0x0` when empty) → FDL |
| FDL button | `0x1000` | `bm:2` → download mode directly |
| PMIC P1 soft reset | `0x0` (8 of 8) | same as the RST button |
| SoC watchdog | `0x0` (1 of 1) | same as the RST button |

`sys` reflects the boot flag register `0xd4282d10`. The mainline SPL writes its
own boot mode there (`0x55a` USB … `0x55e` SD); after an image loaded through
FDL it reads `0x0000055a`. A PMIC reset clears it.

## Software reset of the SoC (watchdog, MMIO only)

Works without I2C.

| Block | Address | Role |
|---|---|---|
| MPMU | `0xd4050000` | Clocks / reset of the watchdog block |
| WDT | `0xd4080000` | Watchdog timer |
| Start | `0xd4051020` | bit 4 starts the counter |

`MPMU+0x200` (WDTPCR): bit 0 = APB clock of the WDT, bit 1 = timer clock
(25.6 MHz / 100 → 256 Hz count), bit 2 = block reset (1 = held in reset; clear
it for the MMIO to respond).

Every write to the WDT must be **immediately** preceded by the unlock pair:

```
WDT+0xB0 = 0xbaba
WDT+0xB4 = 0xeb10
WDT+off  = value
```

| Offset | Value | Role |
|---|---|---|
| `+0xC0` | 0 | Clear status |
| `+0xBC` | N | Timeout in ticks of the 256 Hz clock (1 ≈ 4 ms; 0 fires at once) |
| `+0xB8` | 3 | Enable |
| `+0xC8` | 1 | Timeout causes a SoC reset |

Sequence (does not return):

```
MPMU+0x200 |= 0x3            /* clocks */
MPMU+0x200 &= ~0x4           /* release block reset */
unlock; WDT+0xC0 = 0
unlock; WDT+0xBC = 1
unlock; WDT+0xB8 = 3
unlock; WDT+0xC8 = 1
*(0xd4051020) |= (1 << 4)    /* start */
loop forever
```

If the WDT registers always read 0, its clocks/reset are not set correctly.
Clearing `0xd4282d10` before arming makes the next `sys` value reflect only the
watchdog.

## Board reset through the PMIC (closest to the RST button)

SpacemiT P1 on **I2C bus 8**, address **`0x41`** (BPI-F3 / `k1-x_deb1`). Register
numbers from the public P1 map (the same `P1_*` names as in U-Boot):

| Register | Bit | Role |
|---|---|---|
| `0x44` | 0 | WDT_CTRL: W1C status |
| `0x44` | 1–2 | Timeout (0 = 1 s … 3 = 16 s) |
| `0x44` | 3 | WDT enable |
| `0x7C` | 7 | PWR_CTRL0: the PMIC watchdog causes a reset |
| `0x7E` | 1 | PWR_CTRL2: **SW_RST** (mask `0x02`) |
| `0xAB` | 2 | ALIVE: reboot flag (mask `0x04`) |

Immediate reset:

```
P1[0xAB] |= 0x04      /* reboot flag, not "charging" */
P1[0x7E] |= 0x02      /* SW_RST; does not return, ≈100 ms to the BootROM */
```

The rails go down and up: this matches the RST button (`sys: 0x0`). The FDL
button (`sys: 0x1000`) is a strap and cannot be reproduced by software.

Measured: `P1[0xAB]` already reads `0xf4` (bit 2 set), `P1[0x7E]` reads 0. At
the point where the SPL runs, I2C8 and its pin mux are often not configured
yet; the SoC watchdog works without them.

## Where the BootROM looks on eMMC

- The BootROM reads `bootinfo` (80 bytes, magic `f01407b0`) at LBA 0 of the
  eMMC `boot0` partition. On the board: `spl0_offset = 0x200`,
  `spl1_offset = 0` (backup copy at LBA 0 of `boot1`),
  `spl_size_limit = 0x36000`.
- The first stage must therefore sit at **LBA 1 of `boot0`** (offset `0x200`).
  An image at 64 KiB or 128 KiB is rejected (`Failed to verify spl0/spl1`) and
  the BootROM falls back to FDL.
- A signed first-stage image at that offset passes the BootROM signature check
  (TPKS) and runs from SRAM exactly as an FDL-loaded one.

## DRAM latency probe (frequency meter)

Because the PLL block has no frequency counter, the real DRAM clock is measured
by latency:

- build a cyclic chain of 64-byte nodes at `0x10000000`: random order
  (Sattolo's algorithm) over 16 KiB, 256 KiB and 32 MiB, and one 32 MiB chain
  in address order;
- run 2²⁰ dependent loads per chain and time them with `mtime`
  (`0xe400bff8`, 24 MHz) and the cycle counter;
- report ns per load and the CPU frequency (`cycle / mtime`).

With the data cache off the CPU runs at 307 MHz in the SPL and the cache does
not filter (all sizes cost the same); with the data cache on (1599 MHz) use the
random 32 MiB chain (sequential is hidden by the prefetcher, 16 KiB fits in L1).
A PLL2-based point (fp1) is the control: it must not change between PLL1
recipes. Results in [reference-measurements.md](reference-measurements.md).

## Gaps

- The BootROM's FDL protocol beyond `stage` / `continue` and the image
  signature format were not studied (the vendor tool was only executed).
- The PMIC register map is taken from the public P1 description; only the bits
  above were exercised.

---

[← Previous: Acceptance Criteria and Reference Measurements](reference-measurements.md) | [Index](README.md#index)
