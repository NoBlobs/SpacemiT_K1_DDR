/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Thalles Campagnani */
/*
 * Entry table of the K1 DDR firmware, linked at 0xc082d000.
 * Mainline SPL calls offset 0 with the ddr_init ABI; offset 4 is the
 * re-train entry used for validation on a controller that is already up.
 * Both are plain jumps so a0..a3 and ra reach the C functions untouched.
 */
	.section .text.entry, "ax", @progbits
	.option push
	.option norvc
	.globl	_start
_start:
	j	k1_blob_ddr_init	/* +0x00 */
	j	k1_blob_retrain		/* +0x04 */
	.balign	8
	.ascii	"K1DDRGPL"		/* +0x08 */
	.word	1			/* +0x10 layout version */
	.word	__blob_size		/* +0x14 */
	.option pop
