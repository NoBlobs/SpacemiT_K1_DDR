<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 Thalles Campagnani -->

[Index](README.md#index) | [Next: Glossary →](glossary.md)

# Methodology

## Clean-room approach

The work follows a clean-room split between two roles.

**Analyst.** Writes the specification. Every fact enters the specification as
either

- behaviour of the **silicon** (a register, a field, an encoding, a wait
  condition, a side effect): *programming X produces Y*; or
- a **hardware requirement** of the initialisation (a step, its order relative
  to other steps, the value the hardware needs, how to verify the result).

A fact is never written as "the other firmware does X", nor as "our code does
X". Opaque values needed by the hardware (for example the PI sequencer
parameters) are listed as engine parameters — *reset value X, the hardware
needs Y* — without claiming to know what the bits mean.

**Implementer.** Whoever writes DDR firmware from this specification uses only

- this specification;
- public documents: JEDEC JESD209-4 (LPDDR4) and JESD209-4-1 (LPDDR4X), the
  datasheet of the DRAM part on the board, and the public SpacemiT K1
  datasheet / manuals where available;
- measurements taken on the board.

The implementer does not read listings, decompilations, linker maps, strings or
source code of the program that originally initialised this SoC, and does not
transcribe function names, log strings, stack layouts or code addresses of
another program. Programming the **silicon's registers** described here is not
copying: it is the hardware.

Rules derived from this:

1. If a step is not in the specification, the implementer stops and asks for an
   amendment instead of guessing from memory of other code.
2. New measurements (UART, MMIO reads) are welcome input to the specification;
   source code of another DDR training implementation is not.
3. The specification describes hardware only. It contains no information about
   any particular implementation (its structure, code, timings or results).
4. Running new firmware on the board and comparing the hardware state with the
   reference measurements ([reference-measurements.md](reference-measurements.md))
   does not break the clean room. Comparing a new binary with another binary
   byte by byte would defeat the purpose: new code does **not** have to be
   identical to anything. Equivalence is defined by the **hardware state**
   reached (status registers, trained values within the reference ranges,
   DRAM integrity), not by code.

## Trial and error through FDL

The K1 BootROM has a USB download mode (FDL, *firmware download*). When the
boot media fail, or when the FDL button is held at reset, the BootROM exposes a
fastboot-compatible USB device that accepts a signed first-stage image, places
it in SRAM and runs it. This is the tool that made the reverse engineering
possible without any boot medium and without risk of bricking the board.

The experimental loop:

1. Build a first-stage image (mainline U-Boot SPL plus a DDR image in the
   `ddr_fw.bin` slot) that programs the registers under test and dumps the
   resulting state over the UART.
2. Sign it with the vendor signing tool (the BootROM verifies the image; the
   tool is only executed, never studied).
3. Load and run it through FDL: `fastboot stage <image>` then
   `fastboot continue`.
4. Capture the UART (115200 baud) and compare the observed state with the
   hypothesis.
5. Reset the board by software (SoC watchdog or PMIC soft reset,
   [test-platform.md](test-platform.md)); the BootROM falls back to FDL
   by itself and the next run starts. Runs can alternate between two variants
   (A/B) with nobody at the board.

Kinds of experiments that produced the facts in this specification:

- **Register dumps** before and after each step (PMU, MC STATUS, `CTL_FREQ`,
  `PI_CFG`, `PI_STATUS`, PHY status, taps, Vref words, mode registers).
- **Ordering and omission tests**: removing or moving a step and observing
  what the hardware does (for example the DFC engine hanging when its table is
  incomplete).
- **Encoding probes**: changing one field and watching the effect (CS size
  codes, PLL words, command formats).
- **Fault experiments**: deliberately leaving a precondition unmet (PLL
  enables cleared, a read-gate wait without a command, a DFC kick without a
  table) to see how the hardware reports it.
- **A/B comparisons** between two ways of programming a step (for example
  re-writing MR14 after a frequency change, which turned out to change
  nothing).
- **Latency probes** as a frequency meter, because the PLL block has no
  frequency counter (pointer-chasing loads timed by `mtime`).
- **DRAM integrity checks**: pattern tests and address markers across the whole
  8 GiB, and execution of code from DRAM.

## Confidence labels

Used throughout the documents (see also the [README](README.md)):

| Label | Meaning |
|---|---|
| *measured* | Observed on the reference board. |
| *required* | Needed by the hardware: omitting it was observed to break the initialisation, or the hardware was only observed to reach a working state with it. |
| *inferred* | Consistent with all observations and with JEDEC, but not isolated by a dedicated measurement. |
| *opaque* | Value known and required; meaning of the bits unknown. Write it as given. |
| *unknown* | Not established. Preserve hardware values; do not invent. |

## Limits of the method

- The **PI training sequencer** is treated as a black box. The information
  gathered is sufficient to drive it to a trained state (trigger the engine with
  the right parameters, read results, pick the Vref, re-apply). It is **not**
  sufficient for a training that is independent of the PI (sweeping delays by
  hand, detecting edges, replacing the sequencer): that would need RTL or
  engine documentation.
- Power-on-reset values of several PMU registers cannot be observed: by the
  time the DDR image runs, the BootROM and SPL have already touched them.
  Values *at entry* to `ddr_init` are measured and documented instead.
- Only one board (BPI-F3, 8 GiB, Samsung LPDDR4X x8, 2 CS) was available.
  Other configurations (1 CS, x16, 16 GiB, LPDDR4 non-X, other DRAM vendors)
  are derived from command formats and encodings but not measured; each topic
  marks them as such.

---

[Index](README.md#index) | [Next: Glossary →](glossary.md)
