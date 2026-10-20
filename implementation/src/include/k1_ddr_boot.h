/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Thalles Campagnani */
/*
 * SpacemiT K1 LPDDR4X bring-up from a cold controller (docs 25..31):
 *
 *   PLL + SW-FC to fp0 -> Device_Init -> train(pp0) -> MR5/MR8
 *   -> [MC reset + Device_Init with x8 overlays] -> address map
 *   -> DFC table -> train(pp0) -> DFC(1) train(pp1) -> DFC(2) train(pp2)
 *   -> [MR21 if 16 GiB] -> DFC(final)
 *
 * LPDDR4 (non-X) adds DFC(3) train(pp3) between pp1 and pp2.
 */
#ifndef K1_DDR_BOOT_H
#define K1_DDR_BOOT_H

#include "k1_ddrt.h"

enum k1_ddr_type : uint8_t {
	K1_DDR_LPDDR4X,
	K1_DDR_LPDDR4,
};

enum k1_ddr_step : uint8_t {
	K1_STEP_NONE,
	K1_STEP_ARGS,
	K1_STEP_PLL,		/* PLL lock */
	K1_STEP_SWFC,		/* software frequency change to fp0 */
	K1_STEP_MC,		/* MC does not hold a write (no clock) */
	K1_STEP_DFI_INIT,	/* PHY init_start never reported done */
	K1_STEP_DRAM_INIT,	/* JEDEC init never reported done */
	K1_STEP_TRAIN,
	K1_STEP_MR_READ,
	K1_STEP_MAP,		/* MR8 density / width not supported */
	K1_STEP_DFC,
};

struct k1_ddr_boot_cfg {
	uintptr_t ddr;		/* 0xc0000000 */
	uintptr_t pmu;		/* 0xd4282800 */
	uintptr_t dfc_win;	/* 0xf0000000, where the DFC table is filled */
	uint32_t cs_num;	/* 1 or 2 */
	uint32_t data_rate;	/* MT/s as passed by the SPL */
	enum k1_ddr_type type;
	/* optional observer, called after every training pass */
	void (*on_pass)(void *ctx, const struct k1_ddrt_result *res,
			uint32_t pp);
	void *pass_ctx;
};

struct k1_ddr_pass_summary {
	int32_t rc;
	uint8_t boot_pp;
	uint8_t rx_vref_idx;
	uint8_t tx_vref_dq;
	uint8_t fail_stage;
	uint32_t phy_status;
	uint32_t gate_cs0_b0;
};

struct k1_ddr_boot_report {
	int32_t rc;
	enum k1_ddr_step fail_step;
	uint8_t final_pp;
	uint8_t mr5, mr8;
	bool byte_mode;
	uint32_t size_mb;	/* all ranks */
	uint32_t n_pass;
	struct k1_ddr_pass_summary pass[5];
	uint32_t status;	/* MC STATUS on return */
	uint32_t ctl_freq;
	uint32_t clksel;	/* PMU+0x3b4 on return */
	uint32_t total_us;
};

/*
 * Full bring-up. env->now_us is required: resets, ZQCAL and MR writes are
 * timed. Returns K1_DDRT_OK with DRAM usable at the final frequency point.
 */
[[nodiscard]] int k1_ddr_boot(const struct k1_ddr_boot_cfg *cfg,
			      const struct k1_ddrt_env *env,
			      struct k1_ddr_boot_report *rep);

/* data_rate -> fp left running at the end (doc 30) */
uint32_t k1_ddr_final_fp(enum k1_ddr_type type, uint32_t data_rate);

/* fps trained after the second pp0, in order; returns how many */
uint32_t k1_ddr_dfc_plan(enum k1_ddr_type type, uint8_t fps[3]);

const char *k1_ddr_step_name(enum k1_ddr_step step);

#endif /* K1_DDR_BOOT_H */
