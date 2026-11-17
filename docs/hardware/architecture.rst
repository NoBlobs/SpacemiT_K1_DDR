.. SPDX-License-Identifier: CC-BY-SA-4.0

============
Architecture
============

The K1 DDR subsystem is one 32-bit LPDDR4/4X channel built from two 16-bit JEDEC
channels, driven by a memory controller (MC) with a PHY and a PI training engine,
clocked by two DDR PLLs in the APMU. The full register-level description is in
:dc:`system-overview` and :dc:`mmio-map`; this page is the map of the pieces the
firmware touches.

Blocks and bases
================

.. list-table::
   :header-rows: 1
   :widths: 20 24 56

   * - block
     - base
     - role
   * - MC
     - ``ddr`` = ``0xc0000000``
     - memory controller: command ports, STATUS, four per-frequency timing banks,
       address map (:dc:`memory-controller`)
   * - PHY
     - ``phy`` = ``ddr + 0x40000``
     - four 16 KiB register slices (one per frequency point), per-byte taps, gate
       codes, analog COMMON blocks, training status and harvest (:dc:`phy`)
   * - PI engine
     - inside the MC
     - the training sequencer: commands, done bits, ``PI_STATUS`` semantics
       (:dc:`pi-training-engine`)
   * - APMU (DDR part)
     - ``pmu`` = ``0xd4282800``
     - DDR PLL1/PLL2, clock mux, lock bits, SW-FC and HW-DFC kicks, MC reset
       (:dc:`clocks-and-pmu`)
   * - DFC window
     - ``0xf0000000``
     - the window the hardware frequency-change table is written through
       (:dc:`dfc-engine`)

Frequency points
================

The MC holds four timing banks and the PHY four register slices, indexed by the
*frequency point* ``fp`` (0..3; also ``pp`` or ``boot_pp`` in a training
context). ``slice(fp) = phy + (fp << 14)``. The banks are selected on the live
MC by ``CTL_FREQ[31:28]`` and switched at run time by the hardware DFC engine.
The LPDDR4X boot uses fp0, fp1 and fp2 (LPDDR4 adds fp3); which one is left
running depends on the requested rate (:doc:`../uboot-spl/boot-interface`).

Reference hardware
==================

All measured facts come from two Banana Pi BPI-F3 boards: an 8 GiB Samsung
``K4UCE3Q4AA-MGCL`` (2 ranks, x8 dies, 8 Gb/die) and a 16 GiB Micron
``MT53E4G32D8`` (2 ranks, x8 dies, 16 Gb/die), both LPDDR4X
(:dc:`system-overview`, :dc:`test-platform`). Confidence labels used throughout
the specification — *measured*, *required*, *inferred*, *opaque*, *unknown* —
are defined in :dc:`methodology`.

Conventions
===========

* all registers are 32-bit, little-endian, aligned words;
* addresses are always ``base + offset``; ``ddr+0x3b4`` and ``pmu+0x3b4`` are
  different registers;
* rank-selecting command fields use a CS mask (bit 0 = CS0, bit 1 = CS1);
* data rates are in MT/s (the data rate, not the clock frequency).
