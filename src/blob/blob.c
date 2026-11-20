// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Thalles Campagnani
/*
 * Open DDR firmware for mainline U-Boot SPL on SpacemiT K1.
 *
 * The SPL copies this image to 0xc082d000 and calls offset 0 as
 *   int ddr_init(u64 ddr_base, u32 cs_num, u32 data_rate,
 *                void (*puts)(const char *s));
 * in M-mode, on its own SRAM stack, with puts == NULL when quiet.
 */
#include "fmt.h"
#include "k1_ddr_accept.h"
#include "k1_ddr_boot.h"
#include "k1_ddrt.h"
#include "../lib/k1_ddr_eye.h"
#include "../lib/k1_ddrt_io.h"
#include "../lib/k1_ddrt_regs.h"

typedef void (*k1_puts_fn)(const char *s);

int k1_blob_ddr_init(uint64_t ddr_base, uint32_t cs_num, uint32_t data_rate,
		     k1_puts_fn puts);
int k1_blob_retrain(uint64_t ddr_base, uint32_t cs_num, k1_puts_fn puts,
		    uint32_t flags);

/* k1_blob_retrain() flags */
static constexpr uint32_t K1_RETRAIN_VERBOSE = 1u << 0;

static constexpr uintptr_t K1_PMU_BASE = 0xd428'2800;
static constexpr uintptr_t K1_DFC_WIN  = 0xf000'0000;
static constexpr uintptr_t K1_CLINT_MTIME = 0xe400'0000 + 0xbff8;
static constexpr uint64_t K1_MTIME_TICKS_PER_US = 24;

struct blob_ctx {
	k1_puts_fn puts;
};

static uint64_t blob_now_us(void *ctx)
{
	(void)ctx;
	return *(volatile const uint64_t *)K1_CLINT_MTIME / K1_MTIME_TICKS_PER_US;
}

static void blob_vlog(void *ctx, const char *fmt, va_list ap)
{
	const struct blob_ctx *c = ctx;
	char buf[200];

	if (!c || !c->puts)
		return;
	k1_vsnprintf(buf, sizeof(buf), fmt, ap);
	c->puts(buf);
}

[[gnu::format(printf, 2, 3)]]
static void out(const struct k1_ddrt_env *env, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	blob_vlog(env->ctx, fmt, ap);
	va_end(ap);
}

struct boot_obs {
	const struct k1_ddrt_env *env;
	uintptr_t ddr;
	uint32_t cs_num;
	uint32_t data_rate;
	uint32_t n;
	uint32_t bad;
	int eye_rc;
};

/* gate after the x8 re-init and the doc 21 ranges at the final pp */
static void boot_on_pass(void *ctx, const struct k1_ddrt_result *r, uint32_t pp)
{
	struct boot_obs *o = ctx;
	const struct k1_ddrt_env *env = o->env;

	out(env, "k1-ddr: pass %u pp%u rc %d stage %s cs %u: rx idx %u, mr14 %u, phy_status 0x%x, %u us\n",
	    o->n, pp, r->rc, k1_ddrt_stage_name(r->fail_stage), r->fail_cs,
	    r->rx_vref_idx, r->tx_vref_dq, r->phy_status,
	    r->stage_us[0] + r->stage_us[1] + r->stage_us[2] + r->stage_us[3]);
	k1_ddr_print_pass_stats(env, r, o->cs_num);
	k1_ddr_print_result(env, "state", r, o->cs_num);
#ifdef K1_INDEP_EYE
	/* Doc-21 PI harvest ranges are Samsung-centric; Micron WL/Tx/gate
	 * taps sit outside them with a passing pattern. Production quality
	 * is the independent memory-pattern verify (and PHY_STATUS on the PI
	 * pass, which already fails r->rc). */
	if (!r->rc && !o->eye_rc && o->n >= 1 &&
	    pp != k1_ddr_final_fp(K1_DDR_LPDDR4X, o->data_rate)) {
		o->eye_rc = k1_ddr_eye_measure(o->ddr, o->cs_num, pp, env);
		if (o->eye_rc)
			o->bad++;
	}
#else
	if (o->n == 1)
		o->bad += k1_ddr_accept_pp0b_gate(env, r, o->cs_num);
	if (!r->rc && pp == 2) {
		o->bad += k1_ddr_accept_final(env, r, o->cs_num, K1_DDRT_OK);
		if (o->data_rate == 2400)
			o->bad += k1_ddr_accept_pp2_2400(env, r);
	}
#endif
	o->n++;
}

