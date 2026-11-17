.. SPDX-License-Identifier: CC-BY-SA-4.0

============
Self-refresh
============

Training runs with the ranks parked in self-refresh (SR) so the PI engine can
drive the bus without the controller issuing traffic. The protocol and the MC
STATUS bits are in :dc:`self-refresh`.

Enter and exit
==============

The firmware enters and leaves SR through the ``USER_0`` port:

* **enter:** ``USER_0 = 0x10000000 | (mode << 24) | (1 << 6)``;
* **exit:** ``USER_0 = 0x10000000 | (mode << 24) | (2 << 6)``;

with ``mode = 3`` for two ranks and ``1`` for one. The wait mask on STATUS is
``0x44`` (two ranks) or ``0x04`` (one rank). The enter waits for those bits to
set; the exit waits for them to clear and never waits for a particular full
STATUS value (:doc:`../firmware/training-pass`).

STATUS and the parked state
===========================

A controller out of SR reads STATUS ``0x33`` (two ranks; ``0x11`` acceptable);
with the SR bits set it reads ``0x77``. A DFC kicked while ranks are in SR is
refused by the firmware, and a stuck DFC leaves the ranks parked — the recovery
path issues an SR exit to make the DRAM accessible again before reporting the
timeout (:dc:`dfc-engine`, :doc:`../firmware/dfc`). The bit meanings are in
:dc:`self-refresh`.
