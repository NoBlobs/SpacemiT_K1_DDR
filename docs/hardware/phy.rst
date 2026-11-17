.. SPDX-License-Identifier: CC-BY-SA-4.0

===
PHY
===

The PHY at ``phy = ddr + 0x40000`` carries four 16 KiB register slices, one per
frequency point (``slice(fp) = phy + (fp << 14)``), plus global analog and
status registers. The full register map is in :dc:`phy`.

Per-byte structure
==================

Each slice has per-byte-lane registers for the four bytes of the 32-bit bus. The
firmware addresses byte ``b`` of rank ``cs0`` at
``slice + K1_BYTE_OFF[b] + (cs0 << 8)`` with
``K1_BYTE_OFF = { 0x0000, 0x1000, 0x0200, 0x1200 }``
(:doc:`../firmware/training-pass`). Per byte the PHY holds the write-leveling
delay, the DQS read-gate code, and the read and write DQ taps that training
centres.

Analog COMMON blocks
====================

Each slice has two COMMON blocks (A and B, the two sub-PHYs) programmed during
Device_Init with the memory type's drive strength, ODT and Vref. They are armed
by writing 0 then 1 before the PHY is initialized (:dc:`phy`, :dc:`device-init`).
The electrical values per memory type are the ``k1_io_lpddr4x`` / ``k1_io_lpddr4``
configurations (:doc:`../firmware/tables`).

Training status and harvest
===========================

Two global PHY registers drive training:

* ``PHY_STATUS`` (``phy+0x18000``): must read 0 after a successful pass; non-zero
  bits flag gate, read-leveling or write errors.
* the *harvest* area: after each PI operation it holds the per-DQ margin of the
  last operation (8 words of 4 DQ margins each, plus a DM word). Training reads
  the worst DQ margin from it at every Vref step (:dc:`eye-training`,
  :doc:`../firmware/training-pass`).

The Vref words (``VREF0``/``VREF1``) set the PHY read reference during the read
sweep; the gate code and tap registers hold the trained results that the
acceptance ranges check (:dc:`reference-measurements`,
:doc:`../firmware/acceptance-and-logging`).
