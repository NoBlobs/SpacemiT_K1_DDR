.. SPDX-License-Identifier: CC-BY-SA-4.0

===============
Clocks and PMU
===============

The DDR clocks come from two PLLs (PLL1, PLL2) in the APMU at ``pmu =
0xd4282800``. The firmware programs them, waits for lock, moves to frequency
point 0 by a software frequency change (SW-FC) and enables the MC clock. The
exact register words are in :dc:`clocks-and-pmu`; the firmware side is
:doc:`../firmware/controller-init`.

PLLs and the data rate
======================

PLL1 carries the data-rate-dependent VCO. The two settings the firmware uses are
2400 and 2666 MT/s; every other requested rate is served by the 2400 setting
driving the right frequency point. 2666 is the SoC maximum and trains with
smaller margins (shorter unit interval). A ≈1333 MT/s operating point exists in
the silicon (PLL1 at the 2666 VCO, ending at fp0), but the LPDDR4X image does not
select it (:doc:`../uboot-spl/boot-interface`).

Clock-select, lock and SW-FC
============================

``CLKSEL`` (``pmu`` register) enables PLL1/PLL2 and reports their lock bits.
Bring-up order (:dc:`clocks-and-pmu`):

#. write the PLL1/PLL2 VCO and analog words;
#. enable both PLLs in ``CLKSEL`` and wait for both lock bits;
#. select the fp0 clock word, set ``HW_SLP`` for a SW-FC, kick ``CC_AP`` bit 26
   and wait for it to clear;
#. enable the MC clock (``MCK_CTRL``).

No MC register may be read or written before the MC clock is on. The per-fp
clock words are ``k1_clksel[]`` in the firmware (:doc:`../firmware/tables`).

MC reset
========

Pulsing ``HW_SLP`` bit 21 low then high (≥100 µs each half) resets the MC and
PHY while keeping the PLL configuration. The firmware uses it before the x8
re-initialization (:doc:`../firmware/controller-init`); it must be followed by a
full Device_Init.

Frequency-change kicks
======================

Two kicks share the PMU: the SW-FC kick (``CC_AP`` bit 26, a read-modify-write,
used once to reach fp0) and the hardware DFC kick (``CC_AP`` bit 22, a
whole-word write, used for every run-time frequency change). The DFC side is in
:dc:`dfc-engine` and :doc:`../firmware/dfc`.
