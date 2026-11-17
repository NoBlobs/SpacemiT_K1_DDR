// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Thalles Campagnani
/*
 * SpacemiT K1 LPDDR4X boot sequence (doc 31): which passes run, at which
 * frequency point, and when the controller is re-initialised for x8 dies.
 */
#include "k1_ddr_boot.h"
#include "k1_ddr_init.h"
#include "k1_ddrt_io.h"
#include "k1_ddrt_priv.h"
#include "k1_ddrt_regs.h"

struct k1_boot {
	const struct k1_ddr_boot_cfg *cfg;
	const struct k1_ddrt_env *env;
	struct k1_ddr_boot_report *rep;
	struct k1_ddr_hw hw;
	uint64_t t0;
	bool mc_clocked;	/* MC reads are safe only once SW-FC and MCK are done */
};

const char *k1_ddr_step_name(enum k1_ddr_step step)
{
	switch (step) {
	case K1_STEP_NONE:	return "none";
	case K1_STEP_ARGS:	return "args";
	case K1_STEP_PLL:	return "pll-lock";
	case K1_STEP_SWFC:	return "sw-fc";
	case K1_STEP_MC:	return "mc-program";
	case K1_STEP_DFI_INIT:	return "dfi-init";
	case K1_STEP_DRAM_INIT:	return "dram-init";
	case K1_STEP_TRAIN:	return "training";
	case K1_STEP_MR_READ:	return "mr-read";
	case K1_STEP_MAP:	return "address-map";
	case K1_STEP_DFC:	return "dfc";
	}
	return "?";
}

uint32_t k1_ddr_final_fp(enum k1_ddr_type type, uint32_t data_rate)
{
	switch (data_rate) {
	case 1066:
		return 1;
	case 2133:
		return type == K1_DDR_LPDDR4 ? 3 : 0;
	case 2400:
	case 2666:
		return 2;
	default:
		return 0;
	}
}

uint32_t k1_ddr_dfc_plan(enum k1_ddr_type type, uint8_t fps[3])
{
	uint32_t n = 0;

	fps[n++] = 1;
	if (type == K1_DDR_LPDDR4)
		fps[n++] = 3;
	fps[n++] = 2;
	return n;
}

static int k1_boot_end(struct k1_boot *b, int rc, enum k1_ddr_step step)
{
	struct k1_ddr_boot_report *rep = b->rep;

	rep->rc = rc;
	rep->fail_step = rc ? step : K1_STEP_NONE;
	if (b->hw.pmu)
		rep->clksel = k1_r32(b->hw.pmu + K1_PMU_CLKSEL);
	if (b->mc_clocked) {
		rep->status = k1_r32(b->hw.ddr + K1_MC_STATUS);
		rep->ctl_freq = k1_r32(b->hw.ddr + K1_MC_FREQ);
	}
	rep->total_us = (uint32_t)(k1_now(b->env) - b->t0);
	if (rc)
		k1_log(b->env, "k1-ddr: %s failed, rc %d\n",
		       k1_ddr_step_name(step), rc);
	return rc;
}

static int k1_boot_pass(struct k1_boot *b, uint32_t pp)
{
	const struct k1_ddrt_cfg tc = {
		.ddr = b->hw.ddr,
		.cs_num = b->hw.cs_num,
		.boot_pp = pp,
	};
	struct k1_ddr_boot_report *rep = b->rep;
	struct k1_ddrt_result res;
	const int rc = k1_ddrt_train_pass(&tc, b->env, &res);

	if (rep->n_pass < sizeof(rep->pass) / sizeof(rep->pass[0]))
		rep->pass[rep->n_pass++] = (struct k1_ddr_pass_summary){
			.rc = rc,
			.boot_pp = (uint8_t)pp,
			.rx_vref_idx = res.rx_vref_idx,
			.tx_vref_dq = res.tx_vref_dq,
			.fail_stage = res.fail_stage,
			.phy_status = res.phy_status,
			.gate_cs0_b0 = res.gate[0][0],
		};
	if (b->cfg->on_pass)
		b->cfg->on_pass(b->cfg->pass_ctx, &res, pp);
	return rc;
}

static int k1_boot_dfc(struct k1_boot *b, uint32_t fp)
{
	const struct k1_ddrt_cfg mc = {
		.ddr = b->hw.ddr,
		.cs_num = b->hw.cs_num,
	};
	const int rc = k1_ddr_dfc(&b->hw, fp);
	uint8_t discard = 0;

	if (rc)
		return rc;
	/* doc 08 (P-19): the first MRR after a HW-DFC may read 0, whatever the MR */
	(void)k1_ddrt_mr_read(&mc, b->env, 0, 0, 14, &discard);
	/* the engine re-sends MR14 from the new bank: a cheap check that it ran */
	if (k1_ddrt_check_mr14(&mc, b->env))
		k1_log(b->env, "k1-ddr: warning: MR14 differs from the fp%u bank\n",
		       fp);
	return rc;
}

