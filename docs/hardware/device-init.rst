.. SPDX-License-Identifier: CC-BY-SA-4.0

===========
Device_Init
===========

*Device_Init* is the one-time programming that takes the MC and PHY from a
clocked-but-idle state to DRAM that is powered up and refreshing at fp0, ready
to train. The full contract — register values, timing tables, DFI and JEDEC
handshakes, ZQ and the mode-register order — is in :dc:`device-init`; the
firmware implements it in :doc:`../firmware/controller-init`.

Phases
======

#. **MC programming.** Write the frequency-independent globals and the four
   timing banks (with the IO rows computed from the memory type's electrical
   configuration, and the x8 overlays for byte-mode parts), then select fp0 and
   write the refresh words. The ``CTL0`` read-back confirms the MC is clocked.
#. **PHY analog.** Program the per-slice COMMON blocks (drive, ODT, Vref) and the
   PHY bias/LDO in the PMU; arm the COMMON blocks (write 0 then 1); set the
   per-slice and global PHY init bits (:dc:`phy`).
#. **DRAM power-up.** The DFI ``init_start`` handshake through ``PI_CMD`` /
   ``PI_STATUS``, then JEDEC init through ``USER_0`` waiting on STATUS, the first
   batch of mode-register writes, per-rank ZQ calibration (START, wait, LATCH,
   wait), and the second batch of mode-register writes (:dc:`mode-registers`).

Verification
============

After the three phases the controller must report fp0 selected
(``CTL_FREQ == bank 0``, ``PI_CFG[3:2] == 0``); the PHY init word is expected to
read ``0x100`` (a mismatch is only warned about). The exact wait masks and
budgets are in :dc:`device-init`; the firmware's checks and error steps
(``mc-program``, ``dfi-init``, ``dram-init``) are in
:doc:`../firmware/controller-init`.

Byte mode (x8)
==============

For x8 dies the sequence runs twice: a first Device_Init with x16 timings makes
MR8 readable, and after the geometry is known the MC is reset and Device_Init is
re-run with the x8 overlays applied to every bank (:dc:`configurations`,
:doc:`../firmware/boot-sequence`).
