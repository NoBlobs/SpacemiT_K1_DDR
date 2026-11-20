// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Thalles Campagnani
/*
 * SpacemiT K1 LPDDR4/4X training pass.
 *
 *   enter SR -> PHY_INIT -> WL -> read gate -> Rx Vref sweep -> Tx Vref
 *   sweep -> PHY_STATUS -> exit SR
 *
 * The PI engine finds the edges; this code only arms it, sweeps Vref,
 * applies the max-min rule and re-runs the PI at the winning Vref.
 */
#include "k1_ddrt.h"
#include "k1_ddrt_io.h"
#include "k1_ddrt_priv.h"
#include "k1_ddrt_regs.h"

/* the PI has no hardware timeout; observed stages take 10..122 ms */
static constexpr uint32_t K1_PI_BUDGET_US = 1'000'000;
static constexpr uint32_t K1_SR_BUDGET_US = 10'000;
static constexpr uint32_t K1_MR_BUDGET_US = 10'000;
static constexpr uint32_t K1_DEFAULT_POLL_LIMIT = 100'000'000;

struct k1_pass {
	const struct k1_ddrt_env *env;
	struct k1_ddrt_result *res;
	struct k1_ddrt_pi_stats *pi;
	uintptr_t ddr;
	uintptr_t phy;
	uintptr_t slice;
	uint32_t cs_num;
	uint32_t poll_limit;
};

struct k1_harvest {
	uint8_t dq[K1_N_DQ];
	uint8_t dm[K1_N_BYTES];
};

void k1_log(const struct k1_ddrt_env *env, const char *fmt, ...)
{
	va_list ap;

	if (!env || !env->vlog)
		return;
	va_start(ap, fmt);
	env->vlog(env->ctx, fmt, ap);
	va_end(ap);
}

uint64_t k1_now(const struct k1_ddrt_env *env)
{
	return (env && env->now_us) ? env->now_us(env->ctx) : 0;
}

void k1_udelay(const struct k1_ddrt_env *env, uint32_t us)
{
	const uint64_t t0 = k1_now(env);

	if (!env || !env->now_us)
		return;
	while (k1_now(env) - t0 < us)
		;
}

static uint32_t k1_poll_limit(const struct k1_ddrt_env *env)
{
	return (env && env->poll_limit) ? env->poll_limit : K1_DEFAULT_POLL_LIMIT;
}

int k1_wait_n(const struct k1_ddrt_env *env, uintptr_t addr, uint32_t mask,
	      uint32_t want, uint32_t budget_us, uint32_t *last,
	      uint32_t *polls)
{
	const bool timed = env && env->now_us;
	const uint32_t limit = k1_poll_limit(env);
	const uint64_t t0 = k1_now(env);
	uint32_t v = 0;
	uint32_t n = 0;
	int rc = K1_DDRT_ETIMEDOUT;

	while (n < limit) {
		v = k1_r32(addr);
		n++;
		if ((v & mask) == want) {
			rc = K1_DDRT_OK;
			break;
		}
		if (timed && k1_now(env) - t0 > budget_us)
			break;
	}
	if (last)
		*last = v;
	if (polls)
		*polls = n;
	return rc;
}

int k1_wait(const struct k1_ddrt_env *env, uintptr_t addr, uint32_t mask,
	    uint32_t want, uint32_t budget_us, uint32_t *last)
{
	return k1_wait_n(env, addr, mask, want, budget_us, last, nullptr);
}

const char *k1_ddrt_stage_name(enum k1_ddrt_stage stage)
{
	switch (stage) {
	case K1_STAGE_NONE:	return "none";
	case K1_STAGE_ARGS:	return "args";
	case K1_STAGE_SR_ENTER:	return "sr-enter";
	case K1_STAGE_WL:	return "write-leveling";
	case K1_STAGE_GATE:	return "read-gate";
	case K1_STAGE_RX:	return "read-training";
	case K1_STAGE_TX:	return "write-training";
	case K1_STAGE_SR_EXIT:	return "sr-exit";
	case K1_STAGE_STATUS:	return "phy-status";
	case K1_STAGE_MR:	return "mode-register";
	}
	return "?";
}

static int k1_fail(struct k1_pass *p, int rc, enum k1_ddrt_stage stage,
		   uint32_t cs)
{
	p->res->rc = rc;
	p->res->fail_stage = stage;
	p->res->fail_cs = (uint8_t)cs;
	k1_log(p->env, "k1-ddrt: %s failed: cs %u rc %d pi_status 0x%08x\n",
	       k1_ddrt_stage_name(stage), cs, rc, p->res->pi_status);
	return rc;
}

