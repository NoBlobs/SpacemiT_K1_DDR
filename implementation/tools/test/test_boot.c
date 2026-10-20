// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Thalles Campagnani
/* Bring-up from power-on against the model: docs 25..31. */
#include <string.h>

#include "check.h"
#include "k1_ddr_boot.h"
#include "../lib/k1_ddr_init.h"
#include "../lib/k1_ddr_tables.h"
#include "../lib/k1_ddrt_io.h"
#include "../lib/k1_ddrt_regs.h"

static const struct k1_ddrt_env g_env = {
	.now_us = model_now_us,
	.vlog = test_vlog,
};

static struct k1_ddr_boot_cfg cfg_for(uint32_t cs_num, uint32_t rate,
				      enum k1_ddr_type type)
{
	return (struct k1_ddr_boot_cfg){
		.ddr = DDR,
		.pmu = PMU,
		.dfc_win = WIN,
		.cs_num = cs_num,
		.data_rate = rate,
		.type = type,
	};
}

static uint32_t mcr(uint32_t off)
{
	return model_peek(DDR + off);
}

static uint32_t phyr(uint32_t off)
{
	return model_peek(DDR + K1_PHY_OFF + off);
}

static uint32_t pmur(uint32_t off)
{
	return model_peek(PMU + off);
}

/* trace index of the n-th (0-based) ev, -1 if absent */
static int nth(enum model_ev ev, uint32_t n)
{
	for (uint32_t i = 0; i < g_model.n_trace; i++)
		if (g_model.trace[i] == ev && n-- == 0)
			return (int)i;
	return -1;
}

static int last(enum model_ev ev)
{
	for (uint32_t i = g_model.n_trace; i-- > 0;)
		if (g_model.trace[i] == ev)
			return (int)i;
	return -1;
}

static bool tbl_is(uint32_t idx, uint32_t data, uint32_t addr)
{
	const struct model_tbl *t = &g_model.tbl[idx];

	if (t->set && t->data == data && t->addr == addr)
		return true;
	printf("  tbl[0x%x] = %s 0x%08x 0x%08x, want 0x%08x 0x%08x\n", idx,
	       t->set ? "set" : "unset", t->data, t->addr, data, addr);
	return false;
}

/* power-on -> PLL -> Device_Init, for the block tests */
static struct k1_ddr_hw bring_up(const struct k1_ddr_boot_cfg *cfg, bool x8)
{
	struct k1_ddr_hw hw = k1_ddr_hw_init(cfg, &g_env);
	enum k1_ddr_step step = K1_STEP_NONE;

	CHECK_EQ(k1_ddr_pll_init(&hw, cfg->data_rate, &step), K1_DDRT_OK);
	CHECK_EQ(k1_ddr_device_init(&hw, x8, &step), K1_DDRT_OK);
	CHECK_EQ(step, K1_STEP_NONE);
	return hw;
}

static int run_boot(uint32_t cs_num, uint32_t rate,
		    struct k1_ddr_boot_report *rep)
{
	const struct k1_ddr_boot_cfg cfg = cfg_for(cs_num, rate, K1_DDR_LPDDR4X);

	return k1_ddr_boot(&cfg, &g_env, rep);
}

