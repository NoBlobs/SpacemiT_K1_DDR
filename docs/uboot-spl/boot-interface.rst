.. SPDX-License-Identifier: GPL-3.0-or-later

====================
The boot interface
====================

The firmware fits the slot mainline U-Boot already provides for the proprietary
``ddr_fw.bin``: same file name, same load address, same entry point, same
calling convention [spec: :dc:`boot-interface`].

The ABI
=======

.. code-block:: c

   int ddr_init(u64 ddr_base, u32 cs_num, u32 data_rate,
                void (*puts)(const char *));

Standard RISC-V LP64 calling convention, entered at **offset 0** of the image
(``0xc082d000``). In the firmware this is ``k1_blob_ddr_init``
(:doc:`../firmware/entry-points`).

.. list-table::
   :header-rows: 1
   :widths: 14 20 66

   * - register
     - argument
     - values / rules
   * - ``a0``
     - ``ddr_base``
     - MC base, ``0xc0000000`` on the K1; every MC/PHY address derives from it
       (``phy = ddr + 0x40000``)
   * - ``a1``
     - ``cs_num``
     - 1 or 2 populated ranks; using 1 on a 2-rank package leaves CS1 untrained
   * - ``a2``
     - ``data_rate``
     - requested final rate in MT/s (mapping below)
   * - ``a3``
     - ``puts``
     - optional UART output routine; **may be NULL**, the firmware must not
       require it
   * - ``a4``, ``a5``
     - —
     - **undefined** (caller garbage); the ABI does not grow
   * - return
     - ``int``
     - 0 on success, negative on failure; the mainline SPL ignores it

``data_rate`` mapping
---------------------

.. list-table::
   :header-rows: 1
   :widths: 22 16 62

   * - requested
     - final fp
     - notes
   * - 2400
     - 2
     - PLL1 VCO for 2400
   * - 2666
     - 2
     - PLL1 VCO for 2666, same fp2 bank; SoC maximum
   * - 1066
     - 1
     -
   * - 1200
     - 0
     - PLL1 VCO for 2400; fp0 is 1200 MT/s
   * - 1333
     - 0
     - PLL1 VCO for 2666; fp0 is ≈1333 MT/s (same MC bank as 1200)
   * - 2133
     - 0 (LPDDR4X)
     - no fp3 on LPDDR4X; the final DFC goes back to fp0
   * - anything else
     - 0
     - treated as 1200; never rounded up to 2666

The firmware's ``k1_ddr_final_fp()`` implements this mapping
(:doc:`../firmware/boot-sequence`). ``k1_ddr_pll_init()`` writes the 2666 PLL1
pair when ``data_rate`` is 2666 or 1333.

What the ABI does not carry
===========================

The SPL reads the memory type (LPDDR4 vs LPDDR4X) and the Tx ODT from the EEPROM
but does not pass them, and the silicon offers no way to recover them (no mode
register, strap, PMU bit or eFuse distinguishes the two before Device_Init)
[spec: :dc:`boot-interface`]. Consequences:

* **one image per memory type** — this image is LPDDR4X with Tx ODT 80 Ω; an
  LPDDR4 board would need a different build (and the firmware disables LPDDR4
  anyway, :doc:`../firmware/notes`);
* a single image serving both types would require changing the SPL and the ABI
  upstream.

Execution environment
=====================

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - property
     - value
   * - privilege / harts
     - M-mode, single hart (the SPL's)
   * - code location
     - raw, position-dependent image linked at ``0xc082d000``
   * - usable window
     - ``0xc082d000`` up to the BootROM data area near ``0xc0838000``: 0xb000
       bytes (44 KiB) for code and read-only data
   * - writable data
     - **none in the image**; all mutable state lives on the stack
   * - stack
     - the SPL's stack (SRAM)
   * - DRAM
     - not usable (it is what is being initialised)
   * - time base
     - ``mtime`` at ``0xe400bff8``, 24 MHz, for delays and timeouts

Output contract
===============

When ``ddr_init`` returns 0: the DRAM is at the final frequency point, out of
self-refresh and refreshing; every frequency point the boot may use is trained;
the address map covers the full detected capacity; MC STATUS reads ``0x33`` (or
``0x11``), never ``0x77``; and the PHY training status ``phy+0x18000`` is 0. On
failure the firmware returns a negative code and does not continue past a failed
training pass — but because the mainline SPL ignores the return value, a UART
message (when ``puts`` is present) is the only report.
