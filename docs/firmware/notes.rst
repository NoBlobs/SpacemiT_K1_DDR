.. SPDX-License-Identifier: GPL-3.0-or-later

====================================
Notes, limitations and open points
====================================

Findings that come from reading, building and testing the code. Each item says
where it lives and what would close it.

Behaviour worth knowing
=======================

The image always trains fp0, fp0, fp1 and fp2
---------------------------------------------

``k1_ddr_dfc_plan()`` does not depend on the requested rate. A request for 1066
or 1200 MT/s still trains fp1 and fp2, and then the final DFC goes back down.
This keeps one code path for every rate at the cost of extra passes; every
frequency point the controller may later switch to is trained. The final DFC is
issued even when the final fp is the one just trained.

PLL1 is programmed for 2400 unless 2666 or 1333 is requested
-----------------------------------------------------------

``k1_ddr_pll_init()`` writes the 2666 pair when ``data_rate`` is 2666 or 1333.
Every other value, including 1066, 1200 and 2133, gets the 2400 setting, which
then drives fp0 (PLL1/2) and fp2 (PLL1/1). With the 2666 VCO, fp0 is ≈1333 MT/s
and fp2 is 2666 MT/s.

Range checks never fail the boot (PI harvest path)
--------------------------------------------------

``k1_ddr_accept_*`` only print and count. A pass whose results fall outside the
reference ranges still returns 0 if the hardware reported no error. The count
appears as ``N item(s) out of range`` in the final line. With ``K1_INDEP_EYE``
those harvest checks are skipped on cold boot and on the re-train entry; the
boot fails only when the independent memory-pattern verify (or MR14) fails
(:doc:`acceptance-and-logging`).

``data_rate`` 1333 is the 2666 VCO at fp0
----------------------------------------

``k1_ddr_final_fp()`` parks 1333 at fp0 (the same MC bank and DFC entry as
1200). ``k1_ddr_pll_init()`` programmes the **2666** PLL1 pair, so fp0 runs at
≈1333 MT/s and the fp2 training pass runs at 2666 MT/s. Confirmed on the 8 GiB
Samsung (`logsRefs/026-indep-1333-vco-8gib`) and 16 GiB Micron
(`logsRefs/027-indep-1333-vco-16gib`) boards: PLL1 ``0x203c5555``, PI fp2
mins 9/8, independent verify OK. Logs 015 and 023 measured the previous
mapping (2400 VCO, 1200 MT/s at that request).

Mainline cannot see a failure
-----------------------------

The mainline SPL ignores the return value and, unless built with ``DEBUG``,
passes ``puts = NULL``, so a failing ``ddr_init`` is silent; the SPL's
magic-word test at address 0 is the only signal. The validation patch shows the
minimal change needed in ``spl.c`` to honour the return value
(:doc:`../uboot-spl/validation`).

The HW-DFC kick is a whole-word write
-------------------------------------

``k1_ddr_dfc()`` writes ``CC_AP = 1 << 22``, clearing every other bit of that
register, while the SW-FC kick in ``k1_ddr_pll_init()`` is a read-modify-write
of bit 26. The test model enforces the exact ``1 << 22`` value. Keep the two
forms as they are unless a measurement says otherwise.

Building the DFC snapshot switches the live bank
------------------------------------------------

``k1_ddr_dfc_table_load()`` reads the banked registers by temporarily rewriting
``CTL_FREQ[31:28]`` on the running MC, then restores it. It runs between passes
with the DRAM idle; it must not run while anything else uses the DRAM.

Timeouts and stack
------------------

A PI operation that never completes costs up to 1 s before the pass fails (the
engine has no hardware timeout; the budget is generous on purpose). Without a
clock (not the case in the image) the poll cap of 100 000 000 reads bounds every
wait. The worst-case stack use is about 2.1 KiB of the SPL's stack
(:doc:`build-and-image`); ``struct k1_ddrt_result`` (~400 bytes) is the main
contributor and the re-train entry holds two of them.

Limitations
===========

One rank: MISC entries of the DFC table keep the two-rank byte
--------------------------------------------------------------

