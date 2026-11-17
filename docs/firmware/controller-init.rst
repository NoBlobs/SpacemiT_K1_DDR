.. SPDX-License-Identifier: GPL-3.0-or-later

====================
Controller bring-up
====================

``lib/k1_ddr_init.c`` holds the building blocks that ``k1_ddr_boot()``
sequences: clocks, Device_Init, the MC reset, the address map and
mode-register writes. The DFC part of the same file is described in :doc:`dfc`.
The register values come from the clean-room specification
[spec: :dc:`clocks-and-pmu`, :dc:`device-init`, :dc:`memory-controller`,
:dc:`phy`, :dc:`address-map`].

Hardware context
================

.. code-block:: c

   struct k1_ddr_hw k1_ddr_hw_init(const struct k1_ddr_boot_cfg *cfg,
                                   const struct k1_ddrt_env *env);

Returns ``{ ddr, phy = ddr + 0x40000, pmu, win = dfc_win, cs_num,
cs_sel = (cs_num == 1 ? 1 : 3), type, io, env }``, where ``io`` points to
``k1_io_lpddr4x`` or ``k1_io_lpddr4`` (:doc:`tables`). Every other function in
the file takes this structure.

Timing constants
================

.. list-table::
   :header-rows: 1
   :widths: 30 20 50

   * - constant
     - value
     - used for
   * - ``K1_LOCK_BUDGET_US``
     - 1 000 µs
     - PLL lock wait
   * - ``K1_FC_BUDGET_US``
     - 10 000 µs
     - SW-FC and HW-DFC busy waits
   * - ``K1_SR_BUDGET_US``
     - 10 000 µs
     - SR exit after a stuck DFC
   * - ``K1_INIT_BUDGET_US``
     - 10 000 µs
     - DFI ``init_start`` and JEDEC init waits
   * - ``K1_MC_RESET_US``
     - 100 µs
     - each half of the MC reset pulse
   * - ``K1_ZQ_START_US``
     - 2 µs
     - after ZQCAL START
   * - ``K1_ZQ_LATCH_US``
     - 1 µs
     - after ZQCAL LATCH
   * - ``K1_MRW_US``
     - 1 µs
     - after every MR write

``k1_ddr_pll_init()`` — PLLs, SW-FC to fp0, MC clock
====================================================

#. Logs the PMU words found at entry (``cc_ap``, ``hw_slp``, ``clksel``, both
   PLL1 words, ``mck``), for the record.
#. PLL1:

   * ``data_rate == 2666``: writes ``PLL1_CTL1 = 0x203c5555``,
     ``PLL1_CTL2 = 0x50dd6538``;
   * anything else: read-modify-write of ``PLL1_CTL1[15:8] = 0x3B``,
     ``PLL1_CTL2`` left as found.

#. PLL2: writes ``0x20238000`` / ``0x50dd632c``.
#. CLKSEL: clears the PLL1/PLL2 enable bits (8, 9), then sets 8, 9 and 11.
#. Waits for both lock bits (``CLKSEL & 0x30000``), 1 ms → ``-ETIMEDOUT``, step
   ``pll-lock``.
#. Writes CLKSEL = ``k1_clksel[0]`` (``0x3b50``), ``HW_SLP = 0x40600400``, sets
   the SW-FC kick (bit 26 of ``CC_AP``, read-modify-write) and waits for it to
   clear, 10 ms → ``-ETIMEDOUT``, step ``sw-fc``.
#. Sets ``MCK_CTRL[1:0] = 3`` (MC clock on).
#. Logs the resulting CLKSEL / HW_SLP / MCK and PLL1 words.

No MC register is read or written before step 7 (the test model flags any
access to an unclocked MC; a test asserts zero MC writes when the PLL never
locks).

Error injection: with ``-DK1_EXP_PLL_NO_LOCK`` step 4 leaves the PLLs disabled,
so step 5 times out.

