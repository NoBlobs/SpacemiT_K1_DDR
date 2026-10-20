// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Thalles Campagnani
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "model.h"
#include "../lib/k1_ddrt_io.h"
#include "../lib/k1_ddrt_regs.h"

static constexpr uint32_t MODEL_SPAN = 0x60000;
static constexpr uint32_t PMU_SPAN = 0x400;
static constexpr uint32_t TXVREF_INIT = 0x8019'1234;
static constexpr uint32_t VREF_INIT = 0x5555'a937;
static constexpr uint32_t VREF_POR = 0x0000'0127;
static constexpr uint32_t COMMON_DS_POR = 0x36c0'0000;
static constexpr uint32_t COMMON_14_POR = 0xffff'ffff;
static constexpr uint32_t CC_AP_POR     = 0x0001'9249;	/* read on the BPI-F3 */
static constexpr uint32_t PI_IDLE = 0x8000'8000;
static constexpr uint32_t PHY_INIT_POR = 0x100;

/* per-fp words of the MC (doc 26) */
static const uint16_t banked_off[] = {
	0x300, 0x30c, 0x310, 0x314, 0x34c, 0x38c, 0x390, 0x394, 0x398,
	0x39c, 0x3a0, 0x3a4, 0x3a8, 0x3ac, 0x3b0, 0x3b4, 0x3b8, 0x3bc,
	0x3c0, 0x3c4, 0x3d8, 0x3fc, 0x400, 0x13e4, 0x13ec,
};

static uint32_t regs[MODEL_SPAN / 4];
static uint32_t pmu_regs[PMU_SPAN / 4];
struct model g_model;

[[gnu::format(printf, 1, 2)]]
static void violate(const char *fmt, ...)
{
	va_list ap;

	if (g_model.violations++)
		return;
	va_start(ap, fmt);
	vsnprintf(g_model.violation, sizeof(g_model.violation), fmt, ap);
	va_end(ap);
}

static int bank_slot(uint32_t off)
{
	for (uint32_t i = 0; i < sizeof(banked_off) / sizeof(banked_off[0]); i++)
		if (banked_off[i] == off)
			return (int)i;
	return -1;
}

static uint32_t *mc_bank(uint32_t fp, uint32_t off)
{
	const int s = bank_slot(off);

	return s < 0 ? &regs[off / 4] : &g_model.banked[fp][s];
}

static uint32_t *mc(uint32_t off)
{
	return mc_bank(regs[K1_MC_FREQ / 4] >> K1_FREQ_PP_SHIFT, off);
}

static uint32_t *phy(uint32_t off)
{
	return &regs[(K1_PHY_OFF + off) / 4];
}

static uint32_t *pmu(uint32_t off)
{
	return &pmu_regs[off / 4];
}

static uint32_t slice_off(uint32_t pp)
{
	return pp << K1_PHY_SLICE_SHIFT;
}

static uint32_t *byte_reg(uint32_t pp, uint32_t cs0, uint32_t b, uint32_t off)
{
	return phy(slice_off(pp) + K1_BYTE_OFF[b] + (cs0 << K1_PHY_CS_SHIFT) +
		   off);
}

static void record(enum model_ev ev)
{
	g_model.ev_count[ev]++;
	if (g_model.n_trace < sizeof(g_model.trace) / sizeof(g_model.trace[0]))
		g_model.trace[g_model.n_trace++] = ev;
}

static uint32_t expected_sel(void)
{
	return g_model.real_cs == 2 ? 3 : 1;
}

static uint32_t init_status(void)
{
	return g_model.real_cs == 2 ? 0x33 : 0x03;
}

static uint8_t clamp_margin(int v)
{
	return v < 0 ? 0 : (v > 255 ? 255 : (uint8_t)v);
}

static int absdiff(uint32_t a, uint32_t b)
{
	return a > b ? (int)(a - b) : (int)(b - a);
}

uint8_t model_default_rx_margin(const struct model *m, uint32_t step,
				uint32_t cs, uint32_t dq)
{
	/* the worst DQ moves around: it depends on step and rank */
	return clamp_margin(12 - 3 * absdiff(step, m->rx_peak) -
			    (int)((dq * 7 + step + cs) % 4));
}

uint8_t model_default_tx_margin(const struct model *m, uint32_t step,
				uint32_t cs, uint32_t dq)
{
	return clamp_margin(11 - 2 * absdiff(step, m->tx_peak) -
			    (int)((dq * 5 + step * 3 + cs) % 3));
}

/* PHY state after power-on or an MC reset */
static void phy_defaults(uint32_t vref, bool por)
{
	for (uint32_t pp = 0; pp <= K1_MAX_PP; pp++) {
		const uint32_t s = slice_off(pp);

		*phy(s + K1_SLICE_VREF0) = vref;
		*phy(s + K1_SLICE_VREF1) = vref;
		for (uint32_t c = 0; c < 2; c++) {
			g_model.com_zeroed[pp][c] = false;
			g_model.com_armed[pp][c] = !por;
		}
		for (uint32_t cs0 = 0; cs0 < 2; cs0++) {
			for (uint32_t b = 0; b < 4; b++) {
				for (uint32_t l = 0; l < 8; l++) {
					*byte_reg(pp, cs0, b, K1_BYTE_TX_DQ + 4 * l) = 0xf;
					*byte_reg(pp, cs0, b, K1_BYTE_RX_DQ + 4 * l) = 0xf;
				}
				*byte_reg(pp, cs0, b, K1_BYTE_TX_DM) = 0xf;
				*byte_reg(pp, cs0, b, K1_BYTE_GATE) = 0x0a00;
				*byte_reg(pp, cs0, b, 0x60) = (b % 2) ? 0 : 0xff;
			}
		}
		if (por) {
			for (uint32_t c = 0; c < 2; c++) {
				const uint32_t com = s + (c ? K1_SLICE_COMMON1 :
							  K1_SLICE_COMMON0);

				*phy(com) = 0;
				*phy(com + 0x0c) = COMMON_DS_POR;
				*phy(com + 0x10) = 0;
				*phy(com + 0x14) = COMMON_14_POR;
			}
			*byte_reg(pp, 0, 0, 0x64) = 0;
		} else {
			*byte_reg(pp, 0, 0, 0x64) = 0x4349;
		}
	}
	*phy(K1_PHY_INIT) = PHY_INIT_POR;
	*phy(0x18004) = 0x0100'8870;
}

