.. SPDX-License-Identifier: GPL-3.0-or-later

=========================
Acceptance and logging
=========================

Range checks (``lib/k1_ddr_accept.c``)
======================================

Three functions compare the result of a pass with the reference ranges of the
reference board [spec: :dc:`reference-measurements`]. Each prints one
``  out of range: …`` line per item and returns the number of items.

With ``K1_INDEP_EYE`` (the production data-eye path) the blob **does not**
run these PI-harvest checks on cold boot: the Samsung 8 GiB doc-21 windows
do not match Micron taps that still verify. ``ddr_init`` instead returns
``K1_DDRT_EIO`` when the independent memory-pattern verify (or the MR14
check) fails. Without ``K1_INDEP_EYE`` the functions still never change a
return code; the image only counts the items on the final line.

``k1_ddr_accept_final(env, res, cs_num, mr14_rc)``
--------------------------------------------------

Applied to the pass at the final frequency point (fp2). For every rank and byte:

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - item
     - range
   * - WL (``+0x24``)
     - 17..23
   * - gate code
     - bit 16 set, high byte (``g``) 33..40 and low byte (``t``) 33..40 (they
       need not be equal after read training)
   * - RX DQ0 tap
     - 17..19
   * - TX DQ0 tap
     - 30..36

Plus one item each for ``PHY_STATUS != 0`` and for a failed MR14 check
(``mr14_rc != 0``).

``k1_ddr_accept_pp2_2400(env, res)``
------------------------------------

Applied to the first fp2 pass when the requested rate is 2400:

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - item
     - range
   * - Rx winner
     - index 4..6 with its minimum ≥ 10
   * - Tx winner
     - MR14 29..31 (``0x1d``–``0x1f``) with its minimum ≥ 10

When either is out of range it adds the hint "at 2400 a min of 8/9 is the 2666
CK: check PLL1", because that is what a PLL1 left at the 2666 setting looks like.

``k1_ddr_accept_pp0b_gate(env, res, cs_num)``
---------------------------------------------

Applied to the second fp0 pass: the gate high byte of every rank and byte must
be 32..40 (the cold first pass is far higher; a value in that range shows the
byte-mode re-initialization took effect).

Printing helpers
----------------

.. list-table::
   :header-rows: 1
   :widths: 40 60

   * - function
     - output
   * - ``k1_ddr_print_result(env, label, res, cs_num)``
     - Vref words, ``CTL_TXVREF`` and its MR14 field, PHY/PI status; per rank: WL
       and gate of the 4 bytes, RX DQ0, TX DQ0 and TX DM of each byte
   * - ``k1_ddr_print_pass_stats(env, res, cs_num)``
     - STATUS before / after SR enter / after SR exit; per stage: PI ops, stale
       first reads, max polls, max µs, stage µs; per rank the 16 Rx and 16 Tx
       minima
   * - ``k1_ddr_print_mrs(env, cfg)``
     - MR5, MR6, MR8, MR12, MR14 of every (rank, channel), or ``MRn:err<rc>``

Where the checks run
====================

.. list-table::
   :header-rows: 1
   :widths: 34 66

   * - caller
     - checks
   * - ``boot_on_pass`` in ``blob.c`` (cold boot)
     - without ``K1_INDEP_EYE``: 2nd pass → ``pp0b_gate``; successful fp2
       pass → ``final`` (with ``mr14_rc = OK``; the MR14 check of the boot
       happens after the next DFC) and, at 2400, ``pp2_2400``. With
       ``K1_INDEP_EYE``: skip those counts; fail ``ddr_init`` if
       ``k1_ddr_eye_measure`` returns non-zero.
   * - ``k1_blob_retrain``
     - without ``K1_INDEP_EYE``, at fp2 → ``final`` with the real MR14 check
       result. With ``K1_INDEP_EYE`` that harvest count is skipped; a failed
       MR14 check still fails the re-train return code.
   * - tests (``tools/test/test_accept.c``)
     - each item in and out of range, and the exact messages

Log output
==========

Everything goes through ``env->vlog``. In the image that is ``puts`` from the
SPL, one call per log line (each formatted into a 200-byte buffer). Mainline's
SPL passes ``puts`` only in ``DEBUG`` builds; otherwise nothing is printed and
no formatting happens.

Prefixes
--------