``k1_tbl_data()`` adapts the rank byte only for entries targeting ``USER_0``,
``CA_CMD`` and ``PI_CMD``. The ``MISC`` entries (``0x13010000``, ``0x13008000``,
``0x13020000``) keep ``0x13``. Whether the MISC port uses that byte as a rank
selector is not known; one-rank operation is untested on hardware.

MR5 and MR8 come from rank 0, channel 0 only
--------------------------------------------

``k1_ddr_boot()`` reads MR5/MR8 once and applies the resulting geometry to both
ranks. Mixed parts per rank are not supported (the address-map code has one
geometry for both).

Supported MR8 values
--------------------

``k1_ddr_geometry()`` accepts x8 and x16 with density codes 0..6 (2–16 Gb per
channel) whose rank size has a CS-map code. It rejects 1 Gb (code 12), x8 12 Gb
(6144 MB per rank, no size code), larger densities and other widths, and the
boot stops at step ``address-map``.

The rank count is trusted
-------------------------

``cs_num`` comes from the caller (the SPL's EEPROM read, default 2). The firmware
does not probe for a second rank. A wrong ``cs_num`` shows up as a training or
DFC failure, not as a clear message. Observed on the 16 GiB Micron two-rank
package with SPL ``cs_num 1``: independent CS0 verify passed; a following PI
pass did not keep DRAM at ``0x40000000`` and ``0x1c0000000``. Repeating that
run after ``k1_set_mr14()`` followed ``cs_sel`` left those kept-errors
unchanged (`logsRefs/019-mr14-cs-sel-cs1-16gib`).

LPDDR4 (non-X) is implemented but disabled
------------------------------------------

The IO table, WDQS words, PHY LDO setting, fp3 pass plan and 2133 MT/s final fp
are all in place, but ``k1_ddr_boot()`` returns ``-ENOSYS`` for LPDDR4 and the
image hard-codes ``K1_DDR_LPDDR4X`` (the mainline ABI carries no memory type).
Missing pieces: an MR14 sweep range valid for LPDDR4 (the code sweeps 21..36),
possibly CA training (no CA stage exists in ``k1_ddrt.c``; ``PI_CA0/1`` are never
written) [spec: :dc:`ca-training`], and a build switch to produce an LPDDR4
image. See :dc:`configurations` for the derived LPDDR4 differences.

Open points
===========

Does the DFC engine replay the snapshot's banked values?
--------------------------------------------------------

The DFC table snapshot is taken once, before the second fp0 pass and before
fp1/fp2 are trained. Training later changes banked registers (``CTL_TXVREF``'s
MR14 field, ``PI_RD_CFG``). The test model does not replay the snapshot on a
DFC, so the tests cannot show whether the engine restores the older values on a
later DFC. The run-time guard is the MR14 check after every DFC
(``k1_boot_dfc()``), which only warns. Closing it: dump the banked registers
after each DFC on hardware, and teach the model the observed behaviour.

The model shares the code's understanding
-----------------------------------------

``model.c`` encodes the same reading of the specification as the code. It
catches ordering, timing and encoding mistakes against that reading, but not a
misunderstanding shared by both. Hardware runs through the vehicle are the check
for that (:doc:`../uboot-spl/validation`).

Gate range bounds differ between checks
---------------------------------------

``k1_ddr_accept_final()`` expects gate bytes of 33..40 at the final fp;
``k1_ddr_accept_pp0b_gate()`` expects 32..40 after the second fp0 pass. Both are
informational, but the bounds should come from one place if they are meant to be
the same range.

The re-train entry reads MRs without a discarded read
-----------------------------------------------------

``k1_blob_retrain()`` prints the mode registers before and after the pass with no
discarded first read. If it is ever called right after a hardware DFC, the first
value printed may be 0. It only affects the printout.

Source references to the specification
======================================

Comments in ``src/`` cite sections of the specification revision the code was
written against, as ``doc NN`` and ``P-NN`` amendment numbers. That numbering
does not correspond to :dc:`docsClean/ <README>`, whose files are named by
topic; the comments are kept as written. ``check_tables.py`` also parses that
revision's reStructuredText source, not the Markdown of :dc:`docsClean/
<README>`. Porting the checker to the Markdown tables, and updating the comment
references, would tie the code to the published specification.
