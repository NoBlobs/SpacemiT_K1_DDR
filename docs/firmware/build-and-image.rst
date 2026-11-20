.. SPDX-License-Identifier: GPL-3.0-or-later

===============
Build and image
===============

Toolchains
==========

GCC only, three of them:

.. list-table::
   :header-rows: 1
   :widths: 18 26 56

   * - Variable
     - Default
     - Builds
   * - ``CROSS_ELF``
     - ``riscv64-elf-``
     - the firmware image (bare metal)
   * - ``CROSS_LNX``
     - ``riscv64-linux-gnu-``
     - the test suite as a static rv64gc Linux binary, run under ``qemu-riscv64``
   * - ``HOSTCC``
     - ``gcc``
     - the test suite natively, with ASan/UBSan; ``dump_tables``
   * - ``QEMU``
     - ``qemu-riscv64``
     - runs the rv64gc test binary

The code needs a compiler with C23 support (``-std=c23``: ``constexpr``,
``nullptr``, ``[[nodiscard]]``, typed enums, digit separators).

Makefile targets
================

Run from ``src/`` (or with ``make -C src``):

.. list-table::
   :header-rows: 1
   :widths: 20 52 28

   * - Target
     - What it does
     - Output
   * - ``blob``
     - builds the firmware image
     - ``$(BUILD)/blob/k1_ddr_fw.elf``, ``.bin``, ``.map``
   * - ``test``
     - builds the test suite for rv64gc and runs it under QEMU
     - ``$(BUILD)/test/k1_ddrt_test.rv64``
   * - ``host-test``
     - builds and runs the same suite natively with
       ``-fsanitize=address,undefined``
     - ``$(BUILD)/test/k1_ddrt_test.host``
   * - ``host-test-exp``
     - builds one binary per error-injection flag and runs ``test_exp.c``
       against each
     - ``$(BUILD)/test/exp-<FLAG>.host``
   * - ``check-tables``
     - builds ``dump_tables`` and compares its output with the specification
       tables; needs ``SPEC_DOCS=``
     -
   * - ``mutate``
     - runs the mutation-testing script
     -
   * - ``all``
     - ``blob`` + ``test``
     -
   * - ``clean``
     - removes ``$(BUILD)/blob`` and ``$(BUILD)/test``
     -

Variables: ``BUILD`` (default ``../build``, i.e. ``build/``, ignored by git),
``TOOLS`` (default ``../tools``; the test sources are in ``$(TOOLS)/test``),
``BLOB_DEFS`` (defaults to ``-DK1_INDEP_EYE``, the production data-eye path;
passing an empty ``BLOB_DEFS=`` to ``make`` overrides that and drops the
independent eye — ``tools/vehicle/build-vehicle.sh`` therefore omits the
variable unless ``EXP=`` is set),
``SPEC_DOCS`` (directory of the specification in the format ``check_tables.py``
parses; quote it if the path has spaces).

The test sources include ``"../lib/…"``. Those includes resolve through the
``-Iinclude -Ilib`` search path of the Makefile, which is why the tests compile
from ``src/`` even though they live in ``tools/test/``.

Compiler flags of the image
===========================

.. code-block:: text

   -std=c23 -march=rv64gc -mabi=lp64d -mcmodel=medany
   -Wall -Wextra -Werror -Wshadow -Wundef -Wstrict-prototypes
   -Wmissing-prototypes -Wpointer-arith -Wvla -Wformat=2
   -Os -ffreestanding -fno-builtin -fno-pic -fno-pie -mno-relax
   -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables
   -ffunction-sections -fdata-sections -fno-common

Linked with ``-nostdlib -static -no-pie -T blob/blob.ld --gc-sections
--no-relax --build-id=none`` and ``-lgcc``. After linking, the Makefile fails
the build if ``nm -u`` reports any unresolved symbol, then ``objcopy -O binary``
produces the raw image.

Why these choices:

* ``-mcmodel=medany`` + ``-fno-pic``: the image runs at a fixed address
  (``0xc082d000``) above 2 GiB; medany allows PC-relative addressing there.
* ``-mno-relax`` / ``--no-relax``: no ``gp``-relative relaxation. The image runs
  on the SPL's registers, and U-Boot on RISC-V keeps its global-data pointer in
  ``gp``, so the image must neither use nor change it.
* ``-ffunction-sections`` + ``--gc-sections``: drops unused code (for example
  functions only the tests call).
* ``-fno-stack-protector``, no unwind tables: nothing in the image provides the
  runtime they need.
* ``-Werror`` plus the strict warnings: the image is small; warnings are bugs.

The test builds use ``-O2 -g -DK1_DDRT_MOCK`` with the same warnings.

