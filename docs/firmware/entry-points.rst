.. SPDX-License-Identifier: GPL-3.0-or-later

=============================
Entry points and runtime glue
=============================

The ``blob/`` directory turns the portable library into an image the SPL can
call. It contains the entry table (``start.S``), the two entry functions and the
environment glue (``blob.c``), and a minimal C runtime (``fmt.c``).

``start.S`` — entry table
=========================

.. code-block:: asm

   _start:
       j  k1_blob_ddr_init     /* +0x00 */
       j  k1_blob_retrain      /* +0x04 */
       .balign 8
       .ascii "K1DDRGPL"       /* +0x08 */
       .word 1                 /* +0x10 layout version */
       .word __blob_size       /* +0x14 */

Placed in ``.text.entry``, which the linker script forces to offset 0. No stack
setup and no register save happen here: the jumps keep the caller's ``ra``, so
the C function returns straight to the SPL.

``k1_blob_ddr_init`` — the mainline entry
=========================================

.. code-block:: c

   int k1_blob_ddr_init(uint64_t ddr_base, uint32_t cs_num, uint32_t data_rate,
                        void (*puts)(const char *s));

This is the function mainline U-Boot's SPL calls as ``ddr_init``
(:doc:`../uboot-spl/boot-interface`). It:

#. Builds a ``struct k1_ddrt_env`` on its stack:

   * ``now_us = blob_now_us``: reads the 64-bit ``mtime`` at ``0xe400bff8``
     (CLINT base ``0xe4000000`` + ``0xbff8``) and divides by 24 (24 MHz timer);
   * ``vlog = blob_vlog``: formats into a 200-byte stack buffer with
     ``k1_vsnprintf`` and hands it to ``puts``; returns immediately when ``puts``
     is ``NULL``, so a quiet SPL costs no formatting;
   * ``verbose = false``.

#. Builds a ``struct k1_ddr_boot_cfg`` with ``ddr = ddr_base``, the fixed
   ``pmu = 0xd4282800`` and ``dfc_win = 0xf0000000``, the caller's ``cs_num``
   and ``data_rate``, and **``type = K1_DDR_LPDDR4X``** (the ABI carries no
   memory type; this image is the LPDDR4X build).
#. Registers ``boot_on_pass`` as the ``on_pass`` observer, with a small
   ``struct boot_obs`` context (pass counter, an "out of range" counter, and
   with ``K1_INDEP_EYE`` the independent-eye return code).
#. Calls ``k1_ddr_boot()``.
#. On success, prints MR5/6/8/12/14 of every (rank, channel). With
   ``K1_INDEP_EYE``, re-centres the live frequency point
   (``k1_ddr_eye_measure``) unless an earlier independent train already
   failed, then replaces a 0 from ``k1_ddr_boot()`` with that eye return
   code if it is non-zero.
#. Prints a one-line summary and returns that ``rc``.

The pass observer
-----------------

``boot_on_pass`` runs after every training pass:

* prints ``k1-ddr: pass N ppX rc … stage … cs …: rx idx …, mr14 …,
  phy_status …, … us``;
* prints the per-stage PI statistics and the trained register state
  (``k1_ddr_print_pass_stats``, ``k1_ddr_print_result``);
* without ``K1_INDEP_EYE``: on the **second** pass (the second fp0 pass)
  runs the gate-range check ``k1_ddr_accept_pp0b_gate``; on a successful
  **fp2** pass runs ``k1_ddr_accept_final`` and, when the requested rate is
  2400, ``k1_ddr_accept_pp2_2400``. Those checks only count and print
  "out of range" items; they never change the return code
  (:doc:`acceptance-and-logging`).
* with ``K1_INDEP_EYE``: skip those harvest counts. After every mapped pass
  whose ``pp`` is not the final frequency point, run
  ``k1_ddr_eye_measure``. A non-zero return is stored and counted as an
  "out of range" item; later independent trains on this boot are skipped,
  but the PI sequence continues. The live fp is trained after the last DFC
  (see above).

Final summary line
------------------

