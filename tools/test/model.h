/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Thalles Campagnani */
/*
 * Behavioural model of the K1 MC + PHY + PI engine + DDR PMU as described by
 * cleanroom/docs. It is a test double for the algorithm, not a simulator
 * of the silicon: the PI "finds" eyes from per-test margin functions, and
 * the bring-up side only enforces the order and timing rules of the docs.
 */
#ifndef K1_TEST_MODEL_H
#define K1_TEST_MODEL_H

#include <stdint.h>

enum model_ev : uint8_t {
	EV_SR_ENTER,
	EV_SR_EXIT,
	EV_PHY_INIT,
	EV_CFG_WL,
	EV_CFG_RD,
	EV_CFG_WR,
	EV_OP_WL,
	EV_OP_GATE,
	EV_OP_RD,
	EV_OP_WR,
	EV_MR14_SET,
	EV_MR_READ,
	EV_PLL_LOCK,
	EV_SWFC,
	EV_DFI_INIT,
	EV_DRAM_INIT,
	EV_MR_WRITE,
	EV_ZQ,
	EV_MC_RESET,
	EV_DFC,
	EV_COUNT,
};

struct model;
typedef uint8_t (*model_margin_fn)(const struct model *m, uint32_t step,
				   uint32_t cs, uint32_t dq);

struct model_tbl {
	uint32_t data;
	uint32_t addr;
	bool set;
	bool ok;		/* snapshot line matched the live MC when written */
};

struct model {
	uintptr_t ddr;
	uintptr_t pmu;
	uintptr_t win;		/* DFC table window */
	uint32_t boot_pp;	/* frequency point running now */
	uint32_t real_cs;
	bool strict;		/* training only: nothing outside its write set */

	/* knobs */
	model_margin_fn rx_margin;	/* step = PHY Vref index 0..15 */
	model_margin_fn tx_margin;	/* step = MR14 - 21 */
	uint32_t rx_peak, tx_peak;
	uint32_t gate_timeout_cs;	/* 1-based, 0 = never */
	uint32_t hang_op;		/* EV_OP_* that never completes, 0 = none */
	bool arm_rl_in_sweep;		/* set PHY status bit 2 at Rx step 15 */
	bool sr_enter_stuck;		/* STATUS never reports the ranks parked */
	uint32_t final_status_bits;	/* OR'd into PHY status by the last WR */
	uint32_t latency;		/* PI_STATUS polls before done */
	uint32_t clear_delay;		/* PI_STATUS reads still showing pre-command done bits */
	uint64_t time_us;
	uint32_t time_step_us;
	bool pll_never_locks;
	bool swfc_stuck;
	bool dfc_stuck;
	bool dfi_stuck;
	bool dram_init_stuck;
	uint32_t pmu_delay;		/* CLKSEL / CC_AP reads before lock or kick done */
	uint32_t freq_read_or;		/* stuck bits on CTL_FREQ reads */
	bool win_alias;			/* the window is the MC: ddr+0x70 feeds the engine too */
	bool dfc_no_mr14;		/* the DFC engine leaves MR14 alone */
	bool eye_force_fail;		/* independent pattern always mismatches */
	/* training state */
	bool in_sr;
	bool hung;
	uint32_t pending_ev;
	uint32_t pending_cs;
	uint32_t countdown;
	uint32_t stale_left;
	uint32_t stale_status;
	uint8_t die_mr14[2][2];		/* MR14 as an MRR reads it */
	bool dfc2_seen;			/* fp2 has been entered by a DFC */
	bool mrr_zero_next;		/* the next MRR reads 0 (doc 08, P-19) */
	uint8_t mr[256];

	/* bring-up state */
	bool locked, swfc_done, mck_on, in_reset, reset_pulse, after_reset;
	bool dfi_started, dfi_cleared, dram_ready, dfc_pre;
	bool com_zeroed[4][2], com_armed[4][2];	/* COMMON+0 arms on 0 then 1 */
	uint32_t lock_left, kick_left, kick_bits, dfc_target;
	bool dfc_hung;			/* engine parked the ranks, bit 22 held */
	uint64_t reset_at, release_at, last_mrw_us, zq_start_us, zq_latch_us;
	uint32_t zq_cs;
	uint32_t tbl_data, tbl_addr, mark_data, mark_addr;
	uint32_t banked[4][32];

	/* observations */
	uint32_t sr_enter_cmd, sr_exit_cmd;
	uint32_t mc_reset_count;
	uint32_t ev_count[EV_COUNT];
	uint32_t op_cs_count[EV_COUNT][3];
	enum model_ev trace[8192];
	uint32_t n_trace;
	uint32_t violations;
	char violation[160];
	uint32_t mmio_writes;
	uint32_t mc_writes;		/* MC + PHY window */
	struct model_tbl tbl[0x300];	/* what reached the engine */
	uint32_t tbl_writes;
	char tbl_why[64];		/* why the last HW-DFC hung */
	uint32_t marks[8][3];		/* analog markers that reached the engine */
	uint32_t n_marks;
	uint32_t mr_cmd[64];		/* MR writes outside SR */
	uint32_t n_mr;
	struct {
		uint8_t val;
		uint8_t n_dfc, n_sr;	/* DFCs and SR entries before the read */
	} mr14_read[64];		/* MR14 values the dies returned */
	uint32_t n_mr14_read;
	uint32_t zq_cmd[16];
	uint32_t n_zq;
	uint32_t dfc_fp[8];
	uint32_t n_dfc;
};

extern struct model g_model;

/* controller already up at boot_pp, training only (strict) */
void model_reset(uintptr_t ddr, uint32_t boot_pp, uint32_t real_cs);
/* cold SoC: PLLs off, MC in reset, dies not initialised */
void model_reset_por(uintptr_t ddr, uint32_t real_cs);

uint32_t model_peek(uintptr_t addr);
void model_poke(uintptr_t addr, uint32_t val);
uint32_t model_peek_bank(uint32_t fp, uint32_t off);
uint64_t model_now_us(void *ctx);

uint8_t model_default_rx_margin(const struct model *m, uint32_t step,
				uint32_t cs, uint32_t dq);
uint8_t model_default_tx_margin(const struct model *m, uint32_t step,
				uint32_t cs, uint32_t dq);

/* Independent eye: PI seeds inside a delay window that widens at the Vref peak. */
void model_seed_indep_eye(uint32_t fp);
bool k1_mock_eye_fail(uint32_t fp);

#endif /* K1_TEST_MODEL_H */