int k1_ddr_boot(const struct k1_ddr_boot_cfg *cfg,
		const struct k1_ddrt_env *env,
		struct k1_ddr_boot_report *rep)
{
	struct k1_boot b = { .cfg = cfg, .env = env, .rep = rep };
	enum k1_ddr_step step = K1_STEP_NONE;
	struct k1_ddr_geom g;
	uint8_t fps[3];
	uint32_t n;
	int rc;

	if (!rep)
		return K1_DDRT_EINVAL;
	*rep = (struct k1_ddr_boot_report){};
	if (!cfg || !cfg->ddr || !cfg->pmu || !cfg->dfc_win || cfg->cs_num < 1 ||
	    cfg->cs_num > K1_MAX_CS || !env || !env->now_us)
		return k1_boot_end(&b, K1_DDRT_EINVAL, K1_STEP_ARGS);
	b.t0 = k1_now(env);
	if (cfg->type == K1_DDR_LPDDR4) {
		/* P-22: LPDDR4 is another ddr_fw.bin; MR14 range still open */
		k1_log(env, "k1-ddr: LPDDR4 MR14 range not established\n");
		return k1_boot_end(&b, K1_DDRT_ENOSYS, K1_STEP_ARGS);
	}

	b.hw = k1_ddr_hw_init(cfg, env);
	rep->final_pp = (uint8_t)k1_ddr_final_fp(cfg->type, cfg->data_rate);
	k1_log(env, "k1-ddr: LPDDR4X, cs_num %u, %u MT/s, final fp%u\n",
	       cfg->cs_num, cfg->data_rate, rep->final_pp);

	rc = k1_ddr_pll_init(&b.hw, cfg->data_rate, &step);
	if (rc)
		return k1_boot_end(&b, rc, step);
	b.mc_clocked = true;
	rc = k1_ddr_device_init(&b.hw, false, &step);
	if (rc)
		return k1_boot_end(&b, rc, step);

	/* cold PHY: enough to read MR8 before byte mode and map are decided */
	rc = k1_boot_pass(&b, 0);
	if (rc)
		return k1_boot_end(&b, rc, K1_STEP_TRAIN);

	const struct k1_ddrt_cfg mrcfg = { .ddr = cfg->ddr, .cs_num = cfg->cs_num };
	rc = k1_ddrt_mr_read(&mrcfg, env, 0, 0, 5, &rep->mr5);
	if (!rc)
		rc = k1_ddrt_mr_read(&mrcfg, env, 0, 0, 8, &rep->mr8);
	if (rc)
		return k1_boot_end(&b, rc, K1_STEP_MR_READ);
	k1_log(env, "k1-ddr: MR5 0x%02x MR8 0x%02x\n", rep->mr5, rep->mr8);

	rc = k1_ddr_geometry(rep->mr8, &g);
	if (rc) {
		k1_log(env, "k1-ddr: MR8 0x%02x: no address map for this part\n",
		       rep->mr8);
		return k1_boot_end(&b, rc, K1_STEP_MAP);
	}
	if (g.x8) {
		rep->byte_mode = true;
		k1_ddr_mc_reset(&b.hw);
		rc = k1_ddr_device_init(&b.hw, true, &step);
		if (rc)
			return k1_boot_end(&b, rc, step);
	}
	k1_ddr_map_apply(&b.hw, &g);
	rep->size_mb = g.area_mb * cfg->cs_num;
	k1_log(env, "k1-ddr: %u MB (%s, ROW_%u), cs0 0x%08x cs1 0x%08x 0x%x geom 0x%08x\n",
	       rep->size_mb, g.x8 ? "x8" : "x16", g.row,
	       k1_r32(cfg->ddr + K1_MC_CS0_MAP), k1_r32(cfg->ddr + K1_MC_CS1_MAP),
	       k1_r32(cfg->ddr + K1_MC_CS1_HI), k1_r32(cfg->ddr + K1_MC_CS0_GEOM));
#ifndef K1_EXP_DFC_TIMEOUT
	k1_ddr_dfc_table_load(&b.hw, rep->size_mb == 16384);
#else
	k1_log(env, "k1-ddr: EXP DFC_TIMEOUT: skip the DFC table\n");
#endif

	rc = k1_boot_pass(&b, 0);
	n = k1_ddr_dfc_plan(cfg->type, fps);
	for (uint32_t i = 0; !rc && i < n; i++) {
		rc = k1_boot_dfc(&b, fps[i]);
		if (rc)
			return k1_boot_end(&b, rc, K1_STEP_DFC);
		rc = k1_boot_pass(&b, fps[i]);
	}
	if (rc)
		return k1_boot_end(&b, rc, K1_STEP_TRAIN);

	if (rep->size_mb == 16384)
		k1_ddr_mr_write(&b.hw, K1_MR21_16G);
	rc = k1_boot_dfc(&b, rep->final_pp);
	if (rc)
		return k1_boot_end(&b, rc, K1_STEP_DFC);
	return k1_boot_end(&b, K1_DDRT_OK, K1_STEP_NONE);
}
