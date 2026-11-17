.. SPDX-License-Identifier: GPL-3.0-or-later

============
Architecture
============

The firmware is a small freestanding C23 program, split into four layers; each
layer calls only the layers below it.

.. code-block:: text

   blob/            entry table, ABI glue, logging sink, vsnprintf, mem*   (image only)
     start.S  blob.c  fmt.c  blob.ld
   ------------------------------------------------------------------------
   lib/k1_ddr_boot.c    boot sequence: which step, in which order
   lib/k1_ddr_accept.c  range checks and UART dumps (informational)
   ------------------------------------------------------------------------
   lib/k1_ddr_init.c    PLL, SW-FC, Device_Init, MC reset, map, DFC        (hw drivers)
   lib/k1_ddrt.c        one training pass, MR read, wait primitives
   ------------------------------------------------------------------------
   lib/k1_ddr_tables.c  register values, command flows, IO configs         (data)
   lib/k1_ddrt_regs.h   register offsets, fields, magic values
   lib/k1_ddrt_io.h     k1_r32 / k1_w32 / k1_rmw32 (real or mock)

``lib/`` and ``include/`` are the portable part: they compile both into the
RISC-V image and natively against the test model. ``blob/`` exists only in the
image.

Files and public API
=====================

.. list-table::
   :header-rows: 1
   :widths: 24 30 46

   * - File
     - Role
     - Public API
   * - ``include/k1_ddrt.h``
     - Training-pass interface
     - ``k1_ddrt_train_pass``, ``k1_ddrt_mr_read``, ``k1_ddrt_check_mr14``,
       ``k1_ddrt_snapshot``, ``k1_ddrt_max_min``, ``k1_ddrt_stage_name``
   * - ``include/k1_ddr_boot.h``
     - Full bring-up interface
     - ``k1_ddr_boot``, ``k1_ddr_final_fp``, ``k1_ddr_dfc_plan``,
       ``k1_ddr_step_name``
   * - ``lib/k1_ddr_init.h``
     - Building blocks used by the boot sequence and the tests
     - ``k1_ddr_hw_init``, ``k1_ddr_pll_init``, ``k1_ddr_device_init``,
       ``k1_ddr_mc_reset``, ``k1_ddr_size_code``, ``k1_ddr_geometry``,
       ``k1_ddr_map_apply``, ``k1_ddr_dfc_table_load``, ``k1_ddr_dfc``,
       ``k1_ddr_mr_write``
   * - ``lib/k1_ddr_tables.h``
     - Table types and declarations
     - ``k1_mc_globals``, ``k1_mc_timing``, ``k1_mc_x8_overlay``,
       ``k1_dfc_flow`` …
   * - ``lib/k1_ddr_accept.h``
     - Range checks and printing
     - ``k1_ddr_accept_final``, ``k1_ddr_accept_pp2_2400``,
       ``k1_ddr_accept_pp0b_gate``, ``k1_ddr_print_*``
   * - ``lib/k1_ddrt_priv.h``
     - Shared helpers
     - ``k1_log``, ``k1_now``, ``k1_wait``, ``k1_wait_n``, ``k1_udelay``
   * - ``lib/k1_ddrt_regs.h``
     - Register map as ``static constexpr`` constants
     - ``k1_freq_bank``, ``k1_cs_sel``, ``k1_pack_vref`` inline helpers
   * - ``lib/k1_ddrt_io.h``
     - MMIO accessors
     - ``k1_r32``, ``k1_w32``, ``k1_rmw32``

Call flow of a cold boot
========================

.. code-block:: text

   SPL --jump 0xc082d000--> _start (start.S)
       k1_blob_ddr_init (blob.c)            builds env + cfg
         k1_ddr_boot (k1_ddr_boot.c)
           k1_ddr_hw_init
           k1_ddr_pll_init        PLL1/PLL2, SW-FC fp0, MCK
           k1_ddr_device_init     MC, PHY analog, DFI, JEDEC, MR, ZQ
           k1_boot_pass(0)  -> k1_ddrt_train_pass
           k1_ddrt_mr_read MR5, MR8
           k1_ddr_geometry
           [x8] k1_ddr_mc_reset + k1_ddr_device_init(x8)
           k1_ddr_map_apply
           k1_ddr_dfc_table_load
           k1_boot_pass(0)
           for fp in plan: k1_boot_dfc(fp) + k1_boot_pass(fp)
           [16 GiB] k1_ddr_mr_write(MR21)
           k1_boot_dfc(final fp)

After every pass the boot sequence calls the optional ``on_pass`` observer; the
image uses it to print the pass and run the range checks
(:doc:`entry-points`). The order of the sequence and the hardware steps it
drives are the clean-room boot sequence [spec: :dc:`boot-sequence`].

Data types
==========

Configuration and environment
-----------------------------

.. list-table::
   :header-rows: 1
   :widths: 26 46 28

   * - Type
     - Fields
     - Used by
   * - ``struct k1_ddrt_cfg``
     - ``ddr`` (MC base), ``cs_num`` (1–2), ``boot_pp`` (0–3, already active)
     - one training pass, MR reads
   * - ``struct k1_ddr_boot_cfg``
     - ``ddr``, ``pmu``, ``dfc_win``, ``cs_num``, ``data_rate``, ``type``,
       ``on_pass``, ``pass_ctx``
     - full bring-up
   * - ``struct k1_ddrt_env``
     - ``now_us()``, ``vlog()``, ``ctx``, ``poll_limit``, ``verbose``
     - everything that waits or logs
   * - ``struct k1_ddr_hw`` (internal)
     - ``ddr``, ``phy``, ``pmu``, ``win``, ``cs_num``, ``cs_sel``, ``type``,
       ``io``, ``env``
     - ``k1_ddr_init.c``