``k1_ddr_device_init()`` — Device_Init
======================================

.. code-block:: c

   int k1_ddr_device_init(const struct k1_ddr_hw *hw, bool x8,
                          enum k1_ddr_step *step);

Three parts, then a verification.

``k1_mc_program()``
-------------------

#. Writes the 20 global words of ``k1_mc_globals`` in table order.
#. Reads ``CTL0`` back. If it does not hold the value just written, the MC has
   no clock or is still in reset: returns ``-EIO`` (step ``mc-program``) before
   doing anything else.
#. Read-modify-write of ``ddr+0x2c8`` bit 17 with ``io->rx_pu_cal``.
#. Fills the four timing banks, from fp3 down to fp0. For each bank it writes
   ``CTL_FREQ = k1_freq_bank(n)`` (both fp fields set to ``n``), then every row
   of ``k1_mc_timing``:

   * normal rows: the value of column ``n``;
   * IO rows (``+0x30c``, ``+0x310``, ``+0x314``): computed by ``k1_write_io()``
     from the IO configuration (below);
   * LPDDR4 only: ``ddr+0x400 = k1_mc_wdqs[n]``.

#. If ``x8``: a second loop over fp3..fp0 writes the two overlay words of
   ``k1_mc_x8_overlay`` into each bank (after all timings, before fp0 is
   selected).
#. Read-modify-write of ``ddr+0x308[27:20] = 0x10``.
#. Selects fp0: clears ``CTL_FREQ[31:28]``; ``PI_CFG |= 0``.
#. Writes the three refresh words.

``k1_write_io()``:

.. list-table::
   :header-rows: 1
   :widths: 24 18 58

   * - register
     - seed
     - read-modify-write
   * - ``+0x30c``
     - ``0x4314``
     - keep ``0x8fff``; set ``ca_vref<<24 | tx_vref<<16 | soc_odt<<12``
   * - ``+0x310``, ``+0x314``
     - ``0x00440000``
     - keep ``0xff80ffff``; set ``tx_odt<<20 | rx_drv<<16``

``k1_phy_analog()``
-------------------

#. PMU: ``PHY_BIAS[15:8] = 0x0F``; LDO: LPDDR4X sets ``[11:10] = 3``; LPDDR4
   sets ``[11:10] = 2`` then ``[13:12] = 3``.
#. For each of the four slices: COMMON A and B ``+0`` written 0, 0, then 1, 1
   (both zeroed before either is armed); then for each COMMON:

   * ``+0x4``: keep ``0xffff0fff``, set bits 11, 13, 15;
   * ``+0xc[15:8]``: ``(tx_pdrv << 5) | (tx_ndrv << 2)``;
   * ``+0xc[21:16]``: ``(rx_odt << 3) | rx_odt``;
   * ``+0x4[31:16]``: ``rx_vref`` in both bytes;
   * ``+0x14``: keep ``0xff9fffef``, set ``3 << 21``;
   * ``+0x10``: set bit 28.

#. ``phy+0x3030 = 0x1077`` (slice 0, COMMON A only).
#. ``slice+0x64 = 0x4349`` in each slice (byte 0 of CS0 only).
#. ``phy+0x10024 = 0``, ``phy+0x10000 |= 1``.

``k1_dram_start()``
-------------------

#. ``PI_CMD = 0x10000001 | cs_sel << 24`` (DFI ``init_start``); waits for
   ``PI_STATUS`` bit 31, 10 ms → ``-ETIMEDOUT``, step ``dfi-init``.
#. ``PI_CMD = 0x10000100 | cs_sel << 24`` (clear).
#. ``USER_0 = 0x10000001 | cs_sel << 24`` (JEDEC init); waits for STATUS
   ``0x01`` (1 rank) or ``0x11`` (2 ranks), 10 ms → ``-ETIMEDOUT``, step
   ``dram-init``.