/* reset value not established (doc 26): only the seeds define these words */
static void io_words_unknown(void)
{
	for (uint32_t fp = 0; fp <= K1_MAX_PP; fp++) {
		*mc_bank(fp, K1_MC_TXVREF) = 0xa5a5'a5a5;
		*mc_bank(fp, K1_MC_DS0) = 0xa5a5'a5a5;
		*mc_bank(fp, K1_MC_DS1) = 0xa5a5'a5a5;
	}
}

static void base_reset(uintptr_t ddr, uint32_t real_cs)
{
	memset(regs, 0, sizeof(regs));
	memset(pmu_regs, 0, sizeof(pmu_regs));
	memset(&g_model, 0, sizeof(g_model));

	g_model.ddr = ddr;
	g_model.pmu = 0xd428'2800;
	g_model.win = 0xf000'0000;
	g_model.real_cs = real_cs;
	g_model.rx_margin = model_default_rx_margin;
	g_model.tx_margin = model_default_tx_margin;
	g_model.rx_peak = 5;
	g_model.tx_peak = 7;
	g_model.latency = 3;
	g_model.time_step_us = 7;
	g_model.pmu_delay = 2;
	g_model.mr[5] = 0x01;
	g_model.mr[6] = 0x08;
	g_model.mr[8] = 0x52;
	g_model.mr[12] = 0x5d;
}

void model_reset(uintptr_t ddr, uint32_t boot_pp, uint32_t real_cs)
{
	base_reset(ddr, real_cs);
	g_model.strict = true;
	g_model.boot_pp = boot_pp;
	g_model.locked = g_model.swfc_done = g_model.mck_on = true;
	g_model.dfi_started = g_model.dfi_cleared = g_model.dram_ready = true;

	*mc(K1_MC_STATUS) = 0x33;
	*mc(K1_MC_PI_STATUS) = PI_IDLE;
	*mc(K1_MC_FREQ) = (boot_pp << 30) | (boot_pp << 28) | 0x0080'0400;
	*mc(K1_MC_PI_CFG) = boot_pp << 2;
	for (uint32_t fp = 0; fp <= K1_MAX_PP; fp++) {
		*mc_bank(fp, K1_MC_TXVREF) = TXVREF_INIT;
		*mc_bank(fp, K1_MC_PI_RD_CFG) = 0x0000'0480;
	}
	*mc(K1_MC_PI_WL_CFG) = 0x0a30'1e44;
	*mc(K1_MC_PI_WR_CFG) = 0x0000'0f10;
	*mc(0x13f0) = 0x055d'74ff;
	*mc(K1_MC_CS0_MAP) = 0x0010'0001;
	*mc(K1_MC_CS1_MAP) = real_cs == 2 ? 0x0010'0001 : 0;
	phy_defaults(VREF_INIT, false);
	*pmu(K1_PMU_HW_SLP) = K1_HW_SLP_RESET;

	memset(g_model.die_mr14, 0x19, sizeof(g_model.die_mr14));
	g_model.dfc2_seen = boot_pp >= 2;
}

/* both FSPs keep the die default (0) until a MR14 write reaches them */
void model_reset_por(uintptr_t ddr, uint32_t real_cs)
{
	base_reset(ddr, real_cs);
	g_model.in_reset = true;
	phy_defaults(VREF_POR, true);
	io_words_unknown();
	*pmu(K1_PMU_CC_AP) = CC_AP_POR;
	*pmu(K1_PMU_PLL1_CTL1) = K1_PLL1_BOOTROM;
	*pmu(K1_PMU_PLL1_CTL2) = K1_PLL1_BOOTROM_ANALOG;
	*pmu(K1_PMU_CLKSEL) = 0x0007'3340;
}

uint64_t model_now_us(void *ctx)
{
	(void)ctx;
	g_model.time_us += g_model.time_step_us;
	return g_model.time_us;
}

static bool in_mc_phy(uintptr_t a)
{
	return a >= g_model.ddr && a < g_model.ddr + MODEL_SPAN && !(a & 3);
}

static bool in_pmu(uintptr_t a)
{
	return a >= g_model.pmu && a < g_model.pmu + PMU_SPAN && !(a & 3);
}

static bool in_win(uintptr_t a)
{
	return a >= g_model.win && a < g_model.win + MODEL_SPAN && !(a & 3);
}

static uint32_t *mc_phy(uint32_t off)
{
	return off >= K1_PHY_OFF ? phy(off - K1_PHY_OFF) : mc(off);
}

uint32_t model_peek(uintptr_t addr)
{
	if (in_pmu(addr))
		return *pmu((uint32_t)(addr - g_model.pmu));
	return *mc_phy((uint32_t)(addr - g_model.ddr));
}

void model_poke(uintptr_t addr, uint32_t val)
{
	if (in_pmu(addr))
		*pmu((uint32_t)(addr - g_model.pmu)) = val;
	else
		*mc_phy((uint32_t)(addr - g_model.ddr)) = val;
}

uint32_t model_peek_bank(uint32_t fp, uint32_t off)
{
	return *mc_bank(fp, off);
}

static bool mc_clocked(void)
{
	return g_model.swfc_done && g_model.mck_on && !g_model.in_reset;
}

