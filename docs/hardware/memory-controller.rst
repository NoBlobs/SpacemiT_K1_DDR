.. SPDX-License-Identifier: CC-BY-SA-4.0

=================
Memory controller
=================

The MC at ``ddr = 0xc0000000`` holds the command ports, STATUS, the four
per-frequency timing banks and the address map. The full register set is in
:dc:`memory-controller`.

Command ports
=============

The firmware issues DRAM commands through a few write ports; each takes a CS
mask in its top byte (bit 0 = CS0, bit 1 = CS1):

.. list-table::
   :header-rows: 1
   :widths: 24 20 56

   * - port
     - offset
     - use
   * - ``USER_0``
     - ``0x20``
     - JEDEC init, ZQ calibration (per-rank selector)
   * - ``CA_CMD``
     - ``0x24``
     - mode-register read/write, MR14 path
   * - ``PI_CMD``
     - ``0x13d0``
     - PI training-engine commands (:dc:`pi-training-engine`)
   * - ``MISC``
     - —
     - secondary command words used in the DFC flows

The mode-register path (``CA_CMD`` / ``MR_DATA``) is specified in
:dc:`mode-registers`; the firmware wrappers are in
:doc:`../firmware/training-pass` and :doc:`../firmware/controller-init`.

STATUS
======

``STATUS`` reports the controller/rank state, including the self-refresh bits.
A healthy 2-rank controller out of self-refresh reads ``0x33`` (``0x11`` is also
acceptable); ``0x77`` means ranks are parked in self-refresh. The bit meanings
are in :dc:`self-refresh`; the firmware waits on specific masks rather than full
values.

Timing banks
============

Four banks hold the frequency-dependent MC words, selected by
``CTL_FREQ[31:28]``. Device_Init writes every bank (fp3 down to fp0) from the
programming tables, including three IO rows computed from the electrical
configuration of the memory type, and — for x8 parts — two overlay words per
bank. The register values are the clean-room tables (:dc:`device-init`,
:dc:`memory-controller`), carried in the firmware as ``k1_mc_globals``,
``k1_mc_timing`` and ``k1_mc_x8_overlay`` (:doc:`../firmware/tables`).

The "MC is clocked" check: writing ``CTL0`` (the first global) and reading it
back is how the firmware confirms the MC has a clock and is out of reset before
programming the rest (:doc:`../firmware/controller-init`).