static uintptr_t k1_byte_base(const struct k1_pass *p, uint32_t cs0,
			      uint32_t byte)
{
	return p->slice + K1_BYTE_OFF[byte] + (cs0 << K1_PHY_CS_SHIFT);
}

static int k1_pi_op(struct k1_pass *p, uint32_t op, uint32_t cs,
		    uint32_t done, uint32_t *status, uint32_t *first)
{
	const uintptr_t stat = p->ddr + K1_MC_PI_STATUS;
	struct k1_ddrt_pi_stats *s = p->pi;
	const uint64_t t0 = k1_now(p->env);
	uint32_t st = 0;
	uint32_t polls = 0;
	int rc;

	k1_w32(p->ddr + K1_MC_PI_CMD, op | (cs << K1_PI_CS_SHIFT));
	const uint32_t snap = k1_r32(stat);
	/* the write drops the op's done bits; seeing them set here means it did not */
	const bool stale = (snap & done) == done;

	rc = k1_wait_n(p->env, stat, done, done, K1_PI_BUDGET_US, &st, &polls);
	p->res->pi_status = st;
	if (status)
		*status = st;
	if (first)
		*first = snap;

	if (s) {
		const uint32_t us = (uint32_t)(k1_now(p->env) - t0);

		if (s->ops < UINT16_MAX)
			s->ops++;
		if (stale && s->stale_first < UINT16_MAX)
			s->stale_first++;
		if (polls > s->max_polls)
			s->max_polls = polls;
		if (us > s->max_us)
			s->max_us = us;
	}
	return rc;
}

static uint32_t k1_sr_mode(uint32_t cs_num)
{
	return cs_num == 2 ? 3 : 1;
}

static uint32_t k1_sr_mask(uint32_t cs_num)
{
	return cs_num == 2 ? K1_SR_STAT_2CS : K1_SR_STAT_1CS;
}

static int k1_sr_enter(struct k1_pass *p)
{
	const uint32_t mask = k1_sr_mask(p->cs_num);

	k1_w32(p->ddr + K1_MC_USER0, K1_SR_CMD | K1_SR_ENTER |
	       (k1_sr_mode(p->cs_num) << K1_SR_MODE_SHIFT));
	return k1_wait(p->env, p->ddr + K1_MC_STATUS, mask, mask,
		       K1_SR_BUDGET_US, nullptr);
}

/* exit waits only for the SR bits to drop: STATUS goes 0x77 -> 0x11, not 0x33 */
static int k1_sr_exit(struct k1_pass *p)
{
	const uint32_t mask = k1_sr_mask(p->cs_num);

	k1_w32(p->ddr + K1_MC_USER0, K1_SR_CMD | K1_SR_EXIT |
	       (k1_sr_mode(p->cs_num) << K1_SR_MODE_SHIFT));
	return k1_wait(p->env, p->ddr + K1_MC_STATUS, mask, 0,
		       K1_SR_BUDGET_US, nullptr);
}

/* the harvest latches only the last PI op: copy it before the next CS */
static uint8_t k1_harvest_take(uintptr_t base, struct k1_harvest *h)
{
	uint32_t w[K1_N_DQ / 4];
	uint32_t dm;
	uint8_t worst = UINT8_MAX;

	for (uint32_t g = 0; g < K1_N_DQ / 4; g++)
		w[g] = k1_r32(base + 4 * g);
	dm = k1_r32(base + K1_PHY_HARVEST_DM);

	for (uint32_t g = 0; g < K1_N_DQ / 4; g++) {
		for (uint32_t k = 0; k < 4; k++) {
			const uint8_t m = (uint8_t)(w[g] >> (8 * k));

			h->dq[4 * g + k] = m;
			if (m < worst)
				worst = m;
		}
	}
	/* DM margins are kept but never weigh in the minimum */
	for (uint32_t k = 0; k < K1_N_BYTES; k++)
		h->dm[k] = (uint8_t)(dm >> (8 * k));
	return worst;
}

static void k1_read_lanes(const struct k1_pass *p, uint32_t cs0,
			  uint32_t off, uint32_t mask,
			  uint8_t out[K1_N_BYTES][K1_N_LANES])
{
	for (uint32_t b = 0; b < K1_N_BYTES; b++)
		for (uint32_t l = 0; l < K1_N_LANES; l++)
			out[b][l] = (uint8_t)(k1_r32(k1_byte_base(p, cs0, b) +
						     off + 4 * l) & mask);
}