static uint32_t active_rx_step(void)
{
	const uint32_t s = slice_off(g_model.boot_pp);
	const uint32_t v0 = *phy(s + K1_SLICE_VREF0);
	const uint32_t v1 = *phy(s + K1_SLICE_VREF1);
	const uint32_t step = v0 >> 28;

	if ((v0 >> 16) != (v1 >> 16))
		violate("RD with VREF1 0x%08x != VREF0 0x%08x", v1, v0);
	if (v0 & K1_VREF_BIT4)
		violate("RD with VREF0 bit 4 set (0x%08x)", v0);
	if ((v0 & 0xffff'0000) != k1_pack_vref(step))
		violate("VREF0 high half not packed (0x%08x)", v0);
	return step;
}

static void put_harvest(uint32_t base, model_margin_fn fn, uint32_t step,
			uint32_t cs)
{
	for (uint32_t g = 0; g < 8; g++) {
		uint32_t w = 0;

		for (uint32_t k = 0; k < 4; k++)
			w |= (uint32_t)fn(&g_model, step, cs, 4 * g + k) << (8 * k);
		*phy(base + 4 * g) = w;
	}
	/* DM margins are zero: they must never drive the choice */
	*phy(base + K1_PHY_HARVEST_DM) = 0;
}

static void complete_op(void)
{
	const uint32_t pp = g_model.boot_pp;
	const uint32_t cs = g_model.pending_cs;
	const uint32_t cs0 = cs - 1;
	uint32_t *st = mc(K1_MC_PI_STATUS);

	switch (g_model.pending_ev) {
	case EV_OP_WL:
		for (uint32_t b = 0; b < 4; b++)
			*byte_reg(pp, cs0, b, K1_BYTE_WL) = 17 + b * 2 + cs0;
		*st |= K1_PI_DONE_WL;
		break;
	case EV_OP_GATE:
		/* TIMEOUT: done bit 1 only, bit 2 as it was, PHY gate error */
		if (g_model.gate_timeout_cs == cs) {
			*st |= K1_PI_DONE_RGATE;
			*phy(K1_PHY_TRAIN_STAT) |= K1_ST_RGATE;
			break;
		}
		for (uint32_t b = 0; b < 4; b++) {
			const uint32_t g = 35 + b + cs0;

			*byte_reg(pp, cs0, b, K1_BYTE_GATE) = (1u << 16) | (g << 8) | g;
		}
		*st |= 0x6;
		break;
	case EV_OP_RD: {
		const uint32_t step = active_rx_step();

		put_harvest(K1_PHY_HARVEST_RX, g_model.rx_margin, step, cs);
		for (uint32_t b = 0; b < 4; b++)
			for (uint32_t l = 0; l < 8; l++)
				*byte_reg(pp, cs0, b, K1_BYTE_RX_DQ + 4 * l) =
					10 + step + (l & 1);
		if (g_model.arm_rl_in_sweep && step == 15)
			*phy(K1_PHY_TRAIN_STAT) |= K1_ST_RL;
		*st |= K1_PI_DONE_RD;
		break;
	}
	case EV_OP_WR: {
		const uint32_t vdq = g_model.die_mr14[cs0][0];
		const uint32_t idx = vdq >= 21 ? vdq - 21 : 99;

		put_harvest(K1_PHY_HARVEST_TX, g_model.tx_margin, idx, cs);
		for (uint32_t b = 0; b < 4; b++) {
			for (uint32_t l = 0; l < 8; l++)
				*byte_reg(pp, cs0, b, K1_BYTE_TX_DQ + 4 * l) =
					(20 + idx + (l & 1)) & 63;
			*byte_reg(pp, cs0, b, K1_BYTE_TX_DM) = (21 + idx) & 63;
		}
		*phy(K1_PHY_TRAIN_STAT) |= g_model.final_status_bits;
		*st |= K1_PI_DONE_WR;
		break;
	}
	default:
		break;
	}
	g_model.pending_ev = 0;
}

static void set_die_mr14_from_bank(uint32_t fp)
{
	const uint8_t f = (uint8_t)((*mc_bank(fp, K1_MC_TXVREF) >> 16) & 0x3f);

	memset(g_model.die_mr14, f, sizeof(g_model.die_mr14));
}

/* the DFC engine: SR, PLL switch, MR batch from the target bank, SR exit */
static void apply_dfc(void)
{
	const uint32_t fp = g_model.dfc_target;
	uint32_t *freq = &regs[K1_MC_FREQ / 4];

	*freq = (*freq & ~K1_FREQ_FP_MASK) | (fp << 30) | (fp << 28);
	*mc(K1_MC_PI_CFG) = (*mc(K1_MC_PI_CFG) & ~K1_PI_CFG_SEL_MASK) | (fp << 2);
	*mc(K1_MC_STATUS) = init_status();
	*phy(K1_PHY_INIT) = K1_PHY_INIT_VAL;
	g_model.boot_pp = fp;
	if (!g_model.dfc_no_mr14)
		set_die_mr14_from_bank(fp);
	/* measured (doc 08, P-19): fp2 not trained yet, the first MRR reads 0 */
	if (fp == 2 && !g_model.dfc2_seen)
		g_model.mrr_zero_next = true;
	if (fp == 2)
		g_model.dfc2_seen = true;
	g_model.dfc_pre = false;
	if (g_model.n_dfc < 8)
		g_model.dfc_fp[g_model.n_dfc] = fp;
	g_model.n_dfc++;
	record(EV_DFC);
}

static void finish_kick(void)
{
	const uint32_t bits = g_model.kick_bits;

	g_model.kick_bits = 0;
	*pmu(K1_PMU_CC_AP) &= ~bits;
	if (bits == K1_CC_SWFC_KICK) {
		g_model.swfc_done = true;
		record(EV_SWFC);
	} else {
		apply_dfc();
	}
}

static uint32_t pmu_read(uint32_t o)
{
	if (o == K1_PMU_CLKSEL && g_model.lock_left && --g_model.lock_left == 0) {
		*pmu(o) |= K1_CLKSEL_LOCK;
		g_model.locked = true;
		record(EV_PLL_LOCK);
	}
	if (o == K1_PMU_CC_AP && g_model.kick_bits) {
		if (g_model.kick_left)
			g_model.kick_left--;
		else
			finish_kick();
	}
	return *pmu(o);
}

static bool pll_programmed(void)
{
	const uint32_t p1 = *pmu(K1_PMU_PLL1_CTL1);

	const bool pll1_ok = (p1 == K1_PLL1_2666 &&
			      *pmu(K1_PMU_PLL1_CTL2) == K1_PLL1_ANALOG) ||
			     ((p1 & ~K1_PLL1_2400_KEEP) ==
			      (K1_PLL1_2400_BYTE << 8));

	return pll1_ok &&
	       *pmu(K1_PMU_PLL2_CTL1) == K1_PLL2_VCO &&
	       *pmu(K1_PMU_PLL2_CTL2) == K1_PLL2_ANALOG;
}

/* doc 30 snapshot layout, the oracle for each line as it is written */
static const uint16_t snap_globals[22] = {
	0x048, 0x054, 0x058, 0x060, 0x064, 0x148, 0x14c, 0x200, 0x204, 0x208,
	0x20c, 0x220, 0x224, 0x2c4, 0x2c0, 0x380, 0x384, 0x388, 0x080, 0xa00,
	0xac0, 0xacc,
};
static const uint16_t snap_timings[24] = {
	0x300, 0x30c, 0x310, 0x314, 0x38c, 0x390, 0x394, 0x398, 0x39c, 0x3a0,
	0x3a4, 0x3a8, 0x3ac, 0x3b0, 0x3b4, 0x3b8, 0x3bc, 0x3c0, 0x3c4, 0x400,
	0x3d8, 0x34c, 0x13e4, 0x13ec,
};
static const uint32_t snap_tail[15][2] = {
	{ 0x0002'0200, 0x13e0 }, { 0x1300'0010, 0x13d0 }, { 0x0001'0000, 0x33fc },
	{ 0x0001'0000, 0x33fc }, { 0x1300'0008, 0x0020 }, { 0x1300'0004, 0x0020 },
	{ 0x1302'0000, 0x0028 }, { 0x1300'0001, 0x13d0 }, { 0x0000'8000, 0x33fc },
	{ 0x0000'8000, 0x33fc }, { 0x1000'0100, 0x13d0 }, { 0x0000'8000, 0x33fc },
	{ 0x0000'8000, 0x33fc }, { 0x1302'000d, 0x0024 }, { 0x1302'0003, 0x0024 },
};
static constexpr uint32_t ADDR_CTL0_REQ = 0x1'0044;
static constexpr uint32_t ADDR_CTL0_EOP = 0x2'0044;

static bool snap_ok(uint32_t idx, uint32_t data, uint32_t addr)
{
	const uint32_t live = regs[K1_MC_FREQ / 4];
	const uint32_t ctl0 = *mc(K1_MC_CTL0);

	if (idx == 0x200)
		return addr == ADDR_CTL0_REQ && data == (ctl0 | 0x1004);
	if (idx <= 0x216)
		return addr == snap_globals[idx - 0x201] && data == *mc(addr);
	if (idx < 0x27b) {
		const uint32_t n = (idx - 0x217) / 25, k = (idx - 0x217) % 25;

		/* read with the live bank selected the Device_Init way */
		if (live >> 28 != ((n << 2) | n))
			return false;
		if (k == 0)
			return addr == K1_MC_FREQ && data == (live & ~0x3000'0000u);
		return addr == snap_timings[k - 1] && data == *mc(addr);
	}
	if (idx < 0x28a) {
		const uint32_t d = snap_tail[idx - 0x27b][0];
		const bool one_rank = g_model.real_cs == 1 && d >> 24 == 0x13 &&
				      data == ((d & 0x00ff'ffff) | 0x1100'0000);

		return addr == snap_tail[idx - 0x27b][1] && (data == d || one_rank);
	}
	return idx == 0x28a && addr == ADDR_CTL0_EOP && data == ctl0;
}

/* DATA, ADDR, then INDEX commits the line */
static void tbl_port_write(uint32_t off, uint32_t v)
{
	if (off == K1_MC_TBL_DATA) {
		g_model.tbl_data = v;
	} else if (off == K1_MC_TBL_ADDR) {
		g_model.tbl_addr = v;
	} else if (v >= sizeof(g_model.tbl) / sizeof(g_model.tbl[0])) {
		violate("DFC table index 0x%x", v);
	} else {
		g_model.tbl[v] = (struct model_tbl){
			.data = g_model.tbl_data,
			.addr = g_model.tbl_addr,
			.set = true,
			.ok = v < 0x200 || snap_ok(v, g_model.tbl_data, g_model.tbl_addr),
		};
		g_model.tbl_writes++;
	}
}

static bool is_mark(uint32_t phy_off)
{
	return phy_off == K1_PHY_MARK_DATA || phy_off == K1_PHY_MARK_ADDR ||
	       phy_off == K1_PHY_MARK_CMD;
}

static void mark_write(uint32_t phy_off, uint32_t v)
{
	if (phy_off == K1_PHY_MARK_DATA) {
		g_model.mark_data = v;
	} else if (phy_off == K1_PHY_MARK_ADDR) {
		g_model.mark_addr = v;
	} else if (g_model.n_marks < 8) {
		uint32_t *m = g_model.marks[g_model.n_marks++];

		m[0] = g_model.mark_data;
		m[1] = g_model.mark_addr;
		m[2] = v;
	}
}

static bool eop(uint32_t idx)
{
	return g_model.tbl[idx].set && g_model.tbl[idx].addr == ADDR_CTL0_EOP;
}

/* without every region the engine parks the ranks and never asks the PMU */
static bool table_complete(void)
{
	const struct model_tbl *t = g_model.tbl;
	const char *why = nullptr;
	uint32_t at = 0;

	if (!t[0].set || !(eop(0x1b) || eop(0x1c)) || !eop(0x12e))
		why = "command flow";
	else if (!t[0x180].set || !(eop(0x18e) || eop(0x18f)))
		why = "exit flow";
	for (at = K1_TBL_SNAPSHOT; !why && at <= K1_TBL_SNAP_END; at++)
		if (!t[at].set || !t[at].ok)
			why = "snapshot line";
	if (!why && g_model.n_marks != 3)
		why = "analog markers";
	if (why)
		snprintf(g_model.tbl_why, sizeof(g_model.tbl_why), "%s 0x%x",
			 why, at - 1);
	return !why;
}

static void start_kick(uint32_t kick)
{
	const uint32_t slp = *pmu(K1_PMU_HW_SLP);
	const uint32_t clk = *pmu(K1_PMU_CLKSEL) & 0xffff;

	if (kick == K1_CC_SWFC_KICK) {
		if (!g_model.locked)
			violate("SW-FC kick before PLL lock");
		if (slp != K1_HW_SLP_SWFC)
			violate("SW-FC with HW_SLP 0x%08x", slp);
		if (clk != 0x3b50)
			violate("SW-FC with clksel 0x%04x", clk);
		if (g_model.swfc_stuck)
			return;
	} else {
		const uint32_t fp = (slp >> K1_HW_SLP_FP_SHIFT) & 3;
		static const uint32_t clksel[4] = { 0x3b50, 0x3b04, 0x3b40, 0x3b00 };

		const uint32_t parked = g_model.real_cs == 2 ? 0x44 : 0x04;

		if (!mc_clocked() || !g_model.dram_ready)
			violate("HW-DFC before the DRAM is up");
		if (g_model.in_sr || (*mc(K1_MC_STATUS) & parked))
			violate("HW-DFC with STATUS 0x%02x: ranks in self-refresh",
				*mc(K1_MC_STATUS));
		if (!g_model.dfc_pre)
			violate("HW-DFC without ddr+0x148 = 0x80ac0000");
		if (!(*pmu(K1_PMU_AP_IMR) & K1_IMR_DCLK_FC))
			violate("HW-DFC with the DCLK_FC IRQ unmasked");
		if ((slp & ~(3u << K1_HW_SLP_FP_SHIFT)) != K1_HW_SLP_DFC)
			violate("HW-DFC with HW_SLP 0x%08x", slp);
		if (clk != clksel[fp])
			violate("HW-DFC to fp%u with clksel 0x%04x", fp, clk);
		if (g_model.dfc_stuck) {
			*mc(K1_MC_STATUS) |= parked;
			return;
		}
		if (!table_complete()) {
			/* measured on silicon: SR entered, CTL_FREQ untouched */
			*mc(K1_MC_STATUS) |= parked;
			g_model.dfc_hung = true;
			return;
		}
		g_model.dfc_target = fp;
	}
	g_model.kick_bits = kick;
	g_model.kick_left = g_model.pmu_delay;
}

static void mc_reset_effects(void)
{
	memset(regs, 0, K1_PHY_OFF);
	memset(g_model.banked, 0, sizeof(g_model.banked));
	memset(g_model.tbl, 0, sizeof(g_model.tbl));
	g_model.tbl_writes = g_model.n_marks = 0;
	if (g_model.dfc_hung)
		*pmu(K1_PMU_CC_AP) &= ~K1_CC_DFC_KICK;
	g_model.dfc_hung = false;
	io_words_unknown();
	*mc(K1_MC_PI_STATUS) = PI_IDLE;
	phy_defaults(VREF_POR, true);
	g_model.dfi_started = g_model.dfi_cleared = g_model.dram_ready = false;
	g_model.boot_pp = 0;
}

static void write_hw_slp(uint32_t old, uint32_t v)
{
	const bool was = old & K1_HW_SLP_RESET;
	const bool now = v & K1_HW_SLP_RESET;

	if (was && !now) {
		g_model.in_reset = true;
		g_model.reset_pulse = true;
		g_model.reset_at = g_model.time_us;
		g_model.mc_reset_count++;
		record(EV_MC_RESET);
	} else if (!was && now) {
		if (g_model.reset_pulse) {
			if (g_model.time_us - g_model.reset_at < 100)
				violate("MC reset held %llu us",
					(unsigned long long)(g_model.time_us -
							     g_model.reset_at));
			mc_reset_effects();
			g_model.after_reset = true;
			g_model.release_at = g_model.time_us;
		}
		g_model.reset_pulse = false;
		g_model.in_reset = false;
	}
}

static void pmu_write(uint32_t o, uint32_t v)
{
	const uint32_t old = *pmu(o);

	if (g_model.strict) {
		violate("training wrote PMU +0x%x", o);
		*pmu(o) = v;
		return;
	}
	switch (o) {
	case K1_PMU_CLKSEL: {
		const uint32_t en = K1_CLKSEL_PLL1_EN | K1_CLKSEL_PLL2_EN;
		uint32_t nv = (v & ~K1_CLKSEL_LOCK) | (old & K1_CLKSEL_LOCK);

		if ((v & en) != en) {
			nv &= ~K1_CLKSEL_LOCK;
			g_model.locked = false;
			g_model.lock_left = 0;
		} else if (!g_model.locked && !g_model.lock_left) {
			if (!(v & K1_CLKSEL_EXTRA_EN))
				violate("PLL enable without bit 11");
			else if (!pll_programmed())
				violate("PLL enabled before its VCO words");
			else if (!g_model.pll_never_locks)
				g_model.lock_left = g_model.pmu_delay + 1;
		}
		*pmu(o) = nv;
		return;
	}
	case K1_PMU_CC_AP: {
		const uint32_t kick = v & (K1_CC_SWFC_KICK | K1_CC_DFC_KICK);

		if (g_model.kick_bits)
			violate("CC_AP written while a change is running");
		/* doc 30: writing +0x04 does not release a held bit 22 */
		if ((g_model.dfc_hung || g_model.dfc_stuck) &&
		    (old & K1_CC_DFC_KICK))
			violate("CC_AP written with HW-DFC bit 22 held");
		if (kick == (K1_CC_SWFC_KICK | K1_CC_DFC_KICK))
			violate("both kicks at once");
		/* doc 30: PMU+0x04 = (1 << 22); silicon got stuck with the POR bits OR'd in */
		if (kick == K1_CC_DFC_KICK && v != K1_CC_DFC_KICK)
			violate("HW-DFC kick 0x%08x is not (1 << 22)", v);
		*pmu(o) = v;
		if (kick == K1_CC_SWFC_KICK || kick == K1_CC_DFC_KICK)
			start_kick(kick);
		return;
	}
	case K1_PMU_HW_SLP:
		write_hw_slp(old, v);
		break;
	case K1_PMU_MCK_CTRL:
		g_model.mck_on = (v & K1_MCK_ON) == K1_MCK_ON;
		if (g_model.mck_on && !g_model.swfc_done)
			violate("MCK on before the SW-FC");
		break;
	case K1_PMU_PLL1_CTL1:
	case K1_PMU_PLL1_CTL2:
	case K1_PMU_PLL2_CTL1:
	case K1_PMU_PLL2_CTL2:
		if (g_model.locked)
			violate("PLL word +0x%x rewritten while locked", o);
		break;
	case K1_PMU_AP_IMR:
	case K1_PMU_PHY_LDO:
	case K1_PMU_PHY_BIAS:
		break;
	default:
		violate("unexpected PMU write at +0x%x", o);
		break;
	}
	*pmu(o) = v;
}

uint32_t k1_mock_read32(uintptr_t addr)
{
	if (in_pmu(addr))
		return pmu_read((uint32_t)(addr - g_model.pmu));
	if (in_win(addr) && g_model.win_alias) {
		addr = addr - g_model.win + g_model.ddr;
	} else if (in_win(addr)) {
		/* a separate window reads 0 where the MC has CTL0 */
		if (!mc_clocked() || addr - g_model.win != K1_MC_CTL0)
			violate("read of DFC window +0x%lx",
				(unsigned long)(addr - g_model.win));
		return 0;
	}
	if (!in_mc_phy(addr)) {
		violate("read outside MC/PHY at 0x%lx", (unsigned long)addr);
		return 0;
	}
	if (!mc_clocked()) {
		violate("read of ddr+0x%lx without MC clock",
			(unsigned long)(addr - g_model.ddr));
		return 0;
	}
	if (addr == g_model.ddr + K1_MC_PI_STATUS && g_model.stale_left) {
		g_model.stale_left--;
		return g_model.stale_status;
	}
	if (addr == g_model.ddr + K1_MC_PI_STATUS && g_model.pending_ev &&
	    !g_model.hung) {
		if (g_model.countdown == 0)
			complete_op();
		else
			g_model.countdown--;
	}
	if (addr == g_model.ddr + K1_MC_FREQ)
		return *mc(K1_MC_FREQ) | g_model.freq_read_or;
	return *mc_phy((uint32_t)(addr - g_model.ddr));
}

static void write_pi_cmd(uint32_t v)
{
	const uint32_t cs = (v >> K1_PI_CS_SHIFT) & 0xf;
	const uint32_t op = v & ~(0xfu << K1_PI_CS_SHIFT);
	enum model_ev ev;
	uint32_t clear;

	*mc(K1_MC_PI_CMD) = 0;
	if (!g_model.in_sr && !g_model.strict) {
		if (op == K1_DFI_INIT_START || op == K1_DFI_INIT_CLEAR) {
			if (cs != expected_sel())
				violate("DFI command 0x%08x with cs 0x%x", v, cs);
			if (op == K1_DFI_INIT_CLEAR) {
				if (!g_model.dfi_started)
					violate("init_start cleared before set");
				g_model.dfi_cleared = true;
				return;
			}
			for (uint32_t pp = 0; pp <= K1_MAX_PP; pp++)
				if (!g_model.com_armed[pp][0] ||
				    !g_model.com_armed[pp][1])
					violate("init_start before COMMON of slice %u is armed",
						pp);
			g_model.dfi_started = true;
			g_model.dfi_cleared = false;
			if (!g_model.dfi_stuck)
				*mc(K1_MC_PI_STATUS) |= PI_IDLE;
			record(EV_DFI_INIT);
			return;
		}
	}
	if (!g_model.in_sr)
		violate("PI command 0x%08x outside self-refresh", v);
	if (g_model.pending_ev)
		violate("PI command 0x%08x while an op is still running", v);
	if (cs < 1 || cs > g_model.real_cs)
		violate("PI command 0x%08x with cs field %u", v, cs);

	switch (op) {
	case K1_PI_OP_WL:    ev = EV_OP_WL;   clear = K1_PI_DONE_WL;    break;
	case K1_PI_OP_RGATE: ev = EV_OP_GATE; clear = K1_PI_DONE_RGATE; break;
	case K1_PI_OP_RD:    ev = EV_OP_RD;   clear = K1_PI_DONE_RD;    break;
	case K1_PI_OP_WR:    ev = EV_OP_WR;   clear = K1_PI_DONE_WR;    break;
	default:
		violate("unexpected PI op 0x%08x", v);
		return;
	}

	record(ev);
	if (cs <= 2)
		g_model.op_cs_count[ev][cs]++;
	g_model.stale_status = *mc(K1_MC_PI_STATUS);
	g_model.stale_left = g_model.clear_delay;
	*mc(K1_MC_PI_STATUS) &= ~clear;
	g_model.pending_ev = ev;
	g_model.pending_cs = cs;
	g_model.countdown = g_model.latency;
	if (g_model.hang_op == ev)
		g_model.hung = true;
}

static void write_sr(uint32_t v)
{
	const uint32_t mode = (v >> K1_SR_MODE_SHIFT) & 0xf;
	const uint32_t dir = v & 0xc0;
	uint32_t *stat = mc(K1_MC_STATUS);

	if ((v & ~((0xfu << K1_SR_MODE_SHIFT) | 0xc0u)) != K1_SR_CMD ||
	    (mode != 1 && mode != 3)) {
		violate("malformed USER_0 command 0x%08x", v);
		return;
	}
	if (dir == K1_SR_ENTER) {
		const uint32_t fp = regs[K1_MC_FREQ / 4] >> K1_FREQ_PP_SHIFT;

		if (!g_model.dram_ready)
			violate("SR entry before DRAM init");
		if (!g_model.strict &&
		    (fp != g_model.boot_pp ||
		     ((*mc(K1_MC_PI_CFG) >> 2) & 3) != fp))
			violate("SR entry with CTL_FREQ 0x%08x PI_CFG 0x%x at fp%u",
				regs[K1_MC_FREQ / 4], *mc(K1_MC_PI_CFG),
				g_model.boot_pp);
		g_model.sr_enter_cmd = v;
		if (!g_model.sr_enter_stuck)
			*stat |= mode == 3 ? 0x44 : 0x04;
		g_model.in_sr = true;
		record(EV_SR_ENTER);
	} else if (dir == K1_SR_EXIT) {
		g_model.sr_exit_cmd = v;
		*stat &= ~(mode == 3 ? 0x66u : 0x06u);
		g_model.in_sr = false;
		record(EV_SR_EXIT);
	} else {
		violate("SR command 0x%08x neither enter nor exit", v);
	}
}

static void write_user0(uint32_t v)
{
	const uint32_t cmd = v & 0xf0ff'ffff;
	const uint32_t cs = (v >> K1_CS_SEL_SHIFT) & 0xf;

	if (g_model.in_sr || g_model.strict ||
	    (cmd != K1_USER0_INIT && cmd != K1_USER0_ZQ_START &&
	     cmd != K1_USER0_ZQ_LATCH)) {
		write_sr(v);
		return;
	}
	if (cmd == K1_USER0_INIT) {
		if (cs != expected_sel())
			violate("DRAM init 0x%08x with cs 0x%x", v, cs);
		if (!g_model.dfi_started || !g_model.dfi_cleared ||
		    !(*mc(K1_MC_PI_STATUS) & K1_PI_INIT_DONE))
			violate("DRAM init before the DFI init_start handshake");
		if (!g_model.dram_init_stuck) {
			*mc(K1_MC_STATUS) |= init_status();
			g_model.dram_ready = true;
		}
		record(EV_DRAM_INIT);
		return;
	}
	if (!g_model.dram_ready)
		violate("ZQCAL before DRAM init");
	if (cs != 1 && cs != 2)
		violate("ZQCAL 0x%08x not for a single rank", v);
	if (cmd == K1_USER0_ZQ_START) {
		g_model.zq_cs = cs;
		g_model.zq_start_us = g_model.time_us;
	} else {
		if (g_model.zq_cs != cs)
			violate("ZQ latch 0x%08x without its start", v);
		if (g_model.time_us - g_model.zq_start_us < 2)
			violate("ZQ latch %llu us after start",
				(unsigned long long)(g_model.time_us -
						     g_model.zq_start_us));
		g_model.zq_cs = 0;
		g_model.zq_latch_us = g_model.time_us;
	}
	if (g_model.n_zq < 16)
		g_model.zq_cmd[g_model.n_zq] = v;
	g_model.n_zq++;
	record(EV_ZQ);
}

static void write_ca(uint32_t v)
{
	if (v == K1_CA_OP_TXVREF && g_model.in_sr) {
		set_die_mr14_from_bank(regs[K1_MC_FREQ / 4] >> K1_FREQ_PP_SHIFT);
		record(EV_MR14_SET);
		return;
	}
	if ((v & 0xf0f3'ff00) == K1_CA_MR_READ) {
		const uint32_t cs = ((v >> 24) & 0xf) - 1;
		const uint32_t ch = (v >> 18) & 3;
		const uint32_t mr = v & 0xff;

		if (g_model.in_sr)
			violate("MR%u read inside self-refresh", mr);
		if (!g_model.dram_ready)
			violate("MR%u read before DRAM init", mr);
		if (cs > 1 || ch > 1) {
			violate("MR read 0x%08x with bad cs/ch", v);
			return;
		}
		/* doc 28: MR13 always reads 0 on this SoC */
		uint8_t val = mr == 14 ? g_model.die_mr14[cs][ch] :
			      mr == 13 ? 0 : g_model.mr[mr];

		if (g_model.mrr_zero_next) {
			g_model.mrr_zero_next = false;
			val = 0;
		}
		*mc(K1_MC_MR_DATA) = K1_MR_DATA_VALID | val;
		if (mr == 14 && g_model.n_mr14_read < 64)
			g_model.mr14_read[g_model.n_mr14_read++] = (typeof(g_model.mr14_read[0])){
				.val = val,
				.n_dfc = (uint8_t)g_model.n_dfc,
				.n_sr = (uint8_t)g_model.ev_count[EV_SR_ENTER],
			};
		record(EV_MR_READ);
		return;
	}
	if ((v & 0xf0ff'0000) == K1_CA_MR_WRITE && !g_model.in_sr &&
	    !g_model.strict) {
		const uint32_t mr = v & 0xff;
		const uint32_t live = regs[K1_MC_FREQ / 4] >> K1_FREQ_PP_SHIFT;

		if (!g_model.dram_ready)
			violate("MR%u write before DRAM init", mr);
		if (((v >> K1_CS_SEL_SHIFT) & 0xf) != expected_sel())
			violate("MR write 0x%08x with cs 0x%x", v,
				(v >> K1_CS_SEL_SHIFT) & 0xf);
		if (g_model.n_mr && g_model.time_us - g_model.last_mrw_us < 1)
			violate("MR writes 0x%08x and 0x%08x back to back",
				g_model.mr_cmd[(g_model.n_mr - 1) % 64], v);
		if (g_model.zq_latch_us && g_model.time_us - g_model.zq_latch_us < 1)
			violate("MR write right after ZQ latch");
		if (mr == 14)
			set_die_mr14_from_bank(live);
		g_model.last_mrw_us = g_model.time_us;
		g_model.mr_cmd[g_model.n_mr % 64] = v;
		g_model.n_mr++;
		record(EV_MR_WRITE);
		return;
	}
	violate("unexpected CA command 0x%08x", v);
}

static void write_phy(uint32_t off, uint32_t v)
{
	const uint32_t s = slice_off(g_model.boot_pp);

	if (off == K1_PHY_INIT) {
		if (!g_model.in_sr)
			violate("PHY_INIT outside self-refresh");
		if (v != K1_PHY_INIT_VAL)
			violate("PHY_INIT written with %u", v);
		record(EV_PHY_INIT);
	} else if (off == K1_PHY_TRAIN_STAT) {
		if (v != (*phy(off) & ~K1_ST_RL))
			violate("PHY status write 0x%x is not a bit-2 clear", v);
	} else if (g_model.in_sr || g_model.strict) {
		if (off == s + K1_SLICE_VREF0 || off == s + K1_SLICE_VREF1) {
			const uint32_t lo = v & 0xffff;

			if (lo != 0xa927 && lo != 0xa937)
				violate("Vref low half changed to 0x%04x", lo);
		} else {
			violate("unexpected PHY write at +0x%x = 0x%08x", off, v);
		}
	} else if (off < K1_PHY_OTHER && (off & 0x3dff) == K1_SLICE_COMMON0) {
		const uint32_t pp = off >> K1_PHY_SLICE_SHIFT;
		const uint32_t c = (off >> 9) & 1;

		if (v == 0)
			g_model.com_zeroed[pp][c] = true;
		g_model.com_armed[pp][c] = v == 1 && g_model.com_zeroed[pp][c];
	} else if (is_mark(off) && g_model.win_alias) {
		mark_write(off, v);
	}
	*phy(off) = v;
}

/* MC writes of the training pass, or any MC write when strict */
static void write_mc_training(uint32_t off, uint32_t v)
{
	switch (off) {
	case K1_MC_PI_CMD:
		write_pi_cmd(v);
		return;
	case K1_MC_USER0:
		write_user0(v);
		break;
	case K1_MC_CA_CMD:
		write_ca(v);
		break;
	case K1_MC_PI_WL_CFG:
		record(EV_CFG_WL);
		break;
	case K1_MC_PI_RD_CFG:
		record(EV_CFG_RD);
		break;
	case K1_MC_PI_WR_CFG:
		record(EV_CFG_WR);
		break;
	case K1_MC_TXVREF:
		if ((v & K1_TXVREF_MASK) != (*mc(off) & K1_TXVREF_MASK))
			violate("TXVREF bits outside [21:16] changed: 0x%08x", v);
		break;
	default:
		violate("training wrote MC +0x%x = 0x%08x", off, v);
		break;
	}
	*mc(off) = v;
}

static void write_mc_bringup(uint32_t off, uint32_t v)
{
	switch (off) {
	case K1_MC_PI_CMD:
		write_pi_cmd(v);
		return;
	case K1_MC_USER0:
		write_user0(v);
		break;
	case K1_MC_CA_CMD:
		write_ca(v);
		break;
	case K1_MC_TBL_DATA:
	case K1_MC_TBL_ADDR:
	case K1_MC_TBL_INDEX:
		/* doc 30: this port reaches the table RAM only as an alias */
		if (g_model.win_alias)
			tbl_port_write(off, v);
		break;
	case K1_MC_DFC_PRE:
		g_model.dfc_pre = v == K1_DFC_PRE_VAL;
		break;
	default:
		break;
	}
	*mc(off) = v;
}

void k1_mock_write32(uintptr_t addr, uint32_t v)
{
	uint32_t off;

	g_model.mmio_writes++;
	if (in_pmu(addr)) {
		pmu_write((uint32_t)(addr - g_model.pmu), v);
		return;
	}
	if (in_win(addr) && g_model.win_alias) {
		addr = addr - g_model.win + g_model.ddr;
	} else if (in_win(addr)) {
		off = (uint32_t)(addr - g_model.win);
		g_model.mc_writes++;
		if (!mc_clocked() || g_model.in_sr || g_model.strict)
			violate("DFC window +0x%x written outside the bring-up", off);
		else if (off == K1_MC_TBL_DATA || off == K1_MC_TBL_ADDR ||
			 off == K1_MC_TBL_INDEX)
			tbl_port_write(off, v);
		else if (off >= K1_PHY_OFF && is_mark(off - K1_PHY_OFF))
			mark_write(off - K1_PHY_OFF, v);
		else
			violate("DFC window +0x%x = 0x%08x", off, v);
		return;
	}
	if (!in_mc_phy(addr)) {
		violate("write outside MC/PHY at 0x%lx", (unsigned long)addr);
		return;
	}

	off = (uint32_t)(addr - g_model.ddr);
	g_model.mc_writes++;
	if (!mc_clocked()) {
		violate("write of ddr+0x%x without MC clock", off);
		return;
	}
	if (g_model.after_reset) {
		if (g_model.time_us - g_model.release_at < 100)
			violate("MC written %llu us after reset release",
				(unsigned long long)(g_model.time_us -
						     g_model.release_at));
		g_model.after_reset = false;
	}
	if (off >= K1_PHY_OFF)
		write_phy(off - K1_PHY_OFF, v);
	else if (g_model.strict || g_model.in_sr)
		write_mc_training(off, v);
	else
		write_mc_bringup(off, v);
}
