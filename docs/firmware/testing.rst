.. SPDX-License-Identifier: GPL-3.0-or-later

=======
Testing
=======

The firmware is tested without hardware by compiling the library against a
behavioural model of the controller, PHY, PI engine and PMU. The test sources,
the model and the scripts live in ``tools/test/``.

.. list-table::
   :header-rows: 1
   :widths: 34 66

   * - command
     - what it proves
   * - ``make host-test``
     - the suite, natively, under AddressSanitizer and UndefinedBehaviorSanitizer
   * - ``make test``
     - the same suite compiled for rv64gc and run under ``qemu-riscv64`` (same
       ISA and ABI as the image)
   * - ``make host-test-exp``
     - each error-injection flag produces exactly the failure it is meant to
       produce
   * - ``make mutate``
     - the suite actually detects deliberate bugs (122 mutants)
   * - ``make check-tables SPEC_DOCS=…``
     - every register value in the code matches the specification tables
       (265 facts)

A full run of the suite prints one ``ok``/``FAIL`` line per test and ends with
``1578 checks, 0 failures``. Independent-eye cases include a 2-rank train, a
linear fallback from a seed outside the window, and a forced pattern mismatch
that must return ``K1_DDRT_EIO``.

How the code is connected to the model
======================================

``lib/k1_ddrt_io.h`` has two implementations of ``k1_r32``/``k1_w32``. The test
builds define ``K1_DDRT_MOCK``, which turns every register access into a call to
``k1_mock_read32``/``k1_mock_write32`` in ``model.c``. Nothing else changes: the
same ``lib/`` sources run in the image and in the tests, at the same addresses
(``DDR = 0xc0000000``, ``PMU = 0xd4282800``, ``WIN = 0xf0000000`` in
``check.h``). ``blob/`` is not part of the test build.

Time comes from ``model_now_us()``, which advances a simulated clock by
``time_step_us`` (default 7 µs) on every call, so timeouts and delays are
deterministic.

The model (``model.c``, ``model.h``)
====================================

The model is a **test double for the algorithm, not a simulator of the
silicon**. It keeps register arrays for the MC+PHY window (with four banked
copies of the per-fp registers, selected by ``CTL_FREQ[31:30]`` like the real
MC) and for the PMU, and reacts to writes.

Two starting states
-------------------

.. list-table::
   :header-rows: 1
   :widths: 40 60

   * - function
     - state
   * - ``model_reset(ddr, boot_pp, real_cs)``
     - controller already up at ``boot_pp``, STATUS ``0x33``, PI idle;
       **strict** mode: only the writes a training pass is allowed to make are
       accepted
   * - ``model_reset_por(ddr, real_cs)``
     - cold SoC: PLLs at the values found at power-on, MC in reset, dies not
       initialized, PHY at reset values, IO words unknown (``0xa5a5a5a5``)

What it simulates
-----------------

* **PLLs and kicks**: lock appears a few CLKSEL reads after valid enable + VCO
  words; SW-FC and HW-DFC kicks complete a few ``CC_AP`` reads later.
* **MC reset**: clearing and setting ``HW_SLP`` bit 21 wipes MC/PHY state and the
  DFC table, and must last ≥100 µs.
* **Device_Init**: COMMON arming (0 then 1), DFI ``init_start`` handshake, JEDEC
  init setting STATUS, MR writes, ZQ START/LATCH timing.
* **DFC engine**: records every table line written through the window (or through
  the MC port when ``win_alias`` is set) and checks each snapshot line against
  its own copy of the layout as it is written. A kick with an incomplete table
  parks the ranks (STATUS SR bits set), keeps bit 22 and leaves ``CTL_FREQ``
  untouched; a complete one switches fp, updates ``CTL_FREQ``/``PI_CFG``/STATUS
  and re-sends MR14 from the target bank.
* **PI engine**: each operation clears its done bits and completes after
  ``latency`` status reads, writing taps and harvest from per-test margin
  functions (``rx_margin``, ``tx_margin``); defaults put the Rx peak at index 5
  and the Tx peak at index 7, with the worst DQ moving with step and rank.
* **Independent eye**: geometric RX/TX/WL windows for the memory-pattern
  path; ``eye_force_fail`` makes every pattern mismatch.
* **Mode registers**: MR5/6/8/12 values of an x8 8 Gb part, MR14 per die and
  channel following the MR14 commands and DFCs, MR13 reading 0, and the first
  MRR after the first DFC to fp2 reading 0.

Knobs (per test)
----------------

``gate_timeout_cs``, ``hang_op``, ``arm_rl_in_sweep``, ``sr_enter_stuck``,
``final_status_bits``, ``latency``, ``clear_delay`` (stale done bits after a
command), ``pll_never_locks``, ``swfc_stuck``, ``dfc_stuck``, ``dfi_stuck``,
``dram_init_stuck``, ``pmu_delay``, ``freq_read_or`` (stuck bits on ``CTL_FREQ``
reads), ``win_alias``, ``dfc_no_mr14``, ``eye_force_fail``, ``time_step_us``,
``rx_peak``,
``tx_peak``, custom margin functions, and ``mr[]`` contents (e.g. MR8).

