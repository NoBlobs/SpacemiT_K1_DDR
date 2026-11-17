.. SPDX-License-Identifier: CC-BY-SA-4.0

==========================
Addressing and address map
==========================

MMIO windows
============

The firmware reaches three register windows, plus the DFC table window
(:dc:`mmio-map`):

.. list-table::
   :header-rows: 1
   :widths: 24 24 52

   * - window
     - base
     - contents
   * - MC
     - ``0xc0000000``
     - command ports, STATUS, timing banks, CS map and geometry
   * - PHY
     - ``0xc0040000`` (``ddr + 0x40000``)
     - four 16 KiB slices, COMMON analog blocks, training status / harvest
   * - APMU
     - ``0xd4282800``
     - DDR PLLs, clock select, MC reset, frequency-change kicks
   * - DFC window
     - ``0xf0000000``
     - write port of the hardware frequency-change table

A register index for all three is in :dc:`mmio-map`.

CPU address map
===============

After bring-up the MC maps the detected capacity into the CPU address space in
two windows: the low 2 GiB at CPU address ``0x0`` and the rest from
``0x1_0000_0000``. Ranks are contiguous, not fine-interleaved: on the reference
boards CS1 begins at DRAM offset 4 GiB (8 GiB board) or 8 GiB (16 GiB board),
i.e. CPU address 6 GiB or 10 GiB respectively (:dc:`address-map`).

Size and geometry from MR8
==========================

The MC describes each rank with a *size code* and a *geometry* (row/column/bank
counts) derived from MR8. The firmware reads MR8 once (rank 0, channel 0),
decodes width (``MR8[7:6]``: x16 or x8) and density (``MR8[5:2]``), and programs
the CS0/CS1 map and geometry words. The decode table, the per-rank size codes
and the CS-map register layout are specified in :dc:`address-map`; the firmware
side is in :doc:`../firmware/controller-init`.

Supported parts: x8 and x16 with density codes 0..6 (2–16 Gb per channel) whose
rank size has a CS-map code. 1 Gb parts, x8 12 Gb and other widths are rejected
(:doc:`../firmware/notes`).