uint32_t k1_ddrt_max_min(const uint8_t min_per_step[16])
{
	uint32_t best = 0;

	for (uint32_t s = 1; s < K1_N_VREF; s++)
		if (min_per_step[s] > min_per_step[best])
			best = s;
	return best;
}

static int k1_stage_wl(struct k1_pass *p)
{
	k1_w32(p->ddr + K1_MC_PI_WL_CFG, K1_PI_WL_CFG_VAL);
	for (uint32_t cs = 1; cs <= p->cs_num; cs++) {
		const int rc = k1_pi_op(p, K1_PI_OP_WL, cs, K1_PI_DONE_WL,
					nullptr, nullptr);

		if (rc)
			return k1_fail(p, rc, K1_STAGE_WL, cs);
	}
	return K1_DDRT_OK;
}

#ifndef K1_EXP_GATE_TIMEOUT
/* trained code is (1 << 16) | (g << 8) | g; reset value 0x0a00 is not */
static bool k1_gate_code_ok(uint32_t v)
{
	return (v & (1u << 16)) && (v & 0xff) == ((v >> 8) & 0xff);
}
#endif

/*
 * RGATE clears only bit 1. With bit 2 still 0 right after the command the
 * classic (st & 6) == 6 tells PASS from TIMEOUT; with a leftover bit 2 from
 * an earlier RD it cannot, and the PHY gate-error bit decides.
 */
static int k1_stage_gate(struct k1_pass *p)
{
#ifdef K1_EXP_GATE_TIMEOUT
	/* never send RGATE: the PI wait must expire (silicon of the model path) */
	uint32_t st = 0;

	k1_log(p->env, "k1-ddrt: EXP GATE_TIMEOUT: wait for RGATE done with no PI_CMD\n");
	(void)k1_wait(p->env, p->ddr + K1_MC_PI_STATUS, K1_PI_DONE_RGATE,
		      K1_PI_DONE_RGATE, K1_PI_BUDGET_US, &st);
	k1_log(p->env, "k1-ddrt: read-gate TIMEOUT on cs 1 (pi_status 0x%08x)\n",
	       st);
	return k1_fail(p, K1_DDRT_EIO, K1_STAGE_GATE, 1);
#else
	for (uint32_t cs = 1; cs <= p->cs_num; cs++) {
		uint32_t st = 0, snap = 0;
		int rc = k1_pi_op(p, K1_PI_OP_RGATE, cs, K1_PI_DONE_RGATE, &st,
				  &snap);

		if (rc)
			return k1_fail(p, rc, K1_STAGE_GATE, cs);
		if (!(snap & K1_PI_GATE_BIT2)) {
			if ((st & K1_PI_GATE_MASK) != K1_PI_GATE_PASS) {
				k1_log(p->env,
				       "k1-ddrt: read-gate TIMEOUT on cs %u (pi_status 0x%08x)\n",
				       cs, st);
				return k1_fail(p, K1_DDRT_EIO, K1_STAGE_GATE,
					       cs);
			}
		} else {
			const uint32_t ps = k1_r32(p->phy + K1_PHY_TRAIN_STAT);

			if (ps & K1_ST_RGATE) {
				k1_log(p->env,
				       "k1-ddrt: read-gate error on cs %u (phy_status 0x%x)\n",
				       cs, ps);
				return k1_fail(p, K1_DDRT_EIO, K1_STAGE_GATE,
					       cs);
			}
		}
	}

	for (uint32_t cs0 = 0; cs0 < p->cs_num; cs0++) {
		for (uint32_t b = 0; b < K1_N_BYTES; b++) {
			const uint32_t v = k1_r32(k1_byte_base(p, cs0, b) +
						  K1_BYTE_GATE);

			if (!k1_gate_code_ok(v)) {
				k1_log(p->env,
				       "k1-ddrt: cs %u byte %u gate 0x%08x not trained\n",
				       cs0 + 1, b, v);
				return k1_fail(p, K1_DDRT_EIO, K1_STAGE_GATE,
					       cs0 + 1);
			}
		}
	}
	return K1_DDRT_OK;
#endif
}

static void k1_set_rx_vref(struct k1_pass *p, uint32_t step)
{
	const uintptr_t v0 = p->slice + K1_SLICE_VREF0;
	const uint32_t packed = (k1_r32(v0) & K1_VREF_LO_MASK) |
				k1_pack_vref(step);

	k1_w32(v0, packed);
	k1_w32(p->slice + K1_SLICE_VREF1, packed);
}