.. code-block:: text

   k1-ddr: rc <rc> (<step>) in <us> us: <n> passes, MR5 0x.. MR8 0x..[ x8],
           <MB> MB, fp<final> ctl_freq 0x........ status 0x.. clksel 0x........,
           <k> item(s) out of range

Return value
------------

0 on success, a negative ``enum k1_ddrt_err`` otherwise. Mainline's SPL ignores
it; the validation SPL prints it and skips its DRAM test when it is non-zero
(:doc:`../uboot-spl/validation`).

``k1_blob_retrain`` — re-train entry (validation)
=================================================

.. code-block:: c

   int k1_blob_retrain(uint64_t ddr_base, uint32_t cs_num,
                       void (*puts)(const char *s), uint32_t flags);

At offset 4. Runs **one** training pass on the frequency point the controller is
currently using, on a controller that something else already brought up.
``flags`` bit 0 (``K1_RETRAIN_VERBOSE``) enables per-step logging.

Sequence:

#. Reads ``CTL_FREQ`` and ``PI_CFG``; derives ``pp = CTL_FREQ[31:30]`` and prints
   both, plus STATUS and the CS map words.
#. Refuses (``-EINVAL``) when ``PI_CFG[3:2] != pp``.
#. Warns when ``cs_num == 2`` but CS1 is not enabled in the map, and when the
   self-refresh bits of STATUS are already set (the SR handshake would not be
   observable).
#. Prints all mode registers and a register snapshot (``k1_ddrt_snapshot``,
   "before").
#. Runs ``k1_ddrt_train_pass()`` and prints its duration, the 16 Rx and 16 Tx
   per-step minima with the winners, the PI statistics and the "after" state.
#. Checks MR14 on every (rank, channel) against ``CTL_TXVREF[21:16]``
   (``k1_ddrt_check_mr14``) and prints the mode registers again.
#. Without ``K1_INDEP_EYE``, at fp2 runs ``k1_ddr_accept_final``
   (informational). With ``K1_INDEP_EYE`` that harvest print is omitted.
#. Returns the pass result, or the MR14 check result if the pass succeeded but
   the check failed.

It is used by the validation hook in U-Boot to re-train a controller the
proprietary firmware initialized, and to repeat passes on a controller this
firmware initialized (:doc:`../uboot-spl/validation`).

``fmt.c`` — freestanding runtime
================================

``memset``, ``memcpy``, ``memmove``
-----------------------------------

GCC may emit calls to these for struct copies and zeroing (for example
``*res = (struct k1_ddrt_result){}``), even with ``-fno-builtin``. They are byte
loops through a ``volatile`` pointer, which keeps GCC from recognizing the loop
and turning it back into a call to itself.

``k1_vsnprintf``
----------------

A small ``vsnprintf`` covering what the library's format strings use:

.. list-table::
   :header-rows: 1
   :widths: 34 66

   * - Feature
     - Supported
   * - conversions
     - ``%%``, ``%c``, ``%s`` (``NULL`` prints ``(null)``), ``%d``, ``%i``,
       ``%u``, ``%x``, ``%X``, ``%p``
   * - flags
     - ``-`` (left-justify), ``0`` (zero pad)
   * - width
     - decimal field width
   * - length modifiers
     - ``l``, ``ll``, ``z`` select 64-bit; ``h``, ``hh`` are accepted and ignored
   * - not supported
     - precision (``.N``), ``+``, space, ``#``, ``*`` width, floating point

An unknown conversion is printed literally (``%`` followed by the character).
The output is always NUL-terminated within ``size``, and the return value is the
length the full output would have had. In the image every line goes through a
200-byte buffer, so a single log call longer than 199 characters is truncated.

Fixed addresses in the image
============================

.. list-table::
   :header-rows: 1
   :widths: 34 30 36

   * - Constant
     - Value
     - Where
   * - load / link address
     - ``0xc082d000``
     - ``blob.ld``
   * - PMU base
     - ``0xd4282800``
     - ``blob.c`` (``K1_PMU_BASE``)
   * - DFC table window
     - ``0xf0000000``
     - ``blob.c`` (``K1_DFC_WIN``)
   * - ``mtime``
     - ``0xe400bff8``, 24 ticks/µs
     - ``blob.c``

Everything else is relative to ``ddr_base``.
