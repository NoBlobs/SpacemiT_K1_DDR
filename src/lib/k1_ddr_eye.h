/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Thalles Campagnani */
/*
 * Independent data-eye training by memory-pattern sweep.
 *
 * On a controller already brought up and mapped at frequency point `fp`, this
 * trains write leveling (`byte+0x24`), RX Vref (VREF0/VREF1), TX Vref (MR14),
 * then the RX/TX delay of every CS0/CS1 byte lane by moving data through DRAM
 * (CS1 at the mapped CPU base + 128 MiB, address-map.md),
 * without reading the PI harvest (eye-training.md, write-leveling.md, phy.md).
 * Delay and WL edges are found by a two-stage search from the PI seed: a
 * short pattern for the binary edges, then a longer pattern only at those
 * edges. D-cache is left off: Zicbom and T-Head `dcache.cipa` both trap
 * in this SPL/firmware slot (boot-interface.md).
 *
 * The DQS read-gate cannot be positioned from software (phy.md), so the gate
 * stays with the PI. With `K1_INDEP_EYE` this is the production data-eye
 * path; the PI is only bring-up, WL seed and gate.
 */
#ifndef K1_DDR_EYE_H
#define K1_DDR_EYE_H

#include "k1_ddrt.h"

void k1_ddr_eye_measure(uintptr_t ddr, uint32_t cs_num, uint32_t fp,
			const struct k1_ddrt_env *env);

/* CS1 CPU buffer: map start in 8 MiB units, plus the 2–4 GiB hole, plus 128 MiB. */
uint64_t k1_eye_cs1_cpu(uint32_t cs1_hi, uint32_t map);

#endif /* K1_DDR_EYE_H */
