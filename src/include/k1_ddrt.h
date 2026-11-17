/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Thalles Campagnani */
/*
 * SpacemiT K1 LPDDR4/4X training: one pass at one frequency point.
 *
 * The pass runs from SRAM, inside self-refresh, and drives the PI engine
 * through write leveling, read gate, Rx Vref sweep and Tx Vref (MR14) sweep.
 * PLL, controller/PHY init, mode registers, timings and the address map are
 * the caller's job and must already be in place for boot_pp.
 */
#ifndef K1_DDRT_H
#define K1_DDRT_H

#include <stdarg.h>
#include <stdint.h>

enum k1_ddrt_err : int32_t {
	K1_DDRT_OK        = 0,
	K1_DDRT_EIO       = -5,
	K1_DDRT_EINVAL    = -22,
	K1_DDRT_ENOSYS    = -38,
	K1_DDRT_ETIMEDOUT = -110,
};

enum k1_ddrt_stage : uint8_t {
	K1_STAGE_NONE,
	K1_STAGE_ARGS,
	K1_STAGE_SR_ENTER,
	K1_STAGE_WL,
	K1_STAGE_GATE,
	K1_STAGE_RX,
	K1_STAGE_TX,
	K1_STAGE_SR_EXIT,
	K1_STAGE_STATUS,
	K1_STAGE_MR,
};

struct k1_ddrt_env {
	/* monotonic microseconds; nullptr means only poll_limit bounds waits */
	uint64_t (*now_us)(void *ctx);
	/* printf-like sink; nullptr discards */
	void (*vlog)(void *ctx, const char *fmt, va_list ap);
	void *ctx;
	/* hard cap on status polls per wait; 0 selects the default */
	uint32_t poll_limit;
	/* per-step Vref minima in the log */
	bool verbose;
};

struct k1_ddrt_pi_stats {
	uint16_t ops;
	uint16_t stale_first;	/* first status read after PI_CMD had done set */
	uint32_t max_polls;	/* status polls until done */
	uint32_t max_us;	/* PI_CMD to done (0 without now_us) */
};

struct k1_ddrt_cfg {
	uintptr_t ddr;		/* controller base, 0xc0000000 on K1 */
	uint32_t cs_num;	/* 1 or 2 */
	uint32_t boot_pp;	/* 0..3, already active */
};

struct k1_ddrt_result {
	int32_t rc;
	enum k1_ddrt_stage fail_stage;
	uint8_t fail_cs;		/* 1-based, 0 if not CS specific */
	uint8_t rx_vref_idx;		/* winning PHY Vref index 0..15 */
	uint8_t tx_vref_dq;		/* winning MR14 value 21..36 */
	uint32_t phy_status;		/* phy + 0x18000 before SR exit */
	uint32_t pi_status;		/* last PI_STATUS read */
	uint8_t rx_min[16];		/* worst DQ margin per Rx step */
	uint8_t tx_min[16];		/* worst DQ margin per Tx step */
	uint8_t rx_min_cs[2][16];	/* same, per rank */
	uint8_t tx_min_cs[2][16];
	uint32_t sr_stat[3];		/* before enter, after enter, after exit */
	struct k1_ddrt_pi_stats pi[4];	/* WL, gate, Rx, Tx */
	uint32_t vref0, vref1, txvref;	/* final register values */
	uint32_t wl[2][4];		/* [cs][byte] */
	uint32_t gate[2][4];
	uint8_t rx_dq[2][4][8];		/* [cs][byte][lane] */
	uint8_t tx_dq[2][4][8];
	uint8_t tx_dm[2][4];
	uint32_t stage_us[4];		/* WL, gate, Rx, Tx (0 without now_us) */
};

/*
 * Train one frequency point. Returns K1_DDRT_OK only if the PHY training
 * status is 0 and every gate run passed. Any other value must stop boot.
 */
[[nodiscard]] int k1_ddrt_train_pass(const struct k1_ddrt_cfg *cfg,
				     const struct k1_ddrt_env *env,
				     struct k1_ddrt_result *res);

/* Mode register read; must not be called while in self-refresh. */
[[nodiscard]] int k1_ddrt_mr_read(const struct k1_ddrt_cfg *cfg,
				  const struct k1_ddrt_env *env,
				  uint32_t cs, uint32_t ch, uint32_t mr,
				  uint8_t *val);

/* After the pass: MR14 on every (cs, ch) must equal CTL_TXVREF[21:16]. */
[[nodiscard]] int k1_ddrt_check_mr14(const struct k1_ddrt_cfg *cfg,
				     const struct k1_ddrt_env *env);

/* Fill only the register part of res (taps, gate, WL, Vref); no PI access. */
[[nodiscard]] int k1_ddrt_snapshot(const struct k1_ddrt_cfg *cfg,
				   struct k1_ddrt_result *res);

/* max-min rule shared by Rx and Tx: first index holding the largest value */
uint32_t k1_ddrt_max_min(const uint8_t min_per_step[16]);

const char *k1_ddrt_stage_name(enum k1_ddrt_stage stage);

#endif /* K1_DDRT_H */
