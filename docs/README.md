<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 Thalles Campagnani -->

# Implementation Documentation

Documentation of the DDR firmware in [`../src/`](../src/) and of the helper
code in [`../tools/`](../tools/): how the code is organized, what each module
does, the design decisions behind it, how it is built and tested, and what was
learned while writing it.

Everything in this directory is derived from the source code. It describes the
implementation; the hardware itself is described in the clean-room
specification.

## Rules for this directory

- **New discoveries made during implementation go here**, not in
  [`../docsClean/`](../docsClean/). The clean-room
  specification holds only hardware facts gathered by the analysis; anything
  learned while writing, running or debugging the code is recorded in this
  directory, so the two sources stay distinguishable.
- Information flows one way: the implementation reads `docsClean/`;
  nothing from the implementation is copied back into it.
- Helper code that is not part of the firmware blob belongs in
  [`../tools/`](../tools/), not in `../src/`.

## Index

| # | File | Topic |
|---|---|---|
| 01 | [architecture.md](architecture.md) | Layers, modules, data types, error model, design principles |
| 02 | [build-and-image.md](build-and-image.md) | Makefile targets, compiler flags, the raw image, its header, size and stack budget |
| 03 | [entry-points.md](entry-points.md) | `k1_blob_ddr_init` (the mainline ABI) and `k1_blob_retrain`; the runtime glue in `blob/` |
| 04 | [boot-sequence.md](boot-sequence.md) | `k1_ddr_boot()`: the order of PLL, Device_Init, passes, map, DFC; the boot report |
| 05 | [controller-init.md](controller-init.md) | `k1_ddr_init.c`: PLL and SW-FC, MC programming, PHY analog, JEDEC start, ZQ, MC reset, address map |
| 06 | [dfc.md](dfc.md) | Loading the DFC table and kicking a hardware frequency change |
| 07 | [training-pass.md](training-pass.md) | `k1_ddrt.c`: one training pass, the PI wrapper, every stage, the result structure |
| 08 | [tables.md](tables.md) | `k1_ddr_tables.c`: the data tables and how they are consumed and verified |
| 09 | [acceptance-and-logging.md](acceptance-and-logging.md) | Range checks on the trained values, the UART output and its message prefixes |
| 10 | [testing.md](testing.md) | The behavioural model, the test suites, error-path builds, mutation testing, table checks |
| 11 | [validation-vehicle.md](validation-vehicle.md) | Running the firmware on a board: signed images, FDL loop, U-Boot hooks, host scripts |
| 12 | [implementation-notes.md](implementation-notes.md) | Findings, limitations and open points discovered in the implementation |

Suggested reading order: the first four entries of the index, then the module
you are working on.
Before changing code, read [testing.md](testing.md): every change is
expected to keep `make host-test`, `make test` and `make mutate` green.