.. list-table::
   :header-rows: 1
   :widths: 34 66

   * - prefix
     - origin
   * - ``k1-ddr:``
     - bring-up and boot sequence (``k1_ddr_boot.c``, ``k1_ddr_init.c``,
       ``blob.c``)
   * - ``k1-ddrt:``
     - training pass (``k1_ddrt.c``)
   * - ``k1-ddrt retrain:``
     - re-train entry (``blob.c``)
   * - two-space indent
     - detail lines of the printing and acceptance helpers
   * - ``K1DDRT:``, ``K1DDRT-DONE``
     - validation hook in U-Boot (:doc:`../uboot-spl/validation`)
   * - ``K1VEH:``
     - validation reset prompt in U-Boot
   * - ``K1EMMC>``
     - validation eMMC monitor prompt

A cold boot, in order
---------------------

.. code-block:: text

   k1-ddr: open firmware, ddr 0xc0000000 cs_num 2, 2400 MT/s
   k1-ddr: LPDDR4X, cs_num 2, 2400 MT/s, final fp2
   k1-ddr: pmu before: cc_ap … hw_slp … clksel … pll1 … … mck …
   k1-ddr: PLL locked, fp0 up: clksel … hw_slp … mck …
   k1-ddr: device init: status … ctl_freq … pi_cfg … phy_init … pi_status … common …
   k1-ddrt: pass pp0 cs_num 2
   k1-ddrt: pp0 ok: rx vref idx … (min …), tx mr14 … (min …)
   k1-ddr: pass 0 pp0 rc 0 stage none cs 0: rx idx …, mr14 …, phy_status 0x0, … us
     sr_stat … / pi … / rx min … / tx min …          (pass statistics)
     state: vref0 … / cs0 wl … gate … / rx dq0 … / tx dq0 … / tx dm …
   k1-ddr: MR5 0x.. MR8 0x..
   k1-ddr: <MB> MB (x8, ROW_17), cs0 … cs1 … … geom …
   k1-ddr: DFC window ctl0 …, ddr ctl0 … (alias|separate)
   k1-ddr: DFC table loaded: snapshot 0x200..0x28a
   … second fp0 pass …
   k1-ddr: DFC fp1 in … us: ctl_freq … pi_cfg … status … clksel …
   … fp1 pass …
   k1-ddr: DFC fp2 in … us: …
   … fp2 pass, then the acceptance lines if any …
   k1-ddr: DFC fp2 in … us: …                         (final DFC)
     cs0 ch0 MR5:0x.. MR6:0x.. MR8:0x.. MR12:0x.. MR14:0x..   (every rank/channel)
   k1-ddr: rc 0 (none) in … us: 4 passes, MR5 … MR8 … x8, … MB, fp2 ctl_freq … status … clksel …, 0 item(s) out of range

``tools/vehicle/extract_log.py`` parses exactly these lines to summarize a run
(:doc:`../uboot-spl/validation`). Changing a format string means updating that
script (and ``fdl_run.py``'s summary) as well.

Warnings and errors
-------------------

.. list-table::
   :header-rows: 1
   :widths: 50 50

   * - message
     - meaning
   * - ``k1-ddr: warning: phy_init 0x…, expected 0x100``
     - ``PHY_INIT`` after Device_Init is not the value expected; continues
   * - ``k1-ddr: warning: MR14 differs from the fp<N> bank``
     - after a DFC the dies' MR14 does not match the new bank; continues
   * - ``k1-ddr: CTL0 reads 0x… after writing 0x…``
     - MC not clocked or in reset; Device_Init fails
   * - ``k1-ddr: PLL1/PLL2 not locked, clksel 0x…``
     - lock timeout
   * - ``k1-ddr: SW-FC to fp0 did not finish, cc_ap 0x…``
     - SW-FC timeout
   * - ``k1-ddr: DFI init_start not done, pi_status 0x…``
     - DFI handshake timeout
   * - ``k1-ddr: DRAM init not done, status 0x..``
     - JEDEC init timeout
   * - ``k1-ddr: MR8 0x..: no address map for this part``
     - unsupported density/width
   * - ``k1-ddr: DFC to fp<N> refused: …``
     - ranks in SR or bit 22 still held
   * - ``k1-ddr: DFC to fp<N> not done in 10000 us, cc_ap 0x…`` + two register dumps
     - DFC timeout
   * - ``k1-ddr: after SR exit: status 0x.. [(still parked)], cc_ap … [, bit 22 held: MC reset before any DFC]``
     - recovery after a DFC timeout
   * - ``k1-ddr: <step> failed, rc <rc>``
     - any boot failure
   * - ``k1-ddr: LPDDR4 MR14 range not established``
     - LPDDR4 requested
