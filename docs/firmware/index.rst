.. SPDX-License-Identifier: GPL-3.0-or-later

========
Firmware
========

Implementation of the free DDR init firmware (``src/``). It is C23, freestanding,
built into a position-dependent raw image that the SPL copies to SRAM and calls.
These pages are written from the source and improve on the earlier notes; where
they state a hardware fact they cite :dc:`docsClean/ <README>`.

.. toctree::
   :maxdepth: 1

   architecture
   build-and-image
   entry-points
   boot-sequence
   controller-init
   dfc
   training-pass
   tables
   acceptance-and-logging
   testing
   notes
