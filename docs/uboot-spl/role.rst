.. SPDX-License-Identifier: GPL-3.0-or-later

================
Role of the SPL
================

Mainline U-Boot's K1 SPL (``board/spacemit/k1/spl.c``) is the host side of the
firmware contract. Around the DDR bring-up it does, in order:

#. basic SoC setup: console, clocks, I²C, PMIC;
#. reads the board EEPROM (TLV format) for the memory configuration;
#. in ``ddr_early_init()``: looks up the embedded ``ddr-fw`` blob through binman
   symbols, ``memcpy``\ s it to ``DDR_FIRMWARE_BASE = 0xc082d000`` in SRAM and
   runs ``fence.i``;
#. calls the firmware as ``ddr_init(0xc0000000, cs_num, data_rate, puts)``
   (:doc:`boot-interface`);
#. ignores the return value, writes ``0xaa55aa55`` to physical address 0,
   flushes and re-reads that line, and prints ``DDR is ready`` or
   ``DDR is not ready``;
#. continues the normal boot (loads OpenSBI + U-Boot proper).

No change to the mainline SPL *sources* is required to use the free firmware —
only the build-time wiring that supplies the image (:doc:`integration`). The one
part of ``spl.c`` that matters to the firmware is the call above and the EEPROM
read that feeds it.

Memory configuration from the EEPROM
====================================

The SPL fills a small config from the EEPROM TLV and passes two of its fields.
When the EEPROM cannot be read (common under FDL; the SPL prints
``Fail to detect board:-2``) it falls back to defaults:

.. list-table::
   :header-rows: 1
   :widths: 30 30 40

   * - field
     - default
     - passed to the firmware?
   * - ``cs_num``
     - 2
     - yes (``a1``)
   * - ``data_rate``
     - 2400
     - yes (``a2``)
   * - memory type
     - ``"LPDDR4X"``
     - **no** — the ABI has no slot for it
   * - Tx ODT
     - 80 Ω
     - **no**

Because the type and Tx ODT never reach the firmware, the image is built for a
fixed memory type (LPDDR4X, Tx ODT 80 Ω); see :doc:`boot-interface` and
:dc:`boot-interface`.

The ``puts`` argument
=====================

Mainline passes a real ``puts`` only in ``DEBUG`` builds of ``spl.c``; a normal
build passes ``NULL`` and the firmware runs silently. A failing ``ddr_init`` is
therefore invisible to a normal mainline build except through the magic-word
test at address 0, which can still pass on a marginal DRAM. Honouring the return
value needs the small ``spl.c`` change shown in :doc:`validation`.