void test_gate_leftover_timeout(void)
{
	const struct k1_ddrt_cfg cfg = { .ddr = DDR, .cs_num = 2, .boot_pp = 1 };
	const struct k1_ddrt_env env = env_timed();
	struct k1_ddrt_result r;

	/* after an RD or a DFC, bit 2 is still 1: (st & 6) == 6 even on TIMEOUT */
	model_reset(DDR, 1, 2);
	model_poke(DDR + K1_MC_PI_STATUS, 0x8000'8067);
	g_model.gate_timeout_cs = 1;
	CHECK_EQ(k1_ddrt_train_pass(&cfg, &env, &r), K1_DDRT_EIO);
	CHECK_CLEAN();
	CHECK_EQ(r.fail_stage, K1_STAGE_GATE);
	CHECK_EQ(r.fail_cs, 1);
	CHECK_EQ(r.pi_status & 6, 6);
	CHECK_EQ(g_model.ev_count[EV_OP_GATE], 1);
	CHECK_EQ(g_model.ev_count[EV_OP_RD], 0);
	CHECK(!g_model.in_sr);
}

void test_gate_timeout_stale_codes(void)
{
	const struct k1_ddrt_cfg cfg = { .ddr = DDR, .cs_num = 2, .boot_pp = 0 };
	const struct k1_ddrt_env env = env_timed();
	struct k1_ddrt_result r;

	/* 2nd pp0: the cold pass left trained-looking codes in the same slice */
	model_reset(DDR, 0, 2);
	for (uint32_t cs0 = 0; cs0 < 2; cs0++)
		for (uint32_t b = 0; b < K1_N_BYTES; b++)
			model_poke(DDR + K1_PHY_OFF + K1_BYTE_OFF[b] +
				   (cs0 << K1_PHY_CS_SHIFT) + K1_BYTE_GATE,
				   (1u << 16) | (36u << 8) | 36);
	g_model.gate_timeout_cs = 1;
	CHECK_EQ(k1_ddrt_train_pass(&cfg, &env, &r), K1_DDRT_EIO);
	CHECK_CLEAN();
	CHECK_EQ(r.fail_stage, K1_STAGE_GATE);
	CHECK_EQ(r.fail_cs, 1);
	CHECK_EQ(r.pi_status & 6, 2);
	CHECK_EQ(g_model.ev_count[EV_OP_RD], 0);
	CHECK(!g_model.in_sr);
}

void test_rate_plan(void)
{
	uint8_t f[3] = {};

	CHECK_EQ(k1_ddr_final_fp(K1_DDR_LPDDR4X, 1200), 0);
	CHECK_EQ(k1_ddr_final_fp(K1_DDR_LPDDR4X, 1066), 1);
	CHECK_EQ(k1_ddr_final_fp(K1_DDR_LPDDR4X, 2133), 0);
	CHECK_EQ(k1_ddr_final_fp(K1_DDR_LPDDR4, 2133), 3);
	CHECK_EQ(k1_ddr_final_fp(K1_DDR_LPDDR4X, 2400), 2);
	CHECK_EQ(k1_ddr_final_fp(K1_DDR_LPDDR4X, 2666), 2);
	CHECK_EQ(k1_ddr_final_fp(K1_DDR_LPDDR4X, 1600), 0);
	CHECK_EQ(k1_ddr_final_fp(K1_DDR_LPDDR4X, 0), 0);
	CHECK_EQ(k1_ddr_final_fp(K1_DDR_LPDDR4X, 2700), 0);
	CHECK_EQ(k1_ddr_final_fp(K1_DDR_LPDDR4, 3200), 0);

	CHECK_EQ(k1_ddr_dfc_plan(K1_DDR_LPDDR4X, f), 2);
	CHECK_EQ(f[0], 1);
	CHECK_EQ(f[1], 2);
	CHECK_EQ(k1_ddr_dfc_plan(K1_DDR_LPDDR4, f), 3);
	CHECK_EQ(f[0], 1);
	CHECK_EQ(f[1], 3);
	CHECK_EQ(f[2], 2);
}

void test_pll_init(void)
{
	const struct k1_ddr_boot_cfg c24 = cfg_for(2, 2400, K1_DDR_LPDDR4X);
	const struct k1_ddr_boot_cfg c26 = cfg_for(2, 2666, K1_DDR_LPDDR4X);
	struct k1_ddr_hw hw = k1_ddr_hw_init(&c24, &g_env);
	enum k1_ddr_step step = K1_STEP_NONE;

	model_reset_por(DDR, 2);
	CHECK_EQ(k1_ddr_pll_init(&hw, 2400, &step), K1_DDRT_OK);
	CHECK_CLEAN();
	CHECK_EQ(pmur(K1_PMU_PLL1_CTL1), 0x2a20'3b00);
	CHECK_EQ(pmur(K1_PMU_PLL1_CTL2), 0x50dd'6432);
	CHECK_EQ(pmur(K1_PMU_PLL2_CTL1), 0x2023'8000);
	CHECK_EQ(pmur(K1_PMU_PLL2_CTL2), 0x50dd'632c);
	CHECK_EQ(pmur(K1_PMU_CLKSEL), 0x3'3b50);
	CHECK_EQ(pmur(K1_PMU_HW_SLP), 0x4060'0400);
	CHECK_EQ(pmur(K1_PMU_CC_AP), 0x0001'9249);
	CHECK_EQ(pmur(K1_PMU_MCK_CTRL), 3);
	CHECK_EQ(g_model.ev_count[EV_PLL_LOCK], 1);
	CHECK_EQ(g_model.ev_count[EV_SWFC], 1);
	CHECK(nth(EV_PLL_LOCK, 0) < nth(EV_SWFC, 0));
	CHECK_EQ(g_model.mc_writes, 0);

	model_reset_por(DDR, 2);
	hw = k1_ddr_hw_init(&c26, &g_env);
	CHECK_EQ(k1_ddr_pll_init(&hw, 2666, &step), K1_DDRT_OK);
	CHECK_CLEAN();
	CHECK_EQ(pmur(K1_PMU_PLL1_CTL1), 0x203c'5555);
	CHECK_EQ(pmur(K1_PMU_PLL1_CTL2), 0x50dd'6538);

	model_reset_por(DDR, 2);
	g_model.pll_never_locks = true;
	CHECK_EQ(k1_ddr_pll_init(&hw, 2400, &step), K1_DDRT_ETIMEDOUT);
	CHECK_EQ(step, K1_STEP_PLL);
	CHECK_CLEAN();
	CHECK_EQ(g_model.ev_count[EV_SWFC], 0);
	CHECK_EQ(pmur(K1_PMU_HW_SLP), 0);
	CHECK_EQ(pmur(K1_PMU_MCK_CTRL), 0);

	model_reset_por(DDR, 2);
	g_model.swfc_stuck = true;
	step = K1_STEP_NONE;
	CHECK_EQ(k1_ddr_pll_init(&hw, 2400, &step), K1_DDRT_ETIMEDOUT);
	CHECK_EQ(step, K1_STEP_SWFC);
	CHECK_CLEAN();
	CHECK_EQ(pmur(K1_PMU_MCK_CTRL), 0);
}

void test_device_init(void)
{
	static const uint32_t mr_want[9] = {
		0x1302'0001, 0x1302'0002, 0x1302'000d, 0x1302'0003, 0x1302'0016,
		0x1302'000c, 0x1302'000e, 0x1302'000b, 0x1302'0017,
	};
	static const uint32_t zq_want[4] = {
		0x1100'2000, 0x1100'1000, 0x1200'2000, 0x1200'1000,
	};
	const struct k1_ddr_boot_cfg cfg = cfg_for(2, 2400, K1_DDR_LPDDR4X);
	const struct k1_ddrt_cfg mc = { .ddr = DDR, .cs_num = 2 };

	model_reset_por(DDR, 2);
	(void)bring_up(&cfg, false);
	CHECK_CLEAN();

	for (size_t i = 0; i < K1_N_MC_GLOBALS; i++)
		CHECK_EQ(mcr(k1_mc_globals[i].off), k1_mc_globals[i].val);
	CHECK_EQ(mcr(K1_MC_CTL0), 0x0004'0300);
	CHECK_EQ(mcr(K1_MC_IO_PU), 0x0000'ffff);
	for (uint32_t fp = 0; fp < 4; fp++) {
		for (size_t i = 0; i < K1_N_MC_TIMING; i++)
			if (!k1_mc_timing[i].io)
				CHECK_EQ(model_peek_bank(fp, k1_mc_timing[i].off),
					 k1_mc_timing[i].fp[fp]);
		CHECK_EQ(model_peek_bank(fp, K1_MC_TXVREF), 0x9d19'4314);
		CHECK_EQ(model_peek_bank(fp, K1_MC_DS0), 0x0034'0000);
		CHECK_EQ(model_peek_bank(fp, K1_MC_DS1), 0x0034'0000);
		CHECK_EQ(model_peek_bank(fp, K1_MC_WDQS), 0);
	}
	CHECK_EQ(model_peek_bank(2, 0x13e4), 0x1300'0802);
	CHECK_EQ(model_peek_bank(2, 0x3b4), 0x0800'0e00);
	CHECK_EQ(model_peek_bank(0, 0x3c0), 0x0000'0002);
	CHECK_EQ(model_peek_bank(3, 0x3fc), 0x000d'0065);
	CHECK_EQ(mcr(K1_MC_FREQ), 0x0080'0400);
	CHECK_EQ(mcr(K1_MC_FREQ_AUX), 0x0100'0000);
	CHECK_EQ(mcr(K1_MC_PI_CFG), 0);
	CHECK_EQ(mcr(K1_MC_REFRESH0), 0x0030'd400);
	CHECK_EQ(mcr(K1_MC_REFRESH1), 0x0004'e200);
	CHECK_EQ(mcr(K1_MC_REFRESH2), 0x0c80'0000);
	CHECK_EQ(mcr(K1_MC_STATUS), 0x33);
	CHECK_EQ(mcr(K1_MC_PI_STATUS), 0x8000'8000);
	CHECK_EQ(mcr(K1_MC_DFC_PRE), 0);
	CHECK_EQ(mcr(0x28), 0);

	for (uint32_t pp = 0; pp < 4; pp++) {
		const uint32_t s = pp << K1_PHY_SLICE_SHIFT;

		for (uint32_t c = 0; c < 2; c++) {
			const uint32_t com = s + (c ? K1_SLICE_COMMON1 : K1_SLICE_COMMON0);

			CHECK_EQ(phyr(com), 1);
			CHECK_EQ(phyr(com + 0x4), 0x5555'a927);
			CHECK_EQ(phyr(com + 0xc), 0x36e4'd800);
			CHECK_EQ(phyr(com + 0x10), 0x1000'0000);
			CHECK_EQ(phyr(com + 0x14), 0xffff'ffef);
		}
		CHECK_EQ(phyr(s + 0x64), 0x4349);
		CHECK_EQ(phyr(s + 0x1064), 0);
		CHECK_EQ(phyr(s + 0x164), 0);
	}
	CHECK_EQ(phyr(0x3030), 0x1077);
	CHECK_EQ(phyr(0x7030), 0);
	CHECK_EQ(phyr(0x3230), 0);
	CHECK_EQ(phyr(K1_PHY_OTHER_24), 0);
	CHECK_EQ(phyr(K1_PHY_OTHER), 1);
	CHECK_EQ(phyr(K1_PHY_INIT), 0x100);
	CHECK_EQ(pmur(K1_PMU_PHY_BIAS), 0x0f00);
	CHECK_EQ(pmur(K1_PMU_PHY_LDO), 0x0c00);

	CHECK_EQ(g_model.n_mr, 9);
	for (uint32_t i = 0; i < 9; i++)
		CHECK_EQ(g_model.mr_cmd[i], mr_want[i]);
	CHECK_EQ(g_model.n_zq, 4);
	for (uint32_t i = 0; i < 4; i++)
		CHECK_EQ(g_model.zq_cmd[i], zq_want[i]);
	CHECK(nth(EV_DFI_INIT, 0) < nth(EV_DRAM_INIT, 0));
	CHECK(nth(EV_DRAM_INIT, 0) < nth(EV_MR_WRITE, 0));
	CHECK(nth(EV_ZQ, 0) > nth(EV_MR_WRITE, 4));
	CHECK(nth(EV_ZQ, 3) < nth(EV_MR_WRITE, 5));
	CHECK_EQ(g_model.die_mr14[1][1], 0x19);
	CHECK_EQ(k1_ddrt_check_mr14(&mc, &g_env), K1_DDRT_OK);
	CHECK_CLEAN();
}

void test_device_init_guards(void)
{
	const struct k1_ddr_boot_cfg cfg = cfg_for(2, 2400, K1_DDR_LPDDR4X);
	struct k1_ddr_hw hw = k1_ddr_hw_init(&cfg, &g_env);
	enum k1_ddr_step step = K1_STEP_NONE;

	/* no PLL / MC clock: the controller drops writes, CTL0 does not stick */
	model_reset_por(DDR, 2);
	CHECK_EQ(k1_ddr_device_init(&hw, false, &step), K1_DDRT_EIO);
	CHECK_EQ(step, K1_STEP_MC);
	CHECK(g_model.violations > 0);
	CHECK_EQ(g_model.ev_count[EV_DFI_INIT], 0);

	/* CTL_FREQ not back on fp0 after the banks */
	model_reset_por(DDR, 2);
	step = K1_STEP_NONE;
	CHECK_EQ(k1_ddr_pll_init(&hw, 2400, &step), K1_DDRT_OK);
	g_model.freq_read_or = 0x1000'0000;
	CHECK_EQ(k1_ddr_device_init(&hw, false, &step), K1_DDRT_EIO);
	CHECK_EQ(step, K1_STEP_MC);
	CHECK_CLEAN();
}

void test_device_init_x8(void)
{
	const struct k1_ddr_boot_cfg cfg = cfg_for(2, 2400, K1_DDR_LPDDR4X);

	model_reset_por(DDR, 2);
	(void)bring_up(&cfg, true);
	CHECK_CLEAN();
	CHECK_EQ(model_peek_bank(3, 0x13e4), 0x1900'0a02);
	CHECK_EQ(model_peek_bank(3, 0x3b4), 0x0800'1400);
	CHECK_EQ(model_peek_bank(2, 0x13e4), 0x1500'0802);
	CHECK_EQ(model_peek_bank(2, 0x3b4), 0x0800'1000);
	CHECK_EQ(model_peek_bank(1, 0x13e4), 0x0c00'0402);
	CHECK_EQ(model_peek_bank(1, 0x3b4), 0x0800'0a00);
	CHECK_EQ(model_peek_bank(0, 0x13e4), 0x0c00'0402);
	CHECK_EQ(model_peek_bank(0, 0x3b4), 0x0800'0a00);
	CHECK_EQ(model_peek_bank(2, 0x3b0), 0x1c18'0a18);
	CHECK_EQ(mcr(K1_MC_FREQ), 0x0080'0400);
}

void test_device_init_lpddr4(void)
{
	const struct k1_ddr_boot_cfg cfg = cfg_for(2, 2400, K1_DDR_LPDDR4);

	model_reset_por(DDR, 2);
	(void)bring_up(&cfg, false);
	CHECK_CLEAN();
	for (uint32_t fp = 0; fp < 4; fp++) {
		CHECK_EQ(model_peek_bank(fp, K1_MC_TXVREF), 0xb2a7'3314);
		CHECK_EQ(model_peek_bank(fp, K1_MC_DS0), 0x0024'0000);
		CHECK_EQ(model_peek_bank(fp, K1_MC_DS1), 0x0024'0000);
	}
	CHECK_EQ(model_peek_bank(3, K1_MC_WDQS), 0x8000'1e06);
	CHECK_EQ(model_peek_bank(2, K1_MC_WDQS), 0x8000'1b04);
	CHECK_EQ(model_peek_bank(1, K1_MC_WDQS), 0x8000'1500);
	CHECK_EQ(model_peek_bank(0, K1_MC_WDQS), 0x8000'1500);
	CHECK_EQ(phyr(K1_SLICE_COMMON0 + 0xc), 0x36d2'b000);
	CHECK_EQ(phyr(K1_SLICE_COMMON1 + 0x4), 0x3333'a927);
	CHECK_EQ(pmur(K1_PMU_PHY_LDO), 0x3800);
}

void test_geometry(void)
{
	static const struct {
		uint8_t mr8;
		int rc;
		uint32_t area, code, row;
		bool x8;
	} t[] = {
		{ 0x52, K1_DDRT_OK, 4096, 0x10, 17, true },
		{ 0x18, K1_DDRT_OK, 4096, 0x10, 17, false },
		{ 0x10, K1_DDRT_OK, 2048, 0x0f, 16, false },
		{ 0x58, K1_DDRT_OK, 8192, 0x11, 18, true },
		{ 0x00, K1_DDRT_OK, 512, 0x0d, 14, false },
		{ 0x04, K1_DDRT_OK, 768, 0x01, 15, false },
		{ 0x08, K1_DDRT_OK, 1024, 0x0e, 15, false },
		{ 0x0c, K1_DDRT_OK, 1536, 0x02, 16, false },
		{ 0x14, K1_DDRT_OK, 3072, 0x03, 17, false },
		{ 0x40, K1_DDRT_OK, 1024, 0x0e, 15, true },
		{ 0x54, K1_DDRT_EINVAL, 0, 0, 0, false },
		{ 0x30, K1_DDRT_EINVAL, 0, 0, 0, false },
		{ 0x80, K1_DDRT_EINVAL, 0, 0, 0, false },
		{ 0xc0, K1_DDRT_EINVAL, 0, 0, 0, false },
		{ 0x1c, K1_DDRT_EINVAL, 0, 0, 0, false },
	};

	for (size_t i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
		struct k1_ddr_geom g = {};

		CHECK_EQ(k1_ddr_geometry(t[i].mr8, &g), t[i].rc);
		if (t[i].rc)
			continue;
		CHECK_EQ(g.area_mb, t[i].area);
		CHECK_EQ(g.code, t[i].code);
		CHECK_EQ(g.row, t[i].row);
		CHECK_EQ(g.x8, t[i].x8);
	}
	CHECK_EQ(k1_ddr_size_code(16384), 0x12);
	CHECK_EQ(k1_ddr_size_code(6144), 0);
}

void test_map_apply(void)
{
	const struct k1_ddr_boot_cfg c2 = cfg_for(2, 2400, K1_DDR_LPDDR4X);
	const struct k1_ddr_boot_cfg c1 = cfg_for(1, 2400, K1_DDR_LPDDR4X);
	struct k1_ddr_geom g;
	struct k1_ddr_hw hw;

	model_reset_por(DDR, 2);
	hw = bring_up(&c2, false);
	CHECK_EQ(k1_ddr_geometry(0x52, &g), K1_DDRT_OK);
	k1_ddr_map_apply(&hw, &g);
	CHECK_CLEAN();
	CHECK_EQ(mcr(K1_MC_CS0_MAP), 0x0010'0001);
	CHECK_EQ(mcr(0x204), 0);
	CHECK_EQ(mcr(K1_MC_CS1_MAP), 0x0010'0001);
	CHECK_EQ(mcr(K1_MC_CS1_HI), 1);
	CHECK_EQ(mcr(K1_MC_CS0_GEOM), 0x0503'0732);
	CHECK_EQ(mcr(K1_MC_CS1_GEOM), 0x0503'0732);

	CHECK_EQ(k1_ddr_geometry(0x58, &g), K1_DDRT_OK);
	k1_ddr_map_apply(&hw, &g);
	CHECK_EQ(mcr(K1_MC_CS0_MAP), 0x0011'0001);
	CHECK_EQ(mcr(K1_MC_CS1_MAP), 0x0011'0001);
	CHECK_EQ(mcr(K1_MC_CS1_HI), 2);
	CHECK_EQ(mcr(K1_MC_CS0_GEOM), 0x0503'0832);

	/* one rank: CS1 start still written, present cleared */
	model_reset_por(DDR, 1);
	hw = bring_up(&c1, false);
	CHECK_EQ(k1_ddr_geometry(0x10, &g), K1_DDRT_OK);
	k1_ddr_map_apply(&hw, &g);
	CHECK_CLEAN();
	CHECK_EQ(mcr(K1_MC_CS0_MAP), 0x000f'0001);
	CHECK_EQ(mcr(K1_MC_CS1_MAP), 0x800f'0000);
	CHECK_EQ(mcr(K1_MC_CS1_HI), 0);
	CHECK_EQ(mcr(K1_MC_CS1_GEOM), 0x0503'0632);
}

void test_dfc_table(void)
{
	static const uint32_t tail[15][2] = {
		{ 0x0002'0200, 0x13e0 }, { 0x1300'0010, 0x13d0 },
		{ 0x0001'0000, 0x33fc }, { 0x0001'0000, 0x33fc },
		{ 0x1300'0008, 0x0020 }, { 0x1300'0004, 0x0020 },
		{ 0x1302'0000, 0x0028 }, { 0x1300'0001, 0x13d0 },
		{ 0x0000'8000, 0x33fc }, { 0x0000'8000, 0x33fc },
		{ 0x1000'0100, 0x13d0 }, { 0x0000'8000, 0x33fc },
		{ 0x0000'8000, 0x33fc }, { 0x1302'000d, 0x0024 },
		{ 0x1302'0003, 0x0024 },
	};
	static const uint32_t marks[3][3] = {
		{ 0x0000'1100, 0x0001'0000, 0x20 },
		{ 0x0000'00ff, 0x0001'001c, 0x21 },
		{ 0x0000'0000, 0x0005'001c, 0x22 },
	};
	const struct k1_ddr_boot_cfg cfg = cfg_for(2, 2400, K1_DDR_LPDDR4X);
	struct k1_ddr_hw hw;

	model_reset_por(DDR, 2);
	hw = bring_up(&cfg, false);
	k1_ddr_dfc_table_load(&hw, false);
	CHECK_CLEAN();

	for (size_t i = 0; i < K1_N_DFC_FLOW; i++)
		CHECK(tbl_is(k1_dfc_flow[i].idx, k1_dfc_flow[i].data,
			     k1_dfc_flow[i].addr));
	for (size_t i = 0; i < K1_N_DFC_EXIT; i++)
		CHECK(tbl_is(k1_dfc_exit[i].idx, k1_dfc_exit[i].data,
			     k1_dfc_exit[i].addr));
	CHECK(!g_model.tbl[0x1c].set);
	CHECK(!g_model.tbl[0x18f].set);
	CHECK(tbl_is(0x10, 0x1300'0001, 0x13d0));
	CHECK(tbl_is(0x13, 0x0004'0303, 0x0001'0044));

	/* doc 30: REQ_PMU on the first line, EOP on the last */
	CHECK(tbl_is(0x200, 0x0004'1304, 0x0001'0044));
	CHECK(tbl_is(0x201, 0x0000'0001, 0x48));
	CHECK(tbl_is(0x208, 0x0010'0001, 0x200));
	CHECK(tbl_is(0x216, 0xffff'ffff, 0xacc));
	for (uint32_t n = 0; n < 4; n++) {
		static const uint32_t ram[4] = {
			0x0080'0400, 0x4080'0400, 0x8080'0400, 0xc080'0400,
		};
		static const uint16_t at[4] = { 0x217, 0x230, 0x249, 0x262 };
		const uint32_t base = at[n];

		CHECK(tbl_is(base, ram[n], 0x304));
		CHECK(tbl_is(base + 1, model_peek_bank(n, 0x300), 0x300));
		CHECK(tbl_is(base + 2, 0x9d19'4314, 0x30c));
		CHECK(tbl_is(base + 23, model_peek_bank(n, 0x13e4), 0x13e4));
		CHECK(tbl_is(base + 24, model_peek_bank(n, 0x13ec), 0x13ec));
	}
	CHECK(tbl_is(0x217 + 25 * 2 + 1, 0x0000'0c18, 0x300));
	CHECK(tbl_is(0x217 + 25 * 1 + 23, 0x0a00'0402, 0x13e4));
	for (uint32_t i = 0; i < 15; i++)
		CHECK(tbl_is(0x27b + i, tail[i][0], tail[i][1]));
	CHECK(tbl_is(0x28a, 0x0004'0300, 0x0002'0044));
	CHECK(!g_model.tbl[0x28b].set);
	for (uint32_t i = 0x200; i <= 0x28a; i++)
		CHECK(g_model.tbl[i].ok);
	CHECK_EQ(mcr(K1_MC_FREQ), 0x0080'0400);

	/* everything went through the window, nothing through the MC port */
	CHECK_EQ(g_model.n_marks, 3);
	for (uint32_t i = 0; i < 3; i++)
		for (uint32_t k = 0; k < 3; k++)
			CHECK_EQ(g_model.marks[i][k], marks[i][k]);
	CHECK_EQ(mcr(K1_MC_TBL_INDEX), 0);
	CHECK_EQ(phyr(K1_PHY_MARK_CMD), 0);
}

void test_dfc_table_variants(void)
{
	const struct k1_ddr_boot_cfg c1 = cfg_for(1, 2400, K1_DDR_LPDDR4X);
	const struct k1_ddr_boot_cfg c2 = cfg_for(2, 2400, K1_DDR_LPDDR4X);
	struct k1_ddr_hw hw;

	/* one rank: the CS byte of USER_0, CA and PI commands follows cs_sel */
	model_reset_por(DDR, 1);
	hw = bring_up(&c1, false);
	k1_ddr_dfc_table_load(&hw, false);
	CHECK_CLEAN();
	CHECK(tbl_is(0x00, 0x0004'0303, 0x44));
	CHECK(tbl_is(0x01, 0x1100'0008, 0x20));
	CHECK(tbl_is(0x02, 0x1301'0000, 0x28));
	CHECK(tbl_is(0x03, 0x1102'000d, 0x24));
	CHECK(tbl_is(0x0b, 0x1300'8000, 0x28));
	CHECK(tbl_is(0x10, 0x1100'0001, 0x13d0));
	CHECK(tbl_is(0x14, 0x1000'0100, 0x13d0));
	CHECK(tbl_is(0x17, 0x1100'0004, 0x20));
	CHECK(tbl_is(0x184, 0x1100'0001, 0x13d0));
	CHECK(tbl_is(0x27c, 0x1100'0010, 0x13d0));
	CHECK(tbl_is(0x281, 0x1302'0000, 0x28));
	CHECK(tbl_is(0x288, 0x1102'000d, 0x24));
	CHECK(tbl_is(0x289, 0x1102'0003, 0x24));
	CHECK(g_model.tbl[0x27c].ok && g_model.tbl[0x281].ok);

	/* 16 GiB: MR21 after MR13, the rest of the run moves up by one */
	model_reset_por(DDR, 2);
	hw = bring_up(&c2, false);
	k1_ddr_dfc_table_load(&hw, true);
	CHECK_CLEAN();
	CHECK(tbl_is(0x18, 0x1302'000d, 0x24));
	CHECK(tbl_is(0x19, 0x1302'0095, 0x24));
	CHECK(tbl_is(0x1a, 0x0000'0002, 0x2008));
	CHECK(tbl_is(0x1b, 0x0000'0000, 0x2008));
	CHECK(tbl_is(0x1c, 0x0004'0380, 0x0002'0044));
	CHECK(tbl_is(0x12e, 0x0004'0380, 0x0002'0044));
	CHECK(!g_model.tbl[0x12f].set);
	CHECK(tbl_is(0x18b, 0x1302'000d, 0x24));
	CHECK(tbl_is(0x18c, 0x1302'0095, 0x24));
	CHECK(tbl_is(0x18d, 0x0000'0002, 0x2008));
	CHECK(tbl_is(0x18f, 0x0004'0b00, 0x0002'0044));
	/* the snapshot does not move */
	CHECK(tbl_is(0x200, 0x0004'1304, 0x0001'0044));
	CHECK(tbl_is(0x289, 0x1302'0003, 0x24));
	CHECK(tbl_is(0x28a, 0x0004'0300, 0x0002'0044));
}

void test_dfc(void)
{
	const struct k1_ddr_boot_cfg cfg = cfg_for(2, 2400, K1_DDR_LPDDR4X);
	struct k1_ddr_hw hw;

	/* the engine needs its table */
	model_reset_por(DDR, 2);
	hw = bring_up(&cfg, false);
	CHECK_EQ(k1_ddr_dfc(&hw, 1), K1_DDRT_ETIMEDOUT);
	CHECK_CLEAN();
	CHECK(g_model.dfc_hung);
	CHECK(strstr(g_model.tbl_why, "command flow") != nullptr);

	model_reset_por(DDR, 2);
	hw = bring_up(&cfg, false);
	k1_ddr_dfc_table_load(&hw, false);
	CHECK_EQ(k1_ddr_dfc(&hw, 1), K1_DDRT_OK);
	CHECK_CLEAN();
	CHECK_EQ(mcr(K1_MC_FREQ), 0x5080'0400);
	CHECK_EQ(mcr(K1_MC_PI_CFG), 1 << 2);
	CHECK_EQ(mcr(K1_MC_STATUS), 0x33);
	CHECK_EQ(mcr(K1_MC_DFC_PRE), 0x80ac'0000);
	CHECK_EQ(pmur(K1_PMU_CLKSEL), 0x3'3b04);
	CHECK_EQ(pmur(K1_PMU_HW_SLP), 0x4020'0208);
	CHECK_EQ(pmur(K1_PMU_AP_IMR), 0x10);
	CHECK_EQ(pmur(K1_PMU_CC_AP), 0);
	CHECK_EQ(g_model.boot_pp, 1);
	CHECK_EQ(g_model.n_dfc, 1);

	CHECK_EQ(k1_ddr_dfc(&hw, 2), K1_DDRT_OK);
	CHECK_CLEAN();
	CHECK_EQ(mcr(K1_MC_FREQ), 0xa080'0400);
	CHECK_EQ(mcr(K1_MC_PI_CFG), 2 << 2);
	CHECK_EQ(pmur(K1_PMU_CLKSEL), 0x3'3b40);
	CHECK_EQ(pmur(K1_PMU_HW_SLP), 0x4020'0210);
	CHECK_EQ(g_model.dfc_fp[1], 2);

	CHECK_EQ(k1_ddr_dfc(&hw, 4), K1_DDRT_EINVAL);
	/* the engine did not land on the requested bank */
	g_model.freq_read_or = 0x2000'0000;
	CHECK_EQ(k1_ddr_dfc(&hw, 1), K1_DDRT_EIO);
	g_model.freq_read_or = 0;
	CHECK_CLEAN();
	g_model.dfc_stuck = true;
	CHECK_EQ(k1_ddr_dfc(&hw, 1), K1_DDRT_ETIMEDOUT);
	CHECK_CLEAN();
}

/* one table line written as the live register would be */
static void tbl_write(uint32_t idx, uint32_t data, uint32_t addr)
{
	k1_w32(WIN + K1_MC_TBL_DATA, data);
	k1_w32(WIN + K1_MC_TBL_ADDR, addr);
	k1_w32(WIN + K1_MC_TBL_INDEX, idx);
}

void test_dfc_stuck(void)
{
	const struct k1_ddr_boot_cfg cfg = cfg_for(2, 2400, K1_DDR_LPDDR4X);
	struct k1_ddr_hw hw;
	uint32_t slp;

	/* a window that aliases the MC behaves the same */
	model_reset_por(DDR, 2);
	g_model.win_alias = true;
	hw = bring_up(&cfg, false);
	k1_ddr_dfc_table_load(&hw, false);
	CHECK_EQ(k1_ddr_dfc(&hw, 1), K1_DDRT_OK);
	CHECK_CLEAN();
	CHECK_EQ(g_model.n_marks, 3);
	CHECK_EQ(mcr(K1_MC_FREQ), 0x5080'0400);

	/*
	 * Through the MC port of a separate window the RAM stays empty: the
	 * ranks park, CTL_FREQ stays at fp0, bit 22 is held (silicon, P-14).
	 */
	model_reset_por(DDR, 2);
	hw = bring_up(&cfg, false);
	hw.win = DDR;
	k1_ddr_dfc_table_load(&hw, false);
	CHECK_EQ(g_model.tbl_writes, 0);
	CHECK_EQ(k1_ddr_dfc(&hw, 1), K1_DDRT_ETIMEDOUT);
	CHECK_CLEAN();
	CHECK(g_model.dfc_hung);
	CHECK_EQ(mcr(K1_MC_STATUS) & 0x44, 0);
	CHECK_EQ(pmur(K1_PMU_CC_AP), K1_CC_DFC_KICK);
	CHECK_EQ(mcr(K1_MC_FREQ), 0x0080'0400);
	CHECK_EQ(g_model.n_dfc, 0);
	/* no second kick on top of a held bit 22 */
	CHECK_EQ(k1_ddr_dfc(&hw, 1), K1_DDRT_EIO);
	CHECK_CLEAN();

	/* a snapshot without its EOP line */
	model_reset_por(DDR, 2);
	hw = bring_up(&cfg, false);
	k1_ddr_dfc_table_load(&hw, false);
	g_model.tbl[0x28a].set = false;
	CHECK_EQ(k1_ddr_dfc(&hw, 1), K1_DDRT_ETIMEDOUT);
	CHECK_CLEAN();
	CHECK(strstr(g_model.tbl_why, "0x28a") != nullptr);

	/* the live CTL_FREQ of fp1 stored with its [29:28] */
	model_reset_por(DDR, 2);
	hw = bring_up(&cfg, false);
	k1_ddr_dfc_table_load(&hw, false);
	model_poke(DDR + K1_MC_FREQ, 0x5080'0400);
	tbl_write(0x230, 0x5080'0400, K1_MC_FREQ);
	model_poke(DDR + K1_MC_FREQ, 0x0080'0400);
	CHECK(!g_model.tbl[0x230].ok);
	CHECK_EQ(k1_ddr_dfc(&hw, 1), K1_DDRT_ETIMEDOUT);
	CHECK(strstr(g_model.tbl_why, "0x230") != nullptr);

	/* one rank: the SR exit after the timeout is for CS0 only */
	const struct k1_ddr_boot_cfg c1 = cfg_for(1, 2400, K1_DDR_LPDDR4X);

	model_reset_por(DDR, 1);
	hw = bring_up(&c1, false);
	k1_ddr_dfc_table_load(&hw, false);
	g_model.dfc_stuck = true;
	CHECK_EQ(k1_ddr_dfc(&hw, 1), K1_DDRT_ETIMEDOUT);
	CHECK_CLEAN();
	CHECK_EQ(g_model.sr_exit_cmd, 0x1100'0080);
	CHECK_EQ(mcr(K1_MC_STATUS) & 0x04, 0);

	/* ranks still parked: refused before anything is written */
	model_reset_por(DDR, 2);
	hw = bring_up(&cfg, false);
	k1_ddr_dfc_table_load(&hw, false);
	model_poke(DDR + K1_MC_STATUS, 0x77);
	slp = pmur(K1_PMU_HW_SLP);
	CHECK_EQ(k1_ddr_dfc(&hw, 1), K1_DDRT_EIO);
	CHECK_CLEAN();
	CHECK_EQ(pmur(K1_PMU_CC_AP) & K1_CC_DFC_KICK, 0);
	CHECK_EQ(pmur(K1_PMU_HW_SLP), slp);
	CHECK_EQ(g_model.n_dfc, 0);
}

void test_full_boot_8g(void)
{
	static const uint8_t pps[4] = { 0, 0, 1, 2 };
	static const uint32_t dfcs[3] = { 1, 2, 2 };
	struct k1_ddr_boot_report rep;

	model_reset_por(DDR, 2);
	CHECK_EQ(run_boot(2, 2400, &rep), K1_DDRT_OK);
	CHECK_CLEAN();
	CHECK_EQ(rep.rc, K1_DDRT_OK);
	CHECK_EQ(rep.fail_step, K1_STEP_NONE);
	CHECK_EQ(rep.n_pass, 4);
	for (uint32_t i = 0; i < 4; i++) {
		CHECK_EQ(rep.pass[i].boot_pp, pps[i]);
		CHECK_EQ(rep.pass[i].rc, K1_DDRT_OK);
		CHECK_EQ(rep.pass[i].phy_status, 0);
		CHECK_EQ(rep.pass[i].rx_vref_idx, 5);
		CHECK_EQ(rep.pass[i].tx_vref_dq, 28);
	}
	CHECK_EQ(rep.mr5, 0x01);
	CHECK_EQ(rep.mr8, 0x52);
	CHECK(rep.byte_mode);
	CHECK_EQ(rep.size_mb, 8192);
	CHECK_EQ(rep.final_pp, 2);
	CHECK_EQ(rep.ctl_freq, 0xa080'0400);
	CHECK_EQ(rep.status, 0x33);
	CHECK_EQ(rep.clksel, 0x3'3b40);
	CHECK(rep.total_us > 0);

	CHECK_EQ(g_model.ev_count[EV_PLL_LOCK], 1);
	CHECK_EQ(g_model.ev_count[EV_SWFC], 1);
	CHECK_EQ(g_model.mc_reset_count, 1);
	CHECK_EQ(g_model.ev_count[EV_DFI_INIT], 2);
	CHECK_EQ(g_model.ev_count[EV_DRAM_INIT], 2);
	CHECK_EQ(g_model.n_mr, 18);
	CHECK_EQ(g_model.n_zq, 8);
	CHECK_EQ(g_model.ev_count[EV_SR_ENTER], 4);
	CHECK_EQ(g_model.n_dfc, 3);
	for (uint32_t i = 0; i < 3; i++)
		CHECK_EQ(g_model.dfc_fp[i], dfcs[i]);

	/* doc 31 order */
	CHECK(nth(EV_SWFC, 0) < nth(EV_DFI_INIT, 0));
	CHECK(nth(EV_DRAM_INIT, 0) < nth(EV_SR_ENTER, 0));
	CHECK(nth(EV_SR_EXIT, 0) < nth(EV_MR_READ, 0));
	CHECK(nth(EV_MR_READ, 1) < nth(EV_MC_RESET, 0));
	CHECK(nth(EV_MC_RESET, 0) < nth(EV_DRAM_INIT, 1));
	CHECK(nth(EV_DRAM_INIT, 1) < nth(EV_SR_ENTER, 1));
	CHECK(nth(EV_SR_EXIT, 1) < nth(EV_DFC, 0));
	CHECK(nth(EV_DFC, 0) < nth(EV_SR_ENTER, 2));
	CHECK(nth(EV_SR_EXIT, 2) < nth(EV_DFC, 1));
	CHECK(nth(EV_DFC, 1) < nth(EV_SR_ENTER, 3));
	CHECK(nth(EV_SR_EXIT, 3) < nth(EV_DFC, 2));

	/* x8 timings everywhere, trained Tx Vref and RD_CFG in the trained banks */
	CHECK_EQ(model_peek_bank(2, 0x13e4), 0x1500'0802);
	CHECK_EQ(model_peek_bank(0, 0x3b4), 0x0800'0a00);
	for (uint32_t fp = 0; fp <= 2; fp++) {
		CHECK_EQ(model_peek_bank(fp, K1_MC_TXVREF), 0x9d1c'4314);
		CHECK_EQ(model_peek_bank(fp, K1_MC_PI_RD_CFG), 0x5070);
	}
	CHECK_EQ(model_peek_bank(3, K1_MC_TXVREF), 0x9d19'4314);
	CHECK_EQ(model_peek_bank(3, K1_MC_PI_RD_CFG), 0x0480);
	CHECK_EQ(g_model.die_mr14[1][1], 28);

	CHECK_EQ(mcr(K1_MC_CS0_MAP), 0x0010'0001);
	CHECK_EQ(mcr(K1_MC_CS1_MAP), 0x0010'0001);
	CHECK_EQ(mcr(K1_MC_CS1_HI), 1);
	CHECK_EQ(mcr(K1_MC_CS0_GEOM), 0x0503'0732);
	for (uint32_t pp = 0; pp <= 2; pp++) {
		CHECK_EQ(phyr((pp << K1_PHY_SLICE_SHIFT) + K1_SLICE_VREF0),
			 k1_pack_vref(5) | 0xa937);
		CHECK_EQ(phyr((pp << K1_PHY_SLICE_SHIFT) + K1_SLICE_VREF1),
			 k1_pack_vref(5) | 0xa927);
	}
	CHECK_EQ(phyr((3 << K1_PHY_SLICE_SHIFT) + K1_SLICE_VREF0), 0x5555'a927);
}

void test_full_boot_1cs(void)
{
	struct k1_ddr_boot_report rep;

	model_reset_por(DDR, 1);
	g_model.mr[8] = 0x18;
	CHECK_EQ(run_boot(1, 2666, &rep), K1_DDRT_OK);
	CHECK_CLEAN();
	CHECK(!rep.byte_mode);
	CHECK_EQ(g_model.mc_reset_count, 0);
	CHECK_EQ(g_model.ev_count[EV_DRAM_INIT], 1);
	CHECK_EQ(rep.n_pass, 4);
	CHECK_EQ(rep.size_mb, 4096);
	CHECK_EQ(pmur(K1_PMU_PLL1_CTL1), 0x203c'5555);
	CHECK_EQ(g_model.sr_enter_cmd, 0x1100'0040);
	CHECK_EQ(g_model.n_zq, 2);
	CHECK_EQ(g_model.zq_cmd[0], 0x1100'2000);
	CHECK_EQ(g_model.zq_cmd[1], 0x1100'1000);
	CHECK_EQ(g_model.mr_cmd[0], 0x1102'0001);
	CHECK_EQ(mcr(K1_MC_CS0_MAP), 0x0010'0001);
	CHECK_EQ(mcr(K1_MC_CS1_MAP), 0x0010'0000);
	CHECK_EQ(mcr(K1_MC_CS1_HI), 1);
	CHECK_EQ(mcr(K1_MC_CS0_GEOM), 0x0503'0732);
	CHECK(tbl_is(0x01, 0x1100'0008, 0x20));
	CHECK_EQ(rep.status, 0x03);
	CHECK_EQ(rep.ctl_freq, 0xa080'0400);
	CHECK_EQ(g_model.n_dfc, 3);
	CHECK_EQ(g_model.dfc_fp[2], 2);
}

void test_full_boot_rates(void)
{
	static const struct {
		uint32_t rate, fp;
	} t[] = {
		{ 1200, 0 }, { 1066, 1 }, { 2133, 0 }, { 2400, 2 }, { 1600, 0 },
		{ 0, 0 },
		/* doc 30 (P-20): the SoC stops at 2666, above is not rounded down */
		{ 2667, 0 }, { 2700, 0 }, { 3200, 0 }, { 4266, 0 },
	};

	for (size_t i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
		struct k1_ddr_boot_report rep;

		model_reset_por(DDR, 2);
		CHECK_EQ(run_boot(2, t[i].rate, &rep), K1_DDRT_OK);
		CHECK_CLEAN();
		CHECK_EQ(rep.final_pp, t[i].fp);
		CHECK_EQ(g_model.n_dfc, 3);
		CHECK_EQ(g_model.dfc_fp[2], t[i].fp);
		CHECK_EQ(rep.ctl_freq, k1_freq_bank(t[i].fp));
		CHECK_EQ(rep.clksel & 0xffff, k1_clksel[t[i].fp]);
		CHECK_EQ(pmur(K1_PMU_PLL1_CTL1), 0x2a20'3b00);
	}
}

void test_full_boot_16g(void)
{
	struct k1_ddr_boot_report rep;

	model_reset_por(DDR, 2);
	g_model.mr[8] = 0x58;
	CHECK_EQ(run_boot(2, 2400, &rep), K1_DDRT_OK);
	CHECK_CLEAN();
	CHECK_EQ(rep.size_mb, 16384);
	CHECK(rep.byte_mode);
	CHECK_EQ(mcr(K1_MC_CS0_MAP), 0x0011'0001);
	CHECK_EQ(mcr(K1_MC_CS1_MAP), 0x0011'0001);
	CHECK_EQ(mcr(K1_MC_CS1_HI), 2);
	CHECK_EQ(mcr(K1_MC_CS0_GEOM), 0x0503'0832);
	CHECK(tbl_is(0x19, 0x1302'0095, 0x24));
	CHECK(tbl_is(0x18c, 0x1302'0095, 0x24));
	CHECK_EQ(g_model.n_mr, 19);
	CHECK_EQ(g_model.mr_cmd[18], 0x1302'0095);
	CHECK(last(EV_MR_WRITE) > last(EV_SR_EXIT));
	CHECK(last(EV_MR_WRITE) < last(EV_DFC));
}

/*
 * The MR14 reads between DFC number k (1-based) and the next pass: the
 * discarded one first, then the four of the check.
 */
static bool mr14_after_dfc(uint32_t k, uint8_t first, uint8_t want)
{
	uint32_t n = 0;

	for (uint32_t i = 0; i < g_model.n_mr14_read; i++) {
		const typeof(g_model.mr14_read[0]) *r = &g_model.mr14_read[i];
		const uint8_t w = n ? want : first;

		if (r->n_dfc != k || r->n_sr != k + 1)
			continue;
		n++;
		if (r->val != w) {
			printf("  MR14 read %u after DFC %u: 0x%02x, want 0x%02x\n",
			       n, k, r->val, w);
			return false;
		}
	}
	if (n != 5)
		printf("  %u MR14 reads after DFC %u\n", n, k);
	return n == 5;
}

/* after pp2 the fp2 bank asks for a Vref the dies were never given */
static void move_fp2_txvref(void *ctx, const struct k1_ddrt_result *res,
			    uint32_t pp)
{
	const uintptr_t a = DDR + K1_MC_TXVREF;

	(void)ctx;
	(void)res;
	if (pp == 2)
		model_poke(a, (model_peek(a) & K1_TXVREF_MASK) | (30u << K1_TXVREF_SHIFT));
}

/* doc 08 (P-19): after the first DFC(2) the first MRR reads 0, then the bank */
void test_full_boot_fsp(void)
{
	struct k1_ddr_boot_cfg cfg = cfg_for(2, 2400, K1_DDR_LPDDR4X);
	struct k1_ddr_boot_report rep;

	model_reset_por(DDR, 2);
	log_clear();
	CHECK_EQ(k1_ddr_boot(&cfg, &g_env, &rep), K1_DDRT_OK);
	CHECK_CLEAN();
	CHECK(mr14_after_dfc(1, 0x19, 0x19));
	CHECK(mr14_after_dfc(2, 0x00, 0x19));
	CHECK(mr14_after_dfc(3, 28, 28));
	CHECK(!log_has("warning: MR14"));
	CHECK(!log_has("sent after every DFC"));
	CHECK_EQ(g_model.n_mr, 18);

	/* the final DFC carries a bank moved after pp2 to the dies */
	model_reset_por(DDR, 2);
	cfg = cfg_for(2, 2400, K1_DDR_LPDDR4X);
	cfg.on_pass = move_fp2_txvref;
	log_clear();
	CHECK_EQ(k1_ddr_boot(&cfg, &g_env, &rep), K1_DDRT_OK);
	CHECK_CLEAN();
	CHECK(mr14_after_dfc(3, 30, 30));
	CHECK(!log_has("warning: MR14"));

	/* an engine that leaves MR14 alone is reported after every DFC */
	model_reset_por(DDR, 2);
	g_model.dfc_no_mr14 = true;
	cfg = cfg_for(2, 2400, K1_DDR_LPDDR4X);
	log_clear();
	CHECK_EQ(k1_ddr_boot(&cfg, &g_env, &rep), K1_DDRT_OK);
	CHECK_CLEAN();
	CHECK(log_has("k1-ddr: warning: MR14 differs from the fp1 bank"));
	CHECK(log_has("k1-ddr: warning: MR14 differs from the fp2 bank"));

	/* ... and on the final DFC alone, when only the moved bank differs */
	model_reset_por(DDR, 2);
	g_model.dfc_no_mr14 = true;
	cfg.on_pass = move_fp2_txvref;
	log_clear();
	CHECK_EQ(k1_ddr_boot(&cfg, &g_env, &rep), K1_DDRT_OK);
	CHECK_CLEAN();
	CHECK(mr14_after_dfc(3, 28, 28));
	CHECK(log_has("k1-ddr: warning: MR14 differs from the fp2 bank"));
}

void test_boot_failures(void)
{
	const struct k1_ddrt_env no_clock = { .vlog = test_vlog };
	struct k1_ddr_boot_cfg bad[] = {
		cfg_for(0, 2400, K1_DDR_LPDDR4X),
		cfg_for(3, 2400, K1_DDR_LPDDR4X),
		cfg_for(2, 2400, K1_DDR_LPDDR4X),
		cfg_for(2, 2400, K1_DDR_LPDDR4X),
		cfg_for(2, 2400, K1_DDR_LPDDR4X),
	};
	struct k1_ddr_boot_report rep;

	/* nothing in the ddr window is touched while the PLL is not locked */
	model_reset_por(DDR, 2);
	g_model.pll_never_locks = true;
	CHECK_EQ(run_boot(2, 2400, &rep), K1_DDRT_ETIMEDOUT);
	CHECK_CLEAN();
	CHECK_EQ(rep.rc, K1_DDRT_ETIMEDOUT);
	CHECK_EQ(rep.fail_step, K1_STEP_PLL);
	CHECK_EQ(g_model.mc_writes, 0);
	CHECK_EQ(rep.n_pass, 0);

	model_reset_por(DDR, 2);
	g_model.dfi_stuck = true;
	CHECK_EQ(run_boot(2, 2400, &rep), K1_DDRT_ETIMEDOUT);
	CHECK_CLEAN();
	CHECK_EQ(rep.fail_step, K1_STEP_DFI_INIT);
	CHECK_EQ(g_model.ev_count[EV_DRAM_INIT], 0);

	model_reset_por(DDR, 2);
	g_model.dram_init_stuck = true;
	CHECK_EQ(run_boot(2, 2400, &rep), K1_DDRT_ETIMEDOUT);
	CHECK_CLEAN();
	CHECK_EQ(rep.fail_step, K1_STEP_DRAM_INIT);
	CHECK_EQ(g_model.n_mr, 0);

	model_reset_por(DDR, 2);
	g_model.gate_timeout_cs = 2;
	CHECK_EQ(run_boot(2, 2400, &rep), K1_DDRT_EIO);
	CHECK_CLEAN();
	CHECK_EQ(rep.fail_step, K1_STEP_TRAIN);
	CHECK_EQ(rep.n_pass, 1);
	CHECK_EQ(rep.pass[0].fail_stage, K1_STAGE_GATE);
	CHECK_EQ(g_model.mc_reset_count, 0);
	CHECK_EQ(g_model.n_dfc, 0);

	model_reset_por(DDR, 2);
	g_model.mr[8] = 0x80;
	CHECK_EQ(run_boot(2, 2400, &rep), K1_DDRT_EINVAL);
	CHECK_CLEAN();
	CHECK_EQ(rep.fail_step, K1_STEP_MAP);
	CHECK_EQ(rep.n_pass, 1);
	CHECK_EQ(g_model.mc_reset_count, 0);

	model_reset_por(DDR, 2);
	g_model.dfc_stuck = true;
	CHECK_EQ(run_boot(2, 2400, &rep), K1_DDRT_ETIMEDOUT);
	CHECK_CLEAN();
	CHECK_EQ(rep.fail_step, K1_STEP_DFC);
	CHECK_EQ(rep.n_pass, 2);
	/* the DRAM is handed back out of self-refresh, at fp0 */
	CHECK_EQ(rep.status & 0x44, 0);
	CHECK_EQ(rep.ctl_freq, 0x0080'0400);

	bad[2].ddr = 0;
	bad[3].pmu = 0;
	bad[4].dfc_win = 0;
	model_reset_por(DDR, 2);
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		CHECK_EQ(k1_ddr_boot(&bad[i], &g_env, &rep), K1_DDRT_EINVAL);
		CHECK_EQ(rep.fail_step, K1_STEP_ARGS);
	}
	CHECK_EQ(k1_ddr_boot(&bad[2], &no_clock, &rep), K1_DDRT_EINVAL);
	CHECK_EQ(k1_ddr_boot(nullptr, &g_env, &rep), K1_DDRT_EINVAL);
	CHECK_EQ(k1_ddr_boot(&bad[0], &g_env, nullptr), K1_DDRT_EINVAL);
	const struct k1_ddr_boot_cfg l4 = cfg_for(2, 2133, K1_DDR_LPDDR4);
	CHECK_EQ(k1_ddr_boot(&l4, &g_env, &rep), K1_DDRT_ENOSYS);
	CHECK_EQ(g_model.mmio_writes, 0);
	CHECK_CLEAN();
}