Linker script (``blob/blob.ld``)
================================

.. code-block:: text

   MEMORY { sram (rwx) : ORIGIN = 0xc082d000, LENGTH = 0xb000 }
   .text   : KEEP(*(.text.entry)) then *(.text .text.*)
   .rodata : *(.rodata .rodata.* .srodata .srodata.*)
   .data   : must be empty      ASSERT(SIZEOF(.data) == 0)
   .bss    : must be empty      ASSERT(SIZEOF(.bss) == 0)
   ASSERT(_start == ORIGIN(sram))
   __blob_size = end of .data - origin

* The SPL copies the raw bytes and executes ``fence.i``; nothing initializes
  ``.data`` or zeroes ``.bss``, so both are forbidden and the build fails if any
  object lands there.
* ``.text.entry`` (from ``start.S``) is forced first so that offset 0 is the
  entry point.
* The ``0xb000``-byte window ends before the BootROM data area near
  ``0xc0838000``; the linker fails if the image outgrows it.
* ``__blob_size`` is exported into the header.

Image layout
============

.. code-block:: text

   offset  content
   0x00    j k1_blob_ddr_init      mainline entry (ddr_init ABI)
   0x04    j k1_blob_retrain       re-train entry (validation only)
   0x08    "K1DDRGPL"              magic, 8 ASCII bytes
   0x10    1                       layout version (u32)
   0x14    __blob_size             image size in bytes (u32)
   0x18    code (.text), then constant tables and strings (.rodata)

Both entries are plain ``j`` instructions assembled with ``.option norvc``, so
they are exactly 4 bytes each and ``a0..a3`` and ``ra`` reach the C functions
untouched; the C functions return directly to the SPL. The magic and size let a
loader verify the image (the validation hook in U-Boot checks the magic before
using the re-train entry, :doc:`../uboot-spl/validation`).

Size
====

Current build (GCC 16.2, ``-Os``):

.. list-table::
   :header-rows: 1
   :widths: 60 40

   * - Section
     - Bytes
   * - ``.text`` (code)
     - 13 782
   * - ``.rodata`` (tables, strings)
     - 6 376
   * - raw image
     - 20 160 (of 45 056 available)

The build is reproducible: with the same toolchain the image is byte-identical
across builds.

Stack budget
============

The firmware has no stack of its own; it runs on the SPL's stack in SRAM. Frame
sizes from ``-fstack-usage`` (largest first):

.. list-table::
   :header-rows: 1
   :widths: 70 30

   * - Function
     - Frame
   * - ``k1_blob_retrain``
     - 944 B (two ``struct k1_ddrt_result``)
   * - ``k1_boot_pass``
     - 464 B (one ``struct k1_ddrt_result``)
   * - ``k1_blob_ddr_init``
     - 352 B (boot report)
   * - ``k1_ddr_boot``
     - 304 B
   * - ``k1_ddr_dfc``, ``blob_vlog`` (200-byte line buffer)
     - 224 B each
   * - ``k1_ddrt_train_pass``
     - 208 B
   * - ``k1_ddr_device_init``
     - 192 B
   * - ``k1_stage_rx``, ``k1_stage_tx``
     - 176 B each
   * - ``k1_vsnprintf``
     - 144 B

GCC's call-graph output does not follow the indirect calls (the logging sink,
the stage table and the ``on_pass`` observer). Adding them by hand, the deepest
chain is a log line from inside a training stage during a cold boot:

.. code-block:: text

   k1_blob_ddr_init 352 -> k1_ddr_boot 304 -> k1_boot_pass 464 ->
   k1_ddrt_train_pass 208 -> k1_stage_tx 176 -> k1_fail 48 -> k1_log 80 ->
   blob_vlog 224 -> k1_vsnprintf 144 -> k1_put_field 64      ~ 2.1 KiB

The re-train entry peaks at about 1.9 KiB. Keep the SPL stack comfortably above
2 KiB of free space when calling the image; adding fields to
``struct k1_ddrt_result`` grows the two largest frames directly.

To reproduce the measurement, build a copy with
``make blob BLOB_DEFS="-fstack-usage -fcallgraph-info=su"`` (the ``.su``/``.ci``
files land next to the ELF).

Using the image with mainline U-Boot
====================================

.. code-block:: sh

   make -C src blob
   export DDR_FW_FILE=$PWD/build/blob/k1_ddr_fw.bin
   # in the U-Boot tree:
   make spacemit_k1_defconfig && make      # -> u-boot-spl-ddr.bin

Mainline's binman places the file after the SPL, 64-byte aligned, and the SPL
copies it to ``0xc082d000`` at run time. The build-time wiring is described in
:doc:`../uboot-spl/integration`.
