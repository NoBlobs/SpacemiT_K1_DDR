<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 Thalles Campagnani -->

[← Previous: Testing](testing.md) | [Index](README.md#index) | [Next: Implementation Notes, Limitations and Open Points →](implementation-notes.md)

# Validation Vehicle (`tools/vehicle/`)

The *vehicle* runs the firmware on a real board in a loop, without a boot
medium and without anyone pressing buttons: it builds signed first-stage
images, pushes them through the BootROM's USB download mode (FDL), captures the
UART, and resets the board back into FDL by software for the next run.

```
build-vehicle.sh ──> signed FSBL ──fastboot stage/continue──> board (BootROM FDL)
                                                                 │ UART
fdl_run.py <──────────── log, summary ◄──────────────────────────┘
     │  answers the reset prompt (PMIC / watchdog) ──> board back in FDL
     └─ optional: ymodem U-Boot proper, commands at the U-Boot prompt
extract_log.py ──> one summary per log
```

## Components

| File | Kind | Role |
|---|---|---|
| `build-vehicle.sh` | shell | Builds the firmware, a patched U-Boot SPL and the signed FSBL |
| `u-boot/0001-board-spacemit-k1-validation-vehicle.patch` | patch for mainline U-Boot | Validation hooks in `board/spacemit/k1` |
| `fdl_run.py` | Python 3 + pyserial | Loads FSBLs through fastboot, captures the UART, drives resets and the U-Boot prompt |
| `ymodem.py` | Python 3 | Minimal YMODEM sender (1 KiB blocks, CRC-16) used by `fdl_run.py` |
| `emmc_tool.py` | Python 3 + pyserial | Drives the eMMC monitor built into the `emmc` image |
| `extract_log.py` | Python 3 | Summarizes vehicle logs |

## `build-vehicle.sh`

### Required environment

| Variable | Meaning |
|---|---|
| `UBOOT` | Mainline U-Boot checkout (validated at commit `8d534cec0cc`). It is copied to `$WORK/u-boot` and the patch is applied to the copy (skipped if it is already applied); the checkout itself is not modified |
| `VENDOR_UBOOT` | Vendor U-Boot tree holding the FSBL signing tool (`tools/build_binary_file.py`) and key configuration (`board/spacemit/k1-x/configs`). The tool is only executed, on a copy of its configuration |
| `OPENSBI` | `MODE=uboot` only: `fw_dynamic.bin` built with `FW_TEXT_START=0x0` |
| `DDR_FW` | `MODE=retrain` only: the proprietary `ddr_fw.bin` (default `build/vehicle/ddr_fw.bin`) |
| `WORK` | Scratch directory, default `~/k1-cleanroom-build`; must not contain spaces or colons (U-Boot's kbuild) |
| `CROSS_COMPILE` | Default `riscv64-linux-gnu-` |

### Modes

| `MODE=` | DDR firmware in the SPL | Then | Output |
|---|---|---|---|
| `retrain` (default) | Proprietary `ddr_fw.bin` | DRAM checks; the open firmware re-trains the running fp three times | `FSBL-gpl-test.bin` |
| `full` | Open firmware: everything from power-on | DRAM checks around one re-train round | `FSBL-gpl-full.bin` |
| `emmc` | Open firmware | As `full`, then the eMMC monitor on the UART | `FSBL-gpl-emmc.bin` |
| `uboot` | Open firmware | As `full`, then the SPL loads OpenSBI + U-Boot proper over ymodem | `FSBL-gpl-uboot.bin` + `u-boot.itb` |

### Options

| Variable | Effect | Output suffix |
|---|---|---|
| `EXP=K1_EXP_…` | Builds the firmware with that error-injection flag (own build directory) | `-exp-<flag>` |
| `RATE=<MT/s>` | The SPL passes this `data_rate` instead of the EEPROM's | `-<rate>` |
| `LAT=1` | DRAM latency measurement before `K1DDRT-DONE` | `-lat` |
| `RESET=` (empty) | No software reset at the end (default: reset on, except `emmc`/`uboot`) | — |

### Steps

1. `make -C src blob` (with `BLOB_DEFS=-D$EXP` when set).
2. Copy `$UBOOT` to `$WORK/u-boot` (rsync, without `.git`) and apply the
   patch.
3. Copy the DDR firmware for the SPL slot to `$WORK/ddr_fw.bin` and the open
   firmware to `$WORK/ddr_gpl.bin`.
4. `spacemit_k1_defconfig`; `emmc` adds `SPL_MMC_WRITE`; `uboot` adds
   `SPL_YMODEM_SUPPORT`, moves the SPL BSS to `0xc083f300` (2 KiB) with the
   early stack at `0xc083f200` (the ymodem state does not fit the default
   1 KiB BSS), and enables `CMD_MEMTEST`, `CMD_PMIC`, `CMD_I2C`, `CMD_SBI`,
   `CMD_TIME`, `CMD_CACHE` in U-Boot proper.
5. Build U-Boot with `DDR_FW_FILE`, `DDR_GPL_FILE`, `DDR_GPL_FULL`,
   `K1_EMMC_TOOL`, `K1_VEHICLE_RESET`, `K1_VEHICLE_UART`, `K1_VEHICLE_RATE`,
   `K1_VEHICLE_LAT`.
6. Sign `u-boot-spl-ddr.bin` with the vendor tool (its `fsbl.json` copied and
   edited to point at `u-boot-spl-ddr.bin`).
7. Outputs land in `build/vehicle/`.

## The U-Boot patch

Applies to mainline `board/spacemit/k1`; everything is compiled only when the
corresponding make variable is set, so a normal build is unaffected. Licensed
GPL-2.0-or-later like U-Boot.

### `Makefile`

| Variable | Adds |
|---|---|
| `DDR_GPL_FILE` | `ddr_gpl_test.o` + `ddr_gpl_blob.o` (the open firmware embedded with `.incbin`), `-DK1_DDR_GPL_TEST` |
| `DDR_GPL_FULL` | `-DK1_DDR_GPL_FULL` (the SPL slot itself holds the open firmware) |
| `K1_VEHICLE_RESET` | `vehicle_reset.o`, `-DK1_VEHICLE_RESET` |
| `K1_VEHICLE_LAT` | `-DK1_VEHICLE_LAT` |
| `K1_VEHICLE_RATE` | `-DK1_VEHICLE_RATE=<rate>` |
| `K1_VEHICLE_UART` | `-DK1_VEHICLE_UART` |
| `K1_EMMC_TOOL` | `emmc_tool.o`, `-DK1_EMMC_TOOL` |

### `spl.c` changes

- `K1_VEHICLE_RATE` overrides `cfg.data_rate` after the EEPROM read.
- `K1_DDR_GPL_FULL`: calls `ddr_init` with `puts`, prints
  `K1DDRT: ddr_init rc <rc>`, and **checks the return value**: on failure it
  prints `DDR is not ready` and skips the magic-word write and the re-train
  test (then `K1DDRT-DONE` and the reset prompt).
- `K1_DDR_GPL_TEST`: after the usual magic-word test, calls
  `k1_ddr_gpl_retrain_test(cs_num)`.
- `K1_VEHICLE_UART`: `spl_boot_device()` returns `BOOT_DEVICE_UART`.
- `K1_EMMC_TOOL`: `spl_board_init()` runs the eMMC monitor.

### `ddr_gpl_test.c` — DRAM checks and re-train

1. Prints the image size and the PMU PLL/CLKSEL words; verifies the image's
   `K1DDRGPL` magic at offset 8.
2. Builds up to six probe addresses from the CS map words: start, middle and
   end of the low window (up to 2 GiB) and of the high window from
   `0x100000000`.
3. Fills 64 KiB at each probe with an address-dependent pattern.
4. Copies the open firmware to `0xc082d000`, `fence.i`, and calls its re-train
   entry (offset 4) `RETRAIN_ROUNDS` times (3, or 1 in `full` mode).
5. After each round, verifies the previous pattern (`kept-errors`), writes and
   verifies a new one (`fresh-errors`), per probe.
6. With `K1_VEHICLE_LAT`: the latency test (below).
7. Prints `K1DDRT-DONE` and, with `K1_VEHICLE_RESET`, asks for the reset.

### Latency test (`K1_VEHICLE_LAT`)

Builds a chain of 64-byte nodes at `0x10000000` (a random single-cycle
permutation built with Sattolo's algorithm, or address order for the
sequential case), warms it up, then times 2²⁰ dependent loads with `mtime`
(24 MHz) and the cycle counter. Sets: random 16 KiB, random 256 KiB, sequential
32 MiB, random 32 MiB. Output:
`K1DDRT: lat <rand|seq> <KiB> KiB: <ns> ns <cycles> cycles per load, cpu <MHz> MHz`.
It is used to compare the DRAM clock that different PLL settings actually
produce: load latency follows the DRAM clock, while a register read-back only
shows what was written.

### `vehicle_reset.c` — back to the BootROM

Prints `K1VEH: sys 0x…` (the boot flag register `0xd4282d10`) and
`K1VEH: reset? p = PMIC, w = SoC WDT, h = halt`, then waits 3 s for a key:

- `p` (default): PMIC P1 — sets the reboot flag in `ALIVE` and `SW_RST` in
  `PWR_CTRL2`; falls back to the watchdog if the PMIC write fails;
- `w`: SoC watchdog — clears the boot flag, enables the watchdog clocks and
  releases its reset in MPMU, programs it through the unlock sequence with a
  timeout of 1 tick, and starts it;
- `h`: no reset.

### `emmc_tool.c` — eMMC monitor

A line-based monitor (`K1EMMC> ` prompt) on the UART, run from
`spl_board_init()`: `info` (card and EXT_CSD boot fields), `scan`, `dump`
(with CRC-32), `zero` (write zeros and read back) on the user area, `boot0`
or `boot1`, and `quit`. In the user area only blocks 34..8191 are writable, so
the GPT and everything past 4 MiB are out of reach. It exists to inspect and
clear the boot areas so the BootROM falls back to FDL.

## `fdl_run.py`

```
fdl_run.py FSBL [FSBL ...] --log run.log [--runs N] [--reset p|w|h]
           [--tty /dev/ttyUSB0] [--baud 115200] [--timeout 120]
           [--fdl-wait 30] [--back-timeout 20] [--wait-banner S]
           [--done K1DDRT-DONE] [--ymodem u-boot.itb] [--cmd CMD ...]
           [--cmd-timeout 60] [--p1-bus 8] [--quiet]
```

For each run (alternating between the given FSBLs, logs numbered
`run-NN.log`):

1. waits for a fastboot device;
2. `fastboot stage <fsbl>` and `fastboot continue`;
3. waits for the `--done` marker on the UART;
4. with `--ymodem`: sends the FIT with YMODEM, stops autoboot, runs each
   `--cmd` at the U-Boot prompt; the pseudo-command `p1-reset` resets through
   the PMIC with `i2c` commands from the prompt (the `pmic` command cannot reach
   register `0xAB`);
5. otherwise answers the `K1VEH: reset?` prompt with `--reset` and waits for
   the BootROM banner `usb download handler`, which is the reliable "ready"
   signal (a stale USB gadget can still show in `fastboot devices`).

After each run it prints a one-line summary: `ddr_init` rc, the first fp2
result, DRAM errors from the kept/fresh checks, whether a U-Boot prompt was
reached, `mtest` errors, and the next `sys` value.

## `emmc_tool.py`

```
emmc_tool.py --log L boot FSBL            load the emmc image through FDL, wait for the prompt
emmc_tool.py --log L cmd "scan 1 0 8192"  run one monitor command
emmc_tool.py --log L dump PART BLK COUNT -o FILE   copy blocks to FILE, CRC-32 checked
```

## `extract_log.py`

`extract_log.py LOG [LOG ...]` prints, per log: requested rate and final fp,
PLL1 words, one line per pass (rc, PHY status, Rx/Tx winners with minima, gate
range, WL range, RX/TX DQ0 range, duration, and the following DFC), the final
summary line, the MR values, warnings and out-of-range items, the re-train rc,
the DRAM error total and the latency figures. It parses the log lines described
in [acceptance-and-logging.md](acceptance-and-logging.md).

## Typical runs

```sh
# full bring-up by the open firmware, 5 runs, PMIC reset between them
MODE=full UBOOT=… VENDOR_UBOOT=… tools/vehicle/build-vehicle.sh
python3 tools/vehicle/fdl_run.py \
        build/vehicle/FSBL-gpl-full.bin --runs 5 --log logs/full.log
python3 tools/vehicle/extract_log.py logs/full-*.log

# A/B: alternate two images
python3 tools/vehicle/fdl_run.py A.bin B.bin --runs 8 --log logs/ab.log

# error path on hardware
MODE=full EXP=K1_EXP_DFC_TIMEOUT UBOOT=… VENDOR_UBOOT=… tools/vehicle/build-vehicle.sh
```

---

[← Previous: Testing](testing.md) | [Index](README.md#index) | [Next: Implementation Notes, Limitations and Open Points →](implementation-notes.md)