int k1_blob_ddr_init(uint64_t ddr_base, uint32_t cs_num, uint32_t data_rate,
		     k1_puts_fn puts)
{
	struct blob_ctx c = { .puts = puts };
	const struct k1_ddrt_env env = {
		.now_us = blob_now_us,
		.vlog = blob_vlog,
		.ctx = &c,
	};
	struct boot_obs obs = {
		.env = &env,
		.ddr = (uintptr_t)ddr_base,
		.cs_num = cs_num,
		.data_rate = data_rate,
	};
	/* P-22: no silicon selector; drop-in is LPDDR4X / Tx ODT 80 ohm */
	const struct k1_ddr_boot_cfg cfg = {
		.ddr = (uintptr_t)ddr_base,
		.pmu = K1_PMU_BASE,
		.dfc_win = K1_DFC_WIN,
		.cs_num = cs_num,
		.data_rate = data_rate,
		.type = K1_DDR_LPDDR4X,
		.on_pass = boot_on_pass,
		.pass_ctx = &obs,
	};
	struct k1_ddr_boot_report rep;
	int rc;

	out(&env, "k1-ddr: open firmware, ddr 0x%lx cs_num %u, %u MT/s\n",
	    (unsigned long)ddr_base, cs_num, data_rate);
	rc = k1_ddr_boot(&cfg, &env, &rep);
	if (!rc) {
		const struct k1_ddrt_cfg mc = { .ddr = cfg.ddr, .cs_num = cs_num };

		k1_ddr_print_mrs(&env, &mc);
#ifdef K1_INDEP_EYE
		/* final DFC re-sends MR14 from the bank; re-centre at the live fp */
		if (!obs.eye_rc) {
			obs.eye_rc = k1_ddr_eye_measure(cfg.ddr, cs_num,
							rep.final_pp, &env);
			if (obs.eye_rc)
				obs.bad++;
		}
		if (!rc && obs.eye_rc)
			rc = obs.eye_rc;
#endif
	}
	out(&env, "k1-ddr: rc %d (%s) in %u us: %u passes, MR5 0x%02x MR8 0x%02x%s, %u MB, fp%u ctl_freq 0x%08x status 0x%02x clksel 0x%08x, %u item(s) out of range\n",
	    rc, k1_ddr_step_name(rep.fail_step), rep.total_us, rep.n_pass,
	    rep.mr5, rep.mr8, rep.byte_mode ? " x8" : "", rep.size_mb,
	    rep.final_pp, rep.ctl_freq, rep.status, rep.clksel, obs.bad);
	return rc;
}