The environment is the only way the library reaches the outside world:

* ``now_us`` is a monotonic microsecond clock. Without it, waits are bounded
  only by a poll count and delays are no-ops. ``k1_ddr_boot()`` refuses to run
  without it (resets, ZQ and MR writes are timed).
* ``vlog`` is a printf-like sink; ``nullptr`` discards all output.
* ``poll_limit`` caps the number of status reads per wait (default
  100 000 000); it is the safety net when no clock is available.
* ``verbose`` adds one log line per Vref step and rank.

Results
-------

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Type
     - Content
   * - ``struct k1_ddrt_result``
     - everything a pass produced: rc, failing stage and rank, winning Rx index
       and MR14, PHY status, last PI status, per-step minima (all ranks and per
       rank), STATUS around SR, PI statistics per stage, final Vref words,
       WL/gate/RX/TX/DM taps per rank and byte, per-stage durations
   * - ``struct k1_ddrt_pi_stats``
     - per stage: operations issued, "stale first read" count, max polls, max µs
   * - ``struct k1_ddr_boot_report``
     - rc, failing step, final fp, MR5/MR8, byte mode, size, up to 5 pass
       summaries, STATUS/CTL_FREQ/CLKSEL on return, total µs
   * - ``struct k1_ddr_pass_summary``
     - per pass: rc, pp, Rx index, MR14, failing stage, PHY status, gate code of
       CS0 byte 0

Error model
===========

All functions return an ``int`` from ``enum k1_ddrt_err``:

.. list-table::
   :header-rows: 1
   :widths: 20 10 70

   * - Code
     - Value
     - Meaning in this code
   * - ``K1_DDRT_OK``
     - 0
     - success
   * - ``K1_DDRT_EIO``
     - −5
     - hardware answered but wrongly: gate timeout or untrained code,
       ``PHY_STATUS != 0``, MR14 mismatch, MC not holding writes, DFC landed on
       the wrong bank, DFC refused
   * - ``K1_DDRT_EINVAL``
     - −22
     - bad arguments, or an MR8 the address map does not support
   * - ``K1_DDRT_ENOSYS``
     - −38
     - LPDDR4 (non-X) requested: not enabled in ``k1_ddr_boot()``
   * - ``K1_DDRT_ETIMEDOUT``
     - −110
     - a wait expired (PLL lock, SW-FC, DFI init, JEDEC init, SR, PI op, MR
       read, DFC)

Where it failed is recorded separately:

* ``enum k1_ddrt_stage`` (inside a pass): ``args``, ``sr-enter``,
  ``write-leveling``, ``read-gate``, ``read-training``, ``write-training``,
  ``sr-exit``, ``phy-status``, ``mode-register``.
* ``enum k1_ddr_step`` (in the boot): ``args``, ``pll-lock``, ``sw-fc``,
  ``mc-program``, ``dfi-init``, ``dram-init``, ``training``, ``mr-read``,
  ``address-map``, ``dfc``.

Both have ``*_name()`` functions used in the log.

Design principles
=================

* **No writable static data.** The loader copies a raw image and never
  initializes ``.data`` or ``.bss``; the linker script asserts both are empty.
  All state lives in stack structures passed by pointer (``struct k1_pass``,
  ``struct k1_boot``, ``struct k1_ddr_hw``). Constant tables are ``const`` and
  land in ``.rodata``.
* **Freestanding.** ``-ffreestanding -fno-builtin``, no libc: the image
  provides its own ``memset``/``memcpy``/``memmove`` and a ``vsnprintf`` subset
  (:doc:`entry-points`).
* **Data separated from code.** Every register value that comes from the
  specification lives in ``k1_ddr_tables.c`` or as a named constant in
  ``k1_ddrt_regs.h``; the code only sequences them. This is what lets
  ``dump_tables`` print the values for comparison with the specification
  (:doc:`tables`).
* **Addresses relative to bases.** MC and PHY addresses derive from the ``ddr``
  argument; only the PMU base, the DFC window and ``mtime`` are fixed in
  ``blob.c``. The tests run the whole library at the same addresses against a
  model, and one test checks that nothing depends on ``0xc0000000``.
* **One MMIO choke point.** All register traffic goes through
  ``k1_r32``/``k1_w32``. In the image they are volatile accesses wrapped in
  ``fence iorw, iorw``; with ``-DK1_DDRT_MOCK`` they call the test model instead
  (:doc:`testing`).
* **Every wait is bounded** twice: by a time budget (when ``now_us`` exists)
  and by a poll cap.
* **Fail fast, report precisely.** A failing step returns immediately with its
  stage/step recorded; cleanup that protects the DRAM still runs (self-refresh
  exit after a failed pass, SR exit after a stuck DFC).
* **C23.** ``constexpr``, ``nullptr``, ``[[nodiscard]]``, enums with fixed
  underlying types, digit separators, ``static_assert`` on table layouts.
