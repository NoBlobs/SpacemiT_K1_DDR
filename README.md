<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 Thalles Campagnani -->

# SpacemiT K1 — Free DDR Initialization for Mainline U-Boot

This project aims to produces a free (GPLv3) DDR initialization firmware for the
**SpacemiT K1** RISC-V SoC: the code that, at boot, brings up the LPDDR4/4X
memory controller and PHY, trains the data eye and hands a usable RAM to the
boot loader.

Mainline U-Boot already supports the K1, but its first stage (SPL) cannot
initialize the RAM by itself: it embeds and calls a file named `ddr_fw.bin`,
and the only such file available so far is a **proprietary binary blob**
distributed by the SoC vendor. This project aims replaces that blob with free
software, using the same file name, load address and calling convention, so
that it drops into mainline U-Boot without changes:

```c
int ddr_init(u64 ddr_base, u32 cs_num, u32 data_rate, void (*puts)(const char *));
```

## Why a clean-room reverse engineering

The goal is a DDR firmware that is free software in its own right, not a
derivative of the vendor's program. To get there, the work follows a
**clean-room** process, which separates *knowing how the hardware behaves* from
*writing the code*:

1. **Hardware specification.** As much information as possible about how the
   hardware works was collected and written down as facts about the silicon:
   registers, bit fields, command protocols, wait conditions, ordering rules,
   required values, measured results and known gaps. Every fact is phrased as
   "programming X makes the hardware do Y". The specification never describes
   the code of any program.
2. **Implementation.** The source code is written from that specification and
   from public documents only (JEDEC JESD209-4 / JESD209-4-1, the DRAM
   datasheet, public SpacemiT documentation). The implementation does not use
   the vendor's binaries, disassembly, strings or source code.

Because the code is written from a description of the hardware instead of
from the original program, it does not copy the original's expression, and the
specification itself becomes a lasting, public reference for the K1 memory
subsystem.

The hardware facts were obtained by **trial and error on real hardware**, using
the board's **FDL** function (the BootROM's USB download mode, which runs a
first-stage image sent over USB): each hypothesis was turned into a small
experiment, loaded through FDL, and checked against UART output, register dumps
and DRAM tests. The reference hardware is a **Banana Pi BPI-F3 with 8 GB of
RAM** (Samsung **K4UCE3Q4AA-MGCL** LPDDR4X, two ranks, x8 dies).

## Repository layout

```
docsClean/               hardware specification (clean-room side, submodule)
src/                     DDR firmware source code (builds ddr_fw.bin)
docs/                    documentation of the implementation
tools/                   helper code that is not part of the firmware blob
LICENSE                  GNU GPL version 3
```

### `docsClean/` — what the hardware does

The maximum amount of information gathered about how the K1 hardware works,
consolidated in one place: system topology, MMIO map, clocks and PLLs, memory
controller, Device_Init, PHY, mode registers, self-refresh, address map, the
hardware frequency-change (DFC) engine, the PI training engine, every training
stage, the complete boot sequence, configuration variants, reference
measurements and the FDL test platform. Each topic ends with a **Gaps**
section listing what is still unknown. Start at
[docsClean/README.md](docsClean/README.md).

This directory contains **hardware facts only**. It must not contain
information about the implementation (its code, structure, timings or
results), so that it stays a clean source for any implementation.

### `src/` — the firmware

The source code that trains the RAM, written from the information in
`docsClean/`. It builds the raw image that mainline U-Boot loads as
`ddr_fw.bin`.

### `docs/` — documentation of the implementation

How the implementation is organized and why. **New discoveries made while
implementing must be written here**, in this separate documentation — not in
`docsClean/`. This keeps the specification free of implementation details
and makes clear which knowledge came from the hardware analysis and which came
from writing and testing the code.

### `tools/` — helper code outside the blob

Any code created to help the implementation that is **not part of the
firmware blob** goes here:

- `test/` — a model of the PI training engine and the controller, the test
  suites run natively and on RISC-V under QEMU, a mutation-testing runner and a
  checker that compares the programming tables with the specification;
- `vehicle/` — the validation setup: scripts that build signed first-stage
  images, load them through FDL, drive the UART, reset the board by software,
  and a patch with the validation hooks for mainline U-Boot's
  `board/spacemit/k1`.

### Information flow

```
hardware ──(FDL experiments)──> docsClean ──> src
                                               │
                        docs  <── new findings ┘
                        tools <── helper code
```

The flow is one-way: the implementation reads the specification; nothing from
the implementation is copied back into `docsClean/`.

## Building

Requirements: a RISC-V bare-metal GCC (`riscv64-elf-gcc`) for the firmware;
for the tests also `riscv64-linux-gnu-gcc`, `qemu-riscv64`, a native `gcc` and
Python 3.

```sh
make -C src blob      # -> build/blob/k1_ddr_fw.bin
make -C src host-test # tests natively, with ASan/UBSan
make -C src test      # tests on rv64gc under qemu-riscv64
make -C src host-test-exp   # injected error paths, against the model
make -C src mutate    # mutation testing of the firmware sources
```

To use the firmware with mainline U-Boot, point the K1 build at it:

```sh
export DDR_FW_FILE=$PWD/build/blob/k1_ddr_fw.bin
make spacemit_k1_defconfig && make   # in the U-Boot tree -> u-boot-spl-ddr.bin
```

The image is LPDDR4X with Tx ODT 80 Ω (the SPL's default memory type); the
mainline ABI does not pass the memory type, so LPDDR4 boards need a separate
build. Building signed images for FDL tests (`tools/vehicle/`)
additionally needs a mainline U-Boot checkout (`UBOOT=`) and the vendor U-Boot
tree that holds the signing tool (`VENDOR_UBOOT=`), which is only executed.

## Status

- Reference board (BPI-F3, 8 GiB Samsung LPDDR4X, 2 ranks, x8): independent
  WL+Vref+delay is measured on both ranks at SPL `data_rate` 1066, 1200, 1333,
  2400 and 2666 (`cs_num 2` from EEPROM/default; verify OK; about 3.3–3.4 s).
  Log 023 used the 2400 VCO at `RATE=1333`; 026 confirms the 2666 VCO
  (PLL1 `0x203c5555`, PI fp2 mins 9/8) at that request.
- 16 GiB Micron BPI-F3 (`MT53E4G32D8`, 2 ranks): independent write leveling,
  RX/TX Vref and delay training is measured on both ranks at every frequency
  point (verify OK; two-stage search about 3.2 s) at SPL `data_rate` 1066,
  1200, 1333, 2400 and 2666. `data_rate` 1333 programmes the 2666 PLL1 VCO
  and parks at fp0 (≈1333 MT/s; 026/027). Logs 015/023 used the 2400 VCO at
  that request. `ddr_init` returns an error if that verify
  fails. The DQS read-gate stays with the PI. `cs_num 1` on this two-rank
  package trains CS0 only (8 GiB mapped); a following PI pass does not keep
  DRAM at two mid-window addresses.
- 1-rank / x16 (4 GiB) and LPDDR4 (non-X) configurations are described from
  encodings and command formats but have not been measured on hardware.
- The PI training sequencer is used as a black box; its internal algorithm is
  unknown. The host tests drive the same independent-eye path against a
  geometric DRAM model.

See the Gaps sections in `docsClean/` for everything that is still open.

## License

Copyright (C) 2026 Thalles Campagnani

This project is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version. It is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See [LICENSE](LICENSE) for details.

Each file carries an `SPDX-License-Identifier` line. The U-Boot patch in
`tools/vehicle/u-boot/` is licensed GPL-2.0-or-later, like the
U-Boot code it modifies.

This project is not affiliated with or endorsed by SpacemiT, Banana Pi or
Samsung; product names belong to their owners. Programming DRAM controllers is
inherently hardware-specific — use at your own risk.
