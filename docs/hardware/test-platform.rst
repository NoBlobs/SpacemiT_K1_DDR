.. SPDX-License-Identifier: CC-BY-SA-4.0

=============
Test platform
=============

Every measured fact in the specification was established on real hardware
through the BootROM's USB firmware-download mode (FDL). The setup, the software
resets and the memory layout are in :dc:`test-platform`; the method and
confidence labels are in :dc:`methodology`.

FDL loop
========

The BootROM accepts a first-stage image over USB (``fastboot stage`` /
``fastboot continue``) and runs it from SRAM. Each hypothesis about a register,
encoding, wait condition or ordering rule was turned into a small image, loaded
through FDL, and checked against UART output, register dumps and DRAM integrity
tests. A software reset (SoC watchdog or PMIC) returns the board to FDL with no
one touching it, so runs chain automatically. The project's validation vehicle
automates exactly this loop (:doc:`../uboot-spl/validation`).

Reference boards
================

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - board
     - part
   * - BPI-F3 8 GiB
     - Samsung ``K4UCE3Q4AA-MGCL`` LPDDR4X, 2 ranks, x8 dies, 8 Gb/die
   * - BPI-F3 16 GiB
     - Micron ``MT53E4G32D8`` LPDDR4X, 2 ranks, x8 dies, 16 Gb/die

Both were characterized from a cold controller to a fully mapped DRAM, at 1066,
1200, ≈1333, 2400 and 2666 MT/s, with per-rank and per-lane training windows,
ODT/IO sweeps and multi-minute memory soaks (:dc:`reference-measurements`).

Reference measurements
======================

The measured training windows (WL, gate, read/write taps), the ODT/IO
sensitivities and the per-rank address boundaries are the basis for the
firmware's acceptance ranges (:dc:`reference-measurements`,
:doc:`../firmware/acceptance-and-logging`). Board/die-dependent thresholds differ
between the two boards (for example 48 Ω DRAM DQ ODT is safe at 2666 on the
16 GiB Micron but fails on the 8 GiB Samsung), while the SoC-level behaviour is
identical.