Violations
----------

Every rule the code must respect is a check inside the model. Breaking one
increments ``g_model.violations`` and records the first message; tests end with
``CHECK_CLEAN()`` (zero violations). Examples:

* any MC/PHY access before the SW-FC and MC clock enable, or within 100 µs of an
  MC reset release;
* PLL words rewritten while locked; PLL enabled without bit 11 or before its VCO
  words; a SW-FC with the wrong ``HW_SLP``/CLKSEL;
* a HW-DFC kick that is not exactly ``1 << 22``, with ranks in self-refresh,
  without ``ddr+0x148 = 0x80ac0000``, with the DCLK IRQ unmasked, with a wrong
  ``HW_SLP`` or CLKSEL; writing ``CC_AP`` while bit 22 is stuck;
* ``init_start`` before every COMMON is armed; JEDEC init before the DFI
  handshake; ZQ LATCH less than 2 µs after START; back-to-back MR writes; MR
  writes right after a ZQ LATCH;
* in strict (training-only) mode: any PMU write, any MC write other than PI
  commands/CFG, USER_0, CA_CMD and the MR14 field of ``TXVREF``, any PHY write
  other than ``PHY_INIT``, the bit-2 clear of ``PHY_STATUS`` and the two Vref
  words (whose low half must stay ``0xa927``/``0xa937``);
* PI commands outside self-refresh, while an operation is running, or with a
  rank field outside 1..``real_cs``; reading MRs in self-refresh; an RD with
  VREF1 different from VREF0 or with bit 4 set.

Observations
------------

The model records an event trace (``EV_SR_ENTER``, ``EV_OP_WL``, ``EV_DFC``, …),
per-event and per-rank counters, the MR/ZQ commands issued, the table lines
received, the analog markers, the DFC targets and every MR14 value read (with
the number of DFCs and SR entries before it). Tests assert on these.

Test harness (``check.h``, ``test_main.c``)
===========================================

``CHECK(c)``, ``CHECK_EQ(a, b)`` and ``CHECK_CLEAN()`` count checks and print
``FAIL file:line`` without stopping. ``env_timed()`` gives an environment with
the model clock and a log sink; ``log_has()`` searches everything logged since
``log_clear()`` (first 64 KiB). Run with ``-v`` to echo the log.

Test files: ``test_main.c`` (training pass), ``test_boot.c`` (full bring-up) and
``test_accept.c`` (range checks and printing). Each test starts from
``model_reset()`` or ``model_reset_por()``, sets knobs, runs, asserts with
``CHECK*`` and ends with ``CHECK_CLEAN()``.

Error-injection builds (``test_exp.c``, ``make host-test-exp``)
===============================================================

Each of ``K1_EXP_PLL_NO_LOCK``, ``K1_EXP_GATE_TIMEOUT`` and
``K1_EXP_DFC_TIMEOUT`` is compiled into its own host binary with ``test_exp.c``,
which runs a full boot against the model and checks the return code, the failing
step, the number of passes and the log lines. The regular suite must not define
these flags; the same flags are what the validation vehicle builds into images
for the board (``EXP=…``, :doc:`../uboot-spl/validation`).

.. list-table::
   :header-rows: 1
   :widths: 40 60

   * - flag
     - expected
   * - ``K1_EXP_PLL_NO_LOCK``
     - ``-ETIMEDOUT``, step ``pll-lock``, 0 passes, no MC write
   * - ``K1_EXP_GATE_TIMEOUT``
     - ``-EIO``, step ``training``, 1 pass failing in ``read-gate``, no DFC
   * - ``K1_EXP_DFC_TIMEOUT``
     - ``-ETIMEDOUT``, step ``dfc``, 2 passes, "DFC to fp1 not done"

Mutation testing (``mutate.py``)
================================

122 mutants, each a ``(name, file, old text, new text)`` substitution in one
source file. For each mutant the runner copies ``src/`` and ``tools/`` to a
temporary directory, applies the substitution under ``src/``, and runs
``make test`` there (with ``WARN=`` so a mutant that only triggers a warning
still builds). A mutant is *killed* when the suite fails; the run succeeds only
if all are killed.

.. code-block:: sh

   python3 tools/test/mutate.py            # all mutants, parallel (-j N)
   python3 tools/test/mutate.py --check    # only verify each anchor occurs once
   python3 tools/test/mutate.py pll        # mutants whose name contains "pll"

Every anchor must match exactly once (``--check``); refactoring a mutated line
requires updating its mutant.

Table check (``dump_tables.c``, ``check_tables.py``)
====================================================

``dump_tables`` prints every table fact (:doc:`tables`). ``check_tables.py
<dump_tables> <spec dir>`` extracts the same facts from the specification source
(reStructuredText simple tables and code blocks of the specification revision
the code was written against, ignoring implementer notes), and reports facts
present only in the code, only in the specification, or duplicated. ``make
check-tables`` needs ``SPEC_DOCS`` pointing to that source; the parser does not
read the Markdown in :dc:`docsClean/ <README>`.
