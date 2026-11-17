/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Thalles Campagnani */
/*
 * The SPL copies the raw image to 0xc082d000 and only flushes the icache,
 * so there is no loader for .data/.bss: both must stay empty. The window
 * ends before the BootROM data near 0xc0838000.
 */
OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY
{
	sram (rwx) : ORIGIN = 0xc082d000, LENGTH = 0xb000
}

SECTIONS
{
	.text : {
		KEEP(*(.text.entry))
		*(.text .text.*)
	} > sram

	.rodata : ALIGN(8) {
		*(.rodata .rodata.* .srodata .srodata.*)
	} > sram

	.data : ALIGN(8) {
		*(.data .data.* .sdata .sdata.*)
	} > sram

	.bss (NOLOAD) : ALIGN(8) {
		*(.bss .bss.* .sbss .sbss.* COMMON)
	} > sram

	__blob_end = ADDR(.data) + SIZEOF(.data);
	__blob_size = __blob_end - ORIGIN(sram);

	/DISCARD/ : {
		*(.comment) *(.note .note.*) *(.eh_frame .eh_frame_hdr)
	}
}

ASSERT(SIZEOF(.data) == 0, "k1 ddr blob: .data must be empty")
ASSERT(SIZEOF(.bss) == 0, "k1 ddr blob: .bss must be empty")
ASSERT(_start == ORIGIN(sram), "k1 ddr blob: entry must be at offset 0")
