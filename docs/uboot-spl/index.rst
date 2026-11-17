.. SPDX-License-Identifier: GPL-2.0-or-later

==========
U-Boot SPL
==========

Mainline U-Boot's K1 SPL is the host side of the firmware contract: it sets up
the console and the EEPROM, reads the memory configuration, loads ``ddr_fw.bin``
into SRAM, calls it to bring up the DRAM, and then continues the normal boot.

The firmware is a drop-in image: no change to the mainline SPL *sources* is
required, only the build-time wiring that places the image and a one-line call
already present in ``board/spacemit/k1``. These pages describe that contract and
how to build a U-Boot that uses the free firmware.

.. toctree::
   :maxdepth: 1

   role
   boot-interface
   integration
   validation
