/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Thalles Campagnani */
/* Doc 21 ranges and the blob's UART dump of a training pass. */
#ifndef K1_DDR_ACCEPT_H
#define K1_DDR_ACCEPT_H

#include "k1_ddrt.h"

/* final frequency point (doc 21): WL, gate, Rx/Tx DQ0, PHY_STATUS, MR14 */
uint32_t k1_ddr_accept_final(const struct k1_ddrt_env *env,
			     const struct k1_ddrt_result *r,
			     uint32_t cs_num, int mr14_rc);

/* at 2400, first pp2 is the reference eye; 8/9 means the 2666 CK (P-16, P-21) */
uint32_t k1_ddr_accept_pp2_2400(const struct k1_ddrt_env *env,
				const struct k1_ddrt_result *r);

/* 2nd pp0: gate bytes ~32..40 (not the ~105–118 of the 1st pp0) */
uint32_t k1_ddr_accept_pp0b_gate(const struct k1_ddrt_env *env,
				 const struct k1_ddrt_result *r,
				 uint32_t cs_num);

void k1_ddr_print_result(const struct k1_ddrt_env *env, const char *label,
			 const struct k1_ddrt_result *r, uint32_t cs_num);
void k1_ddr_print_pass_stats(const struct k1_ddrt_env *env,
			     const struct k1_ddrt_result *r, uint32_t cs_num);
void k1_ddr_print_mrs(const struct k1_ddrt_env *env,
		      const struct k1_ddrt_cfg *cfg);

#endif /* K1_DDR_ACCEPT_H */