static int k1_stage_rx(struct k1_pass *p)
{
	const uintptr_t v0 = p->slice + K1_SLICE_VREF0;
	struct k1_harvest h;
	uint8_t taps[K1_N_BYTES][K1_N_LANES];
	uint32_t best;
	int rc;

	k1_rmw32(v0, ~K1_VREF_BIT4, 0);
	k1_w32(p->ddr + K1_MC_PI_RD_CFG, K1_PI_RD_CFG_VAL);

	for (uint32_t step = 0; step < K1_N_VREF; step++) {
		uint8_t worst = UINT8_MAX;

		k1_set_rx_vref(p, step);
		for (uint32_t cs = 1; cs <= p->cs_num; cs++) {
			rc = k1_pi_op(p, K1_PI_OP_RD, cs, K1_PI_DONE_RD, nullptr,
				      nullptr);
			if (rc)
				return k1_fail(p, rc, K1_STAGE_RX, cs);

			const uint8_t m = k1_harvest_take(p->phy +
							  K1_PHY_HARVEST_RX, &h);
			k1_read_lanes(p, cs - 1, K1_BYTE_RX_DQ, K1_RX_DLY_MASK,
				      taps);
			p->res->rx_min_cs[cs - 1][step] = m;
			if (m < worst)
				worst = m;
			if (p->env && p->env->verbose)
				k1_log(p->env,
				       "k1-ddrt:   rx step %2u cs %u min %3u dq0 %2u %2u %2u %2u\n",
				       step, cs, m, taps[0][0], taps[1][0],
				       taps[2][0], taps[3][0]);
		}
		p->res->rx_min[step] = worst;
	}

	/* the sweep may arm the read-leveling bit even when the winner is good */
	k1_rmw32(p->phy + K1_PHY_TRAIN_STAT, ~K1_ST_RL, 0);

	best = k1_ddrt_max_min(p->res->rx_min);
	p->res->rx_vref_idx = (uint8_t)best;
	k1_set_rx_vref(p, best);
	for (uint32_t cs = 1; cs <= p->cs_num; cs++) {
		rc = k1_pi_op(p, K1_PI_OP_RD, cs, K1_PI_DONE_RD, nullptr,
				      nullptr);
		if (rc)
			return k1_fail(p, rc, K1_STAGE_RX, cs);
	}
	k1_rmw32(v0, ~0u, K1_VREF_BIT4);
	return K1_DDRT_OK;
}

/* the die only sees a new Vref(DQ) once the MR14 command is sent */
static void k1_set_mr14(struct k1_pass *p, uint32_t vref_dq)
{
	k1_rmw32(p->ddr + K1_MC_TXVREF, K1_TXVREF_MASK,
		 (vref_dq & K1_TXVREF_FIELD) << K1_TXVREF_SHIFT);
	k1_w32(p->ddr + K1_MC_CA_CMD, k1_ca_mr14(p->cs_num));
}

static int k1_stage_tx(struct k1_pass *p)
{
	struct k1_harvest h;
	uint8_t taps[K1_N_BYTES][K1_N_LANES];
	uint32_t best;
	int rc;

	k1_w32(p->ddr + K1_MC_PI_WR_CFG, K1_PI_WR_CFG_VAL);

	for (uint32_t i = 0; i < K1_N_VREF; i++) {
		uint8_t worst = UINT8_MAX;

		k1_set_mr14(p, K1_TX_VREF_SEED0 + i);
		for (uint32_t cs = 1; cs <= p->cs_num; cs++) {
			rc = k1_pi_op(p, K1_PI_OP_WR, cs, K1_PI_DONE_WR, nullptr,
				      nullptr);
			if (rc)
				return k1_fail(p, rc, K1_STAGE_TX, cs);

			const uint8_t m = k1_harvest_take(p->phy +
							  K1_PHY_HARVEST_TX, &h);
			k1_read_lanes(p, cs - 1, K1_BYTE_TX_DQ, K1_TX_DLY_MASK,
				      taps);
			p->res->tx_min_cs[cs - 1][i] = m;
			if (m < worst)
				worst = m;
			if (p->env && p->env->verbose)
				k1_log(p->env,
				       "k1-ddrt:   tx vdq %2u cs %u min %3u dq0 %2u %2u %2u %2u\n",
				       K1_TX_VREF_SEED0 + i, cs, m, taps[0][0],
				       taps[1][0], taps[2][0], taps[3][0]);
		}
		p->res->tx_min[i] = worst;
	}

	best = k1_ddrt_max_min(p->res->tx_min);
	if (p->res->tx_min[best] == 0)
		best = K1_TX_FALLBACK_IDX;
	p->res->tx_vref_dq = (uint8_t)(K1_TX_VREF_SEED0 + best);

	k1_set_mr14(p, p->res->tx_vref_dq);
	for (uint32_t cs = 1; cs <= p->cs_num; cs++) {
		rc = k1_pi_op(p, K1_PI_OP_WR, cs, K1_PI_DONE_WR, nullptr,
				      nullptr);
		if (rc)
			return k1_fail(p, rc, K1_STAGE_TX, cs);
	}
	return K1_DDRT_OK;
}

