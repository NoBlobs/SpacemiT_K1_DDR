.. SPDX-License-Identifier: CC-BY-SA-4.0

===============
Training engine
===============

Each frequency point must have its data eye centred before it can be used. The
K1 does this with a PI (PHY-independent) training sequencer inside the MC: the
firmware arms it, issues one operation per stage, and the engine drives the bus
and writes back per-DQ margins. The firmware treats the sequencer's internal
edge search as a black box (:dc:`pi-training-engine`); the clean-room split and
an independent memory-pattern cross-check are in :dc:`methodology` and
:dc:`eye-training`.

The PI engine
=============

Operations are issued on the ``PI_CMD`` port with a 1-based rank field; each has
a *done* mask in ``PI_STATUS`` that the firmware polls (the engine has no
hardware timeout, so the budget is generous). A command clears its own done bits;
a done bit still set immediately after issuing is a leftover from an earlier
operation. CFG registers (``PI_WL_CFG``, ``PI_RD_CFG``, ``PI_WR_CFG``) are
written once before each stage. See :dc:`pi-training-engine` and
:doc:`../firmware/training-pass`.

The stages
==========

A pass runs four stages in a fixed order, with the ranks in self-refresh:

.. list-table::
   :header-rows: 1
   :widths: 26 74

   * - stage
     - what it centres
   * - write leveling
     - the DQS-to-clock phase per byte (:dc:`write-leveling`)
   * - read gate (DQS gate)
     - the read DQS gating window per byte; a timeout here stops the pass
       (:dc:`read-gate`)
   * - read training
     - the read eye: the PHY Vref is swept over 16 steps and the best step kept
       (:dc:`eye-training`)
   * - write training
     - the write eye: MR14 is swept (21..36 on LPDDR4X) and the best value kept
       (:dc:`eye-training`)

CA training exists in the engine but is unused by the LPDDR4X boot
(:dc:`ca-training`).

Vref selection (max-min)
========================

For both the read and write sweeps the firmware takes, at each Vref step, the
worst per-DQ margin over every rank and DQ, and picks the step with the largest
of those minima — on a plateau the lowest step wins. This *max-min* rule is the
specified selection (:dc:`eye-training`) and is shared by the firmware and its
tests (:doc:`../firmware/training-pass`). A pass succeeds only when every gate
run passed and ``PHY_STATUS`` reads 0 after the sweeps.

Acceptance ranges
=================

The trained WL, gate, read and write taps of the reference boards define the
acceptance ranges the firmware checks informationally (:dc:`reference-measurements`,
:doc:`../firmware/acceptance-and-logging`).
