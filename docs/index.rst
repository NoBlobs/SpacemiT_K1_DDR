.. SPDX-License-Identifier: GPL-3.0-or-later

=========================================================
SpacemiT K1 — free LPDDR4/4X DDR init: documentation
=========================================================

This is the project documentation. It is written by the clean team and
complements the read-only clean-room specification in :dc:`docsClean/ <README>`:

* facts taken from :dc:`docsClean/ <README>` are cited by file;
* facts about the firmware cite the source under ``src/``;
* where the implementation differs from or goes beyond the specification, the
  firmware pages say so.

The firmware is a drop-in ``ddr_fw.bin`` loaded and called by mainline U-Boot's
K1 SPL; it brings up the LPDDR4X memory from a cold controller (PLLs, controller
and PHY programming, JEDEC power-up, training of every frequency point and the
final frequency change) with no vendor blob.

.. toctree::
   :maxdepth: 2
   :caption: Contents

   hardware/index
   uboot-spl/index
   firmware/index

Current status
==============

================================ ==========================================
Part                             State
================================ ==========================================
Reference board (BPI-F3, 8 GiB,  full bring-up from power-on; ``DRAM: 8 GiB``
2 ranks, x8, LPDDR4X)            in U-Boot and ``mtest`` clean, characterized
                                 at 2400 and 2666 MT/s
1 rank / x16 / 16 GiB            described from encodings and command formats,
                                 not measured on hardware
LPDDR4 (non-X)                   IO table and sequence derived; refused by the
                                 firmware (MR14 sweep range unknown)
PI training sequencer            used as a black box; its internal algorithm
                                 is unknown
================================ ==========================================

Start with :doc:`hardware/architecture` for the subsystem and
:doc:`firmware/boot-sequence` for how the firmware brings it up.

Evidence labels
===============

The clean-room specification labels every fact by confidence; this
documentation keeps the same vocabulary.

``[measured]``
   Observed directly on the board (see :dc:`reference-measurements` and
   :dc:`test-platform`).
``[required]``
   Forced by a standard (JEDEC) or by an unavoidable hardware contract.
``[inferred]``
   Deduced from measurements and encodings, not observed in isolation.
``[design]``
   A decision of this project, not a hardware fact.
``[open]``
   Unknown; to be measured.