static void k1_collect(struct k1_pass *p)
{
	struct k1_ddrt_result *r = p->res;

	r->vref0 = k1_r32(p->slice + K1_SLICE_VREF0);
	r->vref1 = k1_r32(p->slice + K1_SLICE_VREF1);
	r->txvref = k1_r32(p->ddr + K1_MC_TXVREF);
	for (uint32_t cs0 = 0; cs0 < p->cs_num; cs0++) {
		for (uint32_t b = 0; b < K1_N_BYTES; b++) {
			const uintptr_t base = k1_byte_base(p, cs0, b);

			r->wl[cs0][b] = k1_r32(base + K1_BYTE_WL);
			r->gate[cs0][b] = k1_r32(base + K1_BYTE_GATE);
			r->tx_dm[cs0][b] = (uint8_t)(k1_r32(base + K1_BYTE_TX_DM) &
						     K1_TX_DLY_MASK);
		}
		k1_read_lanes(p, cs0, K1_BYTE_RX_DQ, K1_RX_DLY_MASK,
			      r->rx_dq[cs0]);
		k1_read_lanes(p, cs0, K1_BYTE_TX_DQ, K1_TX_DLY_MASK,
			      r->tx_dq[cs0]);
	}
}

static bool k1_cfg_ok(const struct k1_ddrt_cfg *cfg)
{
	return cfg && cfg->ddr && cfg->cs_num >= 1 && cfg->cs_num <= K1_MAX_CS &&
	       cfg->boot_pp <= K1_MAX_PP;
}

static struct k1_pass k1_pass_init(const struct k1_ddrt_cfg *cfg,
				   const struct k1_ddrt_env *env,
				   struct k1_ddrt_result *res)
{
	return (struct k1_pass){
		.env = env,
		.res = res,
		.ddr = cfg->ddr,
		.phy = cfg->ddr + K1_PHY_OFF,
		.slice = cfg->ddr + K1_PHY_OFF +
			 (cfg->boot_pp << K1_PHY_SLICE_SHIFT),
		.cs_num = cfg->cs_num,
		.poll_limit = k1_poll_limit(env),
	};
}

int k1_ddrt_snapshot(const struct k1_ddrt_cfg *cfg, struct k1_ddrt_result *res)
{
	struct k1_pass p;

	if (!res)
		return K1_DDRT_EINVAL;
	*res = (struct k1_ddrt_result){};
	if (!k1_cfg_ok(cfg))
		return K1_DDRT_EINVAL;
	p = k1_pass_init(cfg, nullptr, res);
	res->phy_status = k1_r32(p.phy + K1_PHY_TRAIN_STAT);
	res->pi_status = k1_r32(p.ddr + K1_MC_PI_STATUS);
	k1_collect(&p);
	return K1_DDRT_OK;
}

typedef int (*k1_stage_fn)(struct k1_pass *p);

int k1_ddrt_train_pass(const struct k1_ddrt_cfg *cfg,
		       const struct k1_ddrt_env *env,
		       struct k1_ddrt_result *res)
{
	static const k1_stage_fn stages[] = {
		k1_stage_wl, k1_stage_gate, k1_stage_rx, k1_stage_tx,
	};
	struct k1_pass p;
	int rc, xrc;

	if (!res)
		return K1_DDRT_EINVAL;
	*res = (struct k1_ddrt_result){};
	if (!k1_cfg_ok(cfg)) {
		res->rc = K1_DDRT_EINVAL;
		res->fail_stage = K1_STAGE_ARGS;
		return K1_DDRT_EINVAL;
	}
	p = k1_pass_init(cfg, env, res);

