.. SPDX-License-Identifier: GPL-2.0-or-later

==================
Validation vehicle
==================

The *vehicle* (``tools/vehicle/``) runs the firmware on a real board in a loop,
without a boot medium and without anyone pressing buttons: it builds signed
first-stage images, pushes them through the BootROM's USB download mode (FDL),
captures the UART, and resets the board back into FDL by software for the next
run [spec: :dc:`test-platform`].

.. code-block:: text

   build-vehicle.sh --> signed FSBL --fastboot stage/continue--> board (BootROM FDL)
                                                                   | UART
   fdl_run.py <------------ log, summary <--------------------------+
        |  answers the reset prompt (PMIC / watchdog) --> board back in FDL
        +- optional: ymodem U-Boot proper, commands at the U-Boot prompt
   extract_log.py --> one summary per log

Components
==========

.. list-table::
   :header-rows: 1
   :widths: 34 20 46

   * - file
     - kind
     - role
   * - ``build-vehicle.sh``
     - shell
     - builds the firmware, a patched U-Boot SPL and the signed FSBL
   * - ``u-boot/0001-…-validation-vehicle.patch``
     - U-Boot patch
     - validation hooks in ``board/spacemit/k1``
   * - ``fdl_run.py``
     - Python 3 + pyserial
     - loads FSBLs through fastboot, captures the UART, drives resets and the
       U-Boot prompt
   * - ``ymodem.py``
     - Python 3
     - minimal YMODEM sender used by ``fdl_run.py``
   * - ``emmc_tool.py``
     - Python 3 + pyserial
     - drives the eMMC monitor built into the ``emmc`` image
   * - ``extract_log.py``
     - Python 3
     - summarizes vehicle logs

``build-vehicle.sh``
====================

Required environment: ``UBOOT`` (mainline checkout, copied and patched, not
modified in place), ``VENDOR_UBOOT`` (the FSBL signing tool and keys, only
executed on a copy of its configuration), ``OPENSBI`` (``MODE=uboot`` only),
``DDR_FW`` (``MODE=retrain`` only), ``WORK`` (scratch dir), ``CROSS_COMPILE``.

.. list-table::
   :header-rows: 1
   :widths: 18 46 36

   * - ``MODE=``
     - DDR firmware in the SPL, then
     - output
   * - ``retrain`` (default)
     - proprietary ``ddr_fw.bin``; the open firmware re-trains the running fp
       three times
     - ``FSBL-gpl-test.bin``
   * - ``full``
     - open firmware from power-on, DRAM checks around one re-train round
     - ``FSBL-gpl-full.bin``
   * - ``emmc``
     - as ``full``, then an eMMC monitor on the UART
     - ``FSBL-gpl-emmc.bin``
   * - ``uboot``
     - as ``full``, then OpenSBI + U-Boot proper over ymodem
     - ``FSBL-gpl-uboot.bin`` + ``u-boot.itb``

Options: ``EXP=K1_EXP_…`` (an error-injection image,
:doc:`../firmware/testing`), ``RATE=<MT/s>`` (override ``data_rate``),
``LAT=1`` (DRAM latency meter), ``RESET=`` (no software reset at the end).

The U-Boot validation patch
===========================

Applies to mainline ``board/spacemit/k1``; everything compiles only when the
matching make variable is set, so a normal build is unaffected. It is
GPL-2.0-or-later like U-Boot. The ``spl.c`` changes it adds are the minimal way
to **honour the firmware's return value** (mainline ignores it, :doc:`role`):

* ``K1_DDR_GPL_FULL``: call ``ddr_init`` with ``puts``, print ``K1DDRT:
  ddr_init rc <rc>``, and on failure print ``DDR is not ready`` and skip the
  magic-word write and the re-train test;
* ``K1_DDR_GPL_TEST``: after the magic-word test, run ``k1_ddr_gpl_retrain_test``
  (fills DRAM probes from the CS map, calls the re-train entry at offset 4 a few
  times, and reports ``kept-errors`` / ``fresh-errors`` per probe);
* ``K1_VEHICLE_RATE``, ``K1_VEHICLE_UART`` (boot U-Boot proper over ymodem),
  ``K1_EMMC_TOOL``, ``K1_VEHICLE_RESET`` (software reset back to FDL).

``fdl_run.py``
==============

Loops over one or more FSBLs: waits for the fastboot device, ``fastboot stage``
+ ``fastboot continue``, waits for a marker on the UART (``K1DDRT-DONE``),
optionally sends U-Boot proper by ymodem and runs commands at the prompt,
otherwise answers the ``K1VEH: reset?`` prompt to return the board to FDL. After
each run it prints a one-line summary (``ddr_init`` rc, the first fp2 result,
DRAM errors, whether a U-Boot prompt was reached, ``mtest`` errors).

Log prefixes
============

The firmware's own prefixes are in :doc:`../firmware/acceptance-and-logging`.
The vehicle adds: ``K1DDRT:`` / ``K1DDRT-DONE`` (the validation hook),
``K1VEH:`` (the reset prompt) and ``K1EMMC>`` (the eMMC monitor).
``extract_log.py`` parses exactly these lines, so changing a firmware format
string means updating that script and ``fdl_run.py``'s summary too.

Result on the reference board
=============================

On the BPI-F3 8 GiB (2 ranks, x8, LPDDR4X) the full open-firmware bring-up runs
from power-on at 2400 and 2666 MT/s: ``DDR is ready``, U-Boot proper shows
``DRAM: 8 GiB``, and the DRAM pattern checks and ``mtest`` are clean
[measured; see :dc:`reference-measurements`].