int k1_blob_retrain(uint64_t ddr_base, uint32_t cs_num, k1_puts_fn puts,
		    uint32_t flags)
{
	struct blob_ctx c = { .puts = puts };
	const struct k1_ddrt_env env = {
		.now_us = blob_now_us,
		.vlog = blob_vlog,
		.ctx = &c,
		.verbose = flags & K1_RETRAIN_VERBOSE,
	};
	const uintptr_t ddr = (uintptr_t)ddr_base;
	const uint32_t freq = k1_r32(ddr + K1_MC_FREQ);
	const uint32_t pi_cfg = k1_r32(ddr + K1_MC_PI_CFG);
	const uint32_t pp = freq >> K1_FREQ_PP_SHIFT;
	const uint32_t sel = (freq >> K1_FREQ_SEL_SHIFT) & 3;
	const struct k1_ddrt_cfg cfg = { .ddr = ddr, .cs_num = cs_num, .boot_pp = pp };
	struct k1_ddrt_result before, res;
	uint64_t t0, dt;
	int rc, mr14_rc;

	out(&env, "k1-ddrt retrain: ddr 0x%lx cs_num %u flags 0x%x\n",
	    (unsigned long)ddr, cs_num, flags);
	out(&env, "  CTL_FREQ 0x%08x (pp %u sel %u) PI_CFG 0x%08x SR_STAT 0x%08x CS0 0x%08x CS1 0x%08x\n",
	    freq, pp, sel, pi_cfg, k1_r32(ddr + K1_MC_STATUS),
	    k1_r32(ddr + K1_MC_CS0_MAP), k1_r32(ddr + K1_MC_CS1_MAP));

	if (((pi_cfg >> K1_PI_CFG_SEL_SHIFT) & 3) != pp) {
		out(&env, "  precondition: PI_CFG[3:2] %u != CTL_FREQ[31:30] %u\n",
		    (pi_cfg >> K1_PI_CFG_SEL_SHIFT) & 3, pp);
		return K1_DDRT_EINVAL;
	}
	if (cs_num == 2 && !(k1_r32(ddr + K1_MC_CS1_MAP) & 1))
		out(&env, "  warning: cs_num 2 but CS1 map not enabled\n");

	/* doc 07 starts from 0x33: with these bits already up the exit wait cannot end */
	const uint32_t sr_stat = k1_r32(ddr + K1_MC_STATUS);
	const uint32_t sr_mask = cs_num == 2 ? K1_SR_STAT_2CS : K1_SR_STAT_1CS;

	if (sr_stat & sr_mask)
		out(&env, "  precondition: SR_STAT 0x%02x already has 0x%02x set; the self-refresh handshake is not observable\n",
		    sr_stat, sr_mask);

	k1_ddr_print_mrs(&env, &cfg);
	if (k1_ddrt_snapshot(&cfg, &before) == K1_DDRT_OK)
		k1_ddr_print_result(&env, "before", &before, cs_num);

	t0 = blob_now_us(nullptr);
	rc = k1_ddrt_train_pass(&cfg, &env, &res);
	dt = blob_now_us(nullptr) - t0;

	out(&env, "  pass rc %d stage %s cs %u in %u us (wl %u gate %u rx %u tx %u)\n",
	    rc, k1_ddrt_stage_name(res.fail_stage), res.fail_cs, (uint32_t)dt,
	    res.stage_us[0], res.stage_us[1], res.stage_us[2], res.stage_us[3]);
	out(&env, "  rx min:");
	for (uint32_t s = 0; s < 16; s++)
		out(&env, " %u", res.rx_min[s]);
	out(&env, "  -> idx %u\n  tx min:", res.rx_vref_idx);
	for (uint32_t s = 0; s < 16; s++)
		out(&env, " %u", res.tx_min[s]);
	out(&env, "  -> mr14 %u (0x%02x)\n", res.tx_vref_dq, res.tx_vref_dq);
	k1_ddr_print_pass_stats(&env, &res, cs_num);
	k1_ddr_print_result(&env, "after", &res, cs_num);

	mr14_rc = k1_ddrt_check_mr14(&cfg, &env);
	out(&env, "  MR14 check rc %d\n", mr14_rc);
	k1_ddr_print_mrs(&env, &cfg);

#ifndef K1_INDEP_EYE
	if (pp == 2) {
		const uint32_t bad = k1_ddr_accept_final(&env, &res, cs_num, mr14_rc);

		out(&env, "  acceptance (doc 21, final pp): %u item(s) out of range\n",
		    bad);
	}
#endif
	if (!rc && mr14_rc)
		rc = mr14_rc;
	out(&env, "k1-ddrt retrain: rc %d\n", rc);
	return rc;
}