	k1_log(env, "k1-ddrt: pass pp%u cs_num %u\n", cfg->boot_pp,
	       cfg->cs_num);

	res->sr_stat[0] = k1_r32(p.ddr + K1_MC_STATUS);
	rc = k1_sr_enter(&p);
	res->sr_stat[1] = k1_r32(p.ddr + K1_MC_STATUS);
	if (rc) {
		k1_fail(&p, rc, K1_STAGE_SR_ENTER, 0);
		(void)k1_sr_exit(&p);
		res->sr_stat[2] = k1_r32(p.ddr + K1_MC_STATUS);
		return rc;
	}

	k1_w32(p.phy + K1_PHY_INIT, K1_PHY_INIT_VAL);

	static_assert(sizeof(stages) / sizeof(stages[0]) ==
		      sizeof(res->pi) / sizeof(res->pi[0]));
	for (uint32_t i = 0; i < sizeof(stages) / sizeof(stages[0]); i++) {
		const uint64_t t0 = k1_now(env);

		p.pi = &res->pi[i];
		rc = stages[i](&p);
		res->stage_us[i] = (uint32_t)(k1_now(env) - t0);
		if (rc)
			break;
	}
	p.pi = nullptr;

	res->phy_status = k1_r32(p.phy + K1_PHY_TRAIN_STAT);
	xrc = k1_sr_exit(&p);
	res->sr_stat[2] = k1_r32(p.ddr + K1_MC_STATUS);
	k1_collect(&p);

	if (rc)
		return rc;
	if (xrc)
		return k1_fail(&p, xrc, K1_STAGE_SR_EXIT, 0);
	if (res->phy_status) {
		k1_log(env, "k1-ddrt: PHY training status 0x%x\n",
		       res->phy_status);
		return k1_fail(&p, K1_DDRT_EIO, K1_STAGE_STATUS, 0);
	}

	k1_log(env,
	       "k1-ddrt: pp%u ok: rx vref idx %u (min %u), tx mr14 %u (min %u)\n",
	       cfg->boot_pp, res->rx_vref_idx, res->rx_min[res->rx_vref_idx],
	       res->tx_vref_dq,
	       res->tx_min[res->tx_vref_dq - K1_TX_VREF_SEED0]);
	return K1_DDRT_OK;
}

int k1_ddrt_mr_read(const struct k1_ddrt_cfg *cfg,
		    const struct k1_ddrt_env *env, uint32_t cs, uint32_t ch,
		    uint32_t mr, uint8_t *val)
{
	uint32_t v = 0;
	int rc;

	if (!cfg || !cfg->ddr || !val || cs >= K1_MAX_CS || ch > 1 || mr > 0xff)
		return K1_DDRT_EINVAL;

	k1_w32(cfg->ddr + K1_MC_CA_CMD, K1_CA_MR_READ |
	       ((cs + 1) << K1_CA_MR_CS_SHIFT) | (ch << K1_CA_MR_CH_SHIFT) | mr);
	rc = k1_wait(env, cfg->ddr + K1_MC_MR_DATA, K1_MR_DATA_VALID,
		     K1_MR_DATA_VALID, K1_MR_BUDGET_US, &v);
	if (rc)
		return rc;
	*val = (uint8_t)v;
	return K1_DDRT_OK;
}

int k1_ddrt_check_mr14(const struct k1_ddrt_cfg *cfg,
		       const struct k1_ddrt_env *env)
{
	uint32_t want;
	int rc = K1_DDRT_OK;

	if (!cfg || !cfg->ddr || cfg->cs_num < 1 || cfg->cs_num > K1_MAX_CS)
		return K1_DDRT_EINVAL;

	want = (k1_r32(cfg->ddr + K1_MC_TXVREF) >> K1_TXVREF_SHIFT) &
	       K1_TXVREF_FIELD;
	for (uint32_t cs = 0; cs < cfg->cs_num; cs++) {
		for (uint32_t ch = 0; ch < 2; ch++) {
			uint8_t mr14 = 0;
			const int r = k1_ddrt_mr_read(cfg, env, cs, ch, 14, &mr14);

			if (r)
				return r;
			if (mr14 != want) {
				k1_log(env,
				       "k1-ddrt: MR14 cs%u ch%u = 0x%02x, TXVREF has 0x%02x\n",
				       cs, ch, mr14, want);
				rc = K1_DDRT_EIO;
			}
		}
	}
	return rc;
}