#. MR writes of ``k1_mr_init1`` (MR1, MR2, MR13, MR3, MR22).
#. ZQCAL per rank, through USER_0 with a single-rank selector (``1 << cs0`` in
   the CS byte): START, 2 µs, LATCH, 1 µs.
#. MR writes of ``k1_mr_init2`` (MR12, MR14, MR11, MR23).

Verification
------------

After the three parts, ``k1_ddr_device_init()`` reads ``CTL_FREQ``, ``PI_CFG``,
``PHY_INIT``, STATUS, ``PI_STATUS`` and ``COMMON+0xc``, logs them, and:

* returns ``-EIO`` (step ``mc-program``) if ``CTL_FREQ != k1_freq_bank(0)`` or
  ``PI_CFG[3:2] != 0``;
* only **warns** if ``PHY_INIT != 0x100``.

``k1_ddr_mc_reset()``
=====================

Clears bit 21 of ``HW_SLP``, waits 100 µs, sets it, waits 100 µs. The caller
must run a full Device_Init afterwards; the PLL configuration is kept.

``k1_ddr_mr_write()``
=====================

.. code-block:: c

   void k1_ddr_mr_write(const struct k1_ddr_hw *hw, uint32_t mr);

``CA_CMD = 0x10020000 | cs_sel << 24 | (mr & 0xff)``, then a 1 µs delay. There
is no completion bit to wait for; the data comes from the active timing bank.
Used for the Device_Init batches and for MR21 (``mr = 0x95``) on 16 GiB parts.

Address map
===========

``k1_ddr_size_code()``
----------------------

Maps the size of one rank in MB to the CS-map size code: 512 → ``0x0d``,
768 → ``0x01``, 1024 → ``0x0e``, 1536 → ``0x02``, 2048 → ``0x0f``,
3072 → ``0x03``, 4096 → ``0x10``, 8192 → ``0x11``, 16384 → ``0x12``; anything
else → 0 (no code).

``k1_ddr_geometry()``
---------------------

.. code-block:: c

   int k1_ddr_geometry(uint8_t mr8, struct k1_ddr_geom *g);

* ``io = MR8[7:6]``: only 0 (x16) and 1 (x8) are accepted.
* ``dens = MR8[5:2]`` is looked up in an internal table of x16 per-channel sizes
  and row counts: 0 → 256 MB / ROW_14, 1 → 384 / 15, 2 → 512 / 15, 3 → 768 / 16,
  4 → 1024 / 16, 5 → 1536 / 17, 6 → 2048 / 17.
* x8 doubles the size and adds one row.
* ``area_mb = 2 × (channel MB << io)`` (two JEDEC channels per rank).
* Returns ``-EINVAL`` for ``io > 1``, a density not in the table (including
  code 12, 1 Gb) or an area without a size code.

Examples (from the tests): ``0x52`` → x8, 4096 MB/rank, code ``0x10``, ROW_17;
``0x18`` → x16, 4096 MB, ROW_17; ``0x58`` → x8, 8192 MB, code ``0x11``, ROW_18;
``0x54`` (x8 12 Gb, 6144 MB) → ``-EINVAL``.

``k1_ddr_map_apply()``
----------------------

.. code-block:: text

   start = area_mb >> 3                         (CS1 start, 8 MB units)
   CS0_MAP: keep 0x0060ffff, set code << 16 | present
   CS1_MAP: keep 0x0060fffe, set (start & 0x1ff) << 23 | code << 16
            | present only when cs_num == 2
   CS1_HI = start >> 9
   CS0_GEOM, CS1_GEOM: keep 0xfffff00c, set (row - 10) << 8 | COL_10 | BANK_8

With one rank the CS1 start and size are still written but the present bit is
cleared.

Error-injection hooks in this file
==================================

.. list-table::
   :header-rows: 1
   :widths: 34 66

   * - flag
     - effect
   * - ``K1_EXP_PLL_NO_LOCK``
     - does not re-enable PLL1/PLL2; the lock wait times out
