.. SPDX-License-Identifier: GPL-3.0-or-later

===========================
Integration into the build
===========================

Using the free firmware with a mainline U-Boot tree is a build-time wiring step;
no source change to the SPL is needed [spec: :dc:`boot-interface`].

Build time
==========

The board ``-u-boot.dtsi`` files (for example ``k1-bananapi-f3-u-boot.dtsi``)
declare a binman section that concatenates the SPL and the DDR firmware:

.. code-block:: text

   u-boot-spl-ddr {                /* output: u-boot-spl-ddr.bin */
           type = "section";
           u-boot-spl { };
           ddr-fw {
                   type = "blob";
                   filename = "ddr_fw.bin";
                   align = <64>;
           };
   };

The file is supplied through the ``DDR_FW_FILE`` environment variable. The
resulting ``u-boot-spl-ddr.bin`` is what gets signed and placed where the
BootROM looks for the first stage (or pushed over FDL).

.. code-block:: sh

   # build the free firmware
   make -C src blob
   export DDR_FW_FILE=$PWD/build/blob/k1_ddr_fw.bin
   # in the U-Boot tree
   make spacemit_k1_defconfig && make      # -> u-boot-spl-ddr.bin

The image is a plain drop-in: it replaces the proprietary ``ddr_fw.bin`` byte
for byte in the same slot. See :doc:`../firmware/build-and-image` for how the
image itself is built and :doc:`../firmware/entry-points` for its header.

Run time
========

``ddr_early_init()`` in ``board/spacemit/k1/spl.c`` finds the ``ddr-fw`` blob
through binman symbols, copies it to ``0xc082d000``, runs ``fence.i``, reads the
EEPROM and calls the image (:doc:`role`). No relocation, ``.data`` copy or
``.bss`` clearing takes place, which is why the image carries no writable data
(:doc:`../firmware/architecture`).

Signing and flashing
====================

``u-boot-spl-ddr.bin`` must be signed for the BootROM to accept it as a first
stage; the vendor U-Boot tree provides the signing tool. The signed FSBL is
then written to the boot medium's first-stage slot, or pushed over FDL for a
test run (:doc:`validation`).
