.. SPDX-License-Identifier: CC-BY-SA-4.0

==============
Mode registers
==============

DRAM mode registers are read and written through the MC ``CA_CMD`` port, with
the result of a read appearing in ``MR_DATA``. The protocol, the per-die values
and the read quirks are in :dc:`mode-registers`.

Read and write
==============

* **Write (MRW):** ``CA_CMD = 0x10020000 | cs_sel << 24 | (mr & 0xff)``; there is
  no completion bit, so a fixed delay (``tMRW``) separates writes. The written
  value comes from the active timing bank.
* **Read (MRR):** ``CA_CMD = 0x10010000 | (cs+1) << 24 | ch << 18 | mr``, then
  wait for ``MR_DATA`` bit 31 and take the low byte. MRRs must not be issued in
  self-refresh.

The firmware wrappers are ``k1_ddr_mr_write()`` and ``k1_ddrt_mr_read()``
(:doc:`../firmware/controller-init`, :doc:`../firmware/training-pass`).

Registers the boot uses
=======================

* MR5 (manufacturer), MR6, MR8 (density and width — drives the address map,
  :dc:`address-map`), MR12, MR11, MR13, MR21, MR22, MR23 are written during
  Device_Init in two batches around ZQ calibration (:dc:`device-init`).
* **MR14** is the write-Vref register centred by write training; the firmware
  sweeps it (21..36 on LPDDR4X) and leaves the winner. The MC also keeps an MR14
  field in ``CTL_TXVREF[21:16]`` that must match what the dies hold; the MR14
  check compares them (:dc:`eye-training`, :doc:`../firmware/training-pass`).
* MR21 is written on 16 GiB parts.

Read quirks
===========

Two measured quirks matter to the boot (:dc:`mode-registers`): MR13 reads 0, and
the **first** MRR after a hardware frequency change to fp2 reads 0 whatever the
register. The firmware absorbs the latter with a discarded read after every DFC
(:doc:`../firmware/boot-sequence`).
