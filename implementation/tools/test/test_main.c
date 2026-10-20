// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Thalles Campagnani
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "check.h"
#include "k1_ddr_boot.h"
#include "k1_ddrt.h"
#include "model.h"
#include "../lib/k1_ddrt_regs.h"

int g_checks, g_fails;
bool g_log;

static char g_logbuf[1 << 16];
static size_t g_loglen;

void test_vlog(void *ctx, const char *fmt, va_list ap)
{
	va_list cp;

	(void)ctx;
	va_copy(cp, ap);
	if (g_loglen < sizeof(g_logbuf) - 1) {
		const int n = vsnprintf(g_logbuf + g_loglen,
					sizeof(g_logbuf) - g_loglen, fmt, cp);

		if (n > 0)
			g_loglen += (size_t)n;
		if (g_loglen > sizeof(g_logbuf) - 1)
			g_loglen = sizeof(g_logbuf) - 1;
	}
	va_end(cp);
	if (g_log)
		vprintf(fmt, ap);
}

void log_clear(void)
{
	g_loglen = 0;
	g_logbuf[0] = '\0';
}

bool log_has(const char *s)
{
	return strstr(g_logbuf, s) != nullptr;
}

struct k1_ddrt_env env_timed(void)
{
	return (struct k1_ddrt_env){
		.now_us = model_now_us,
		.vlog = test_vlog,
	};
}

static int run_pass(uint32_t cs_num, struct k1_ddrt_result *res)
{
	const struct k1_ddrt_cfg cfg = {
		.ddr = g_model.ddr,
		.cs_num = cs_num,
		.boot_pp = g_model.boot_pp,
	};
	const struct k1_ddrt_env env = env_timed();

	return k1_ddrt_train_pass(&cfg, &env, res);
}

static uintptr_t slice_reg(uint32_t off)
{
	return g_model.ddr + K1_PHY_OFF + (g_model.boot_pp << K1_PHY_SLICE_SHIFT) +
	       off;
}

static int phase_of(enum model_ev ev)
{
	switch (ev) {
	case EV_CFG_WL: case EV_OP_WL:			return 0;
	case EV_OP_GATE:				return 1;
	case EV_CFG_RD: case EV_OP_RD:			return 2;
	case EV_CFG_WR: case EV_MR14_SET: case EV_OP_WR: return 3;
	case EV_SR_EXIT:				return 4;
	default:					return -1;
	}
}

static int first_index(enum model_ev ev)
{
	for (uint32_t i = 0; i < g_model.n_trace; i++)
		if (g_model.trace[i] == ev)
			return (int)i;
	return -1;
}

static void check_order(void)
{
	int phase = 0;

	CHECK(g_model.n_trace >= 3);
	CHECK_EQ(g_model.trace[0], EV_SR_ENTER);
	CHECK_EQ(g_model.trace[1], EV_PHY_INIT);
	CHECK_EQ(g_model.trace[g_model.n_trace - 1], EV_SR_EXIT);
	for (uint32_t i = 2; i < g_model.n_trace; i++) {
		const int p = phase_of(g_model.trace[i]);

		CHECK(p >= phase);
		if (p > phase)
			phase = p;
	}
	CHECK(first_index(EV_CFG_WL) < first_index(EV_OP_WL));
	CHECK(first_index(EV_CFG_RD) < first_index(EV_OP_RD));
	CHECK(first_index(EV_CFG_WR) < first_index(EV_OP_WR));
	CHECK(first_index(EV_MR14_SET) < first_index(EV_OP_WR));
	CHECK_EQ(g_model.ev_count[EV_CFG_WL], 1);
	CHECK_EQ(g_model.ev_count[EV_CFG_RD], 1);
	CHECK_EQ(g_model.ev_count[EV_CFG_WR], 1);
	CHECK_EQ(g_model.ev_count[EV_MR14_SET], 17);
}

/* brute-force reference of the max-min rule over the model's eye */
static uint32_t expected_best(model_margin_fn fn, uint32_t cs_num)
{
	uint8_t mins[16];

	for (uint32_t s = 0; s < 16; s++) {
		uint8_t m = 255;

		for (uint32_t cs = 1; cs <= cs_num; cs++)
			for (uint32_t dq = 0; dq < 32; dq++)
				if (fn(&g_model, s, cs, dq) < m)
					m = fn(&g_model, s, cs, dq);
		mins[s] = m;
	}
	uint32_t best = 0;
	for (uint32_t s = 1; s < 16; s++)
		if (mins[s] > mins[best])
			best = s;
	return best;
}

static void test_happy_2cs(void)
{
	struct k1_ddrt_result r;
	const struct k1_ddrt_cfg cfg = { .ddr = DDR, .cs_num = 2, .boot_pp = 2 };
	const struct k1_ddrt_env env = env_timed();

	model_reset(DDR, 2, 2);
	const uint32_t rx = expected_best(model_default_rx_margin, 2);
	const uint32_t tx = expected_best(model_default_tx_margin, 2);

	CHECK_EQ(rx, 5);
	CHECK_EQ(tx, 7);
	CHECK_EQ(run_pass(2, &r), K1_DDRT_OK);
	CHECK_CLEAN();
	CHECK_EQ(r.rc, K1_DDRT_OK);
	CHECK_EQ(r.rx_vref_idx, rx);
	CHECK_EQ(r.tx_vref_dq, 21 + tx);
	CHECK_EQ(r.phy_status, 0);

	CHECK_EQ(model_peek(slice_reg(K1_SLICE_VREF0)), k1_pack_vref(rx) | 0xa937);
	CHECK_EQ(model_peek(slice_reg(K1_SLICE_VREF1)), k1_pack_vref(rx) | 0xa927);
	CHECK_EQ(model_peek(DDR + K1_MC_PI_WL_CFG), 0x0a64'1e44);
	CHECK_EQ(model_peek(DDR + K1_MC_PI_RD_CFG), 0x5070);
	CHECK_EQ(model_peek(DDR + K1_MC_PI_WR_CFG), 0x1810);
	CHECK_EQ(model_peek(DDR + K1_MC_PI_CFG), 2 << 2);
	CHECK_EQ(model_peek(DDR + K1_MC_TXVREF), 0x8000'1234 | ((21 + tx) << 16));
	CHECK_EQ(g_model.die_mr14[1][1], 21 + tx);

	CHECK_EQ(g_model.sr_enter_cmd, 0x1300'0040);
	CHECK_EQ(g_model.sr_exit_cmd, 0x1300'0080);
	CHECK_EQ(model_peek(DDR + K1_MC_STATUS), 0x11);
	CHECK(!g_model.in_sr);

	for (uint32_t cs = 1; cs <= 2; cs++) {
		CHECK_EQ(g_model.op_cs_count[EV_OP_WL][cs], 1);
		CHECK_EQ(g_model.op_cs_count[EV_OP_GATE][cs], 1);
		CHECK_EQ(g_model.op_cs_count[EV_OP_RD][cs], 17);
		CHECK_EQ(g_model.op_cs_count[EV_OP_WR][cs], 17);
	}
	check_order();

	/* the re-kick leaves the winner's taps, not step 15's */
	CHECK_EQ(r.rx_dq[1][3][0], 10 + rx);
	CHECK_EQ(r.rx_dq[0][0][1], 11 + rx);
	CHECK_EQ(r.tx_dq[1][2][0], 20 + tx);
	CHECK_EQ(r.tx_dm[0][1], 21 + tx);
	CHECK_EQ(r.gate[0][0], 0x0001'2323);
	CHECK_EQ(r.gate[1][3], 0x0001'2727);
	CHECK_EQ(r.wl[0][1], 19);
	CHECK_EQ(r.wl[1][1], 20);
	CHECK_EQ(r.vref0, k1_pack_vref(rx) | 0xa937);

	CHECK_EQ(k1_ddrt_check_mr14(&cfg, &env), K1_DDRT_OK);
	g_model.die_mr14[1][1] = 0x10;
	CHECK_EQ(k1_ddrt_check_mr14(&cfg, &env), K1_DDRT_EIO);
	CHECK_CLEAN();
}

static void test_happy_1cs(void)
{
	struct k1_ddrt_result r;

	model_reset(DDR, 0, 1);
	CHECK_EQ(run_pass(1, &r), K1_DDRT_OK);
	CHECK_CLEAN();
	CHECK_EQ(g_model.sr_enter_cmd, 0x1100'0040);
	CHECK_EQ(g_model.sr_exit_cmd, 0x1100'0080);
	CHECK_EQ(model_peek(DDR + K1_MC_STATUS), 0x31);
	CHECK_EQ(g_model.op_cs_count[EV_OP_RD][1], 17);
	CHECK_EQ(g_model.op_cs_count[EV_OP_RD][2], 0);
	CHECK_EQ(r.rx_vref_idx, expected_best(model_default_rx_margin, 1));
	CHECK_EQ(r.tx_vref_dq, 21 + expected_best(model_default_tx_margin, 1));
	check_order();
}

static void test_leftover_status(void)
{
	struct k1_ddrt_result r;

	model_reset(DDR, 1, 2);
	model_poke(DDR + K1_MC_PI_STATUS, 0x8000'8067);
	g_model.latency = 20;
	CHECK_EQ(run_pass(2, &r), K1_DDRT_OK);
	CHECK_CLEAN();
	CHECK_EQ(r.rx_vref_idx, 5);
	CHECK_EQ(r.tx_vref_dq, 28);
	CHECK_EQ(model_peek(DDR + K1_MC_PI_STATUS), 0x8000'8067);
}

/* per-stage PI counters, and the stale-read detector they carry */
static void test_pi_stats(void)
{
	struct k1_ddrt_result r;

	model_reset(DDR, 2, 2);
	model_poke(DDR + K1_MC_PI_STATUS, 0x8000'8067);
	g_model.latency = 20;
	CHECK_EQ(run_pass(2, &r), K1_DDRT_OK);
	CHECK_CLEAN();
	for (uint32_t i = 0; i < 4; i++) {
		CHECK_EQ(r.pi[i].stale_first, 0);
		CHECK(r.pi[i].max_polls >= 20);
		CHECK(r.pi[i].max_us > 0);
	}
	CHECK_EQ(r.pi[0].ops, 2);
	CHECK_EQ(r.pi[1].ops, 2);
	CHECK_EQ(r.pi[2].ops, 34);
	CHECK_EQ(r.pi[3].ops, 34);
	CHECK_EQ(r.sr_stat[0], 0x33);
	CHECK_EQ(r.sr_stat[1], 0x77);
	CHECK_EQ(r.sr_stat[2], 0x11);
	for (uint32_t s = 0; s < 16; s++)
		CHECK_EQ(r.rx_min[s], r.rx_min_cs[0][s] < r.rx_min_cs[1][s] ?
				      r.rx_min_cs[0][s] : r.rx_min_cs[1][s]);

	/* silicon that drops done late would end the wait on the leftover */
	model_reset(DDR, 2, 2);
	model_poke(DDR + K1_MC_PI_STATUS, 0x8000'8067);
	g_model.clear_delay = 3;
	g_model.latency = 20;
	(void)run_pass(2, &r);
	CHECK(g_model.violations > 0);
	CHECK(r.pi[0].stale_first >= 1);
}

static uint8_t plateau_rx(const struct model *m, uint32_t step, uint32_t cs,
			  uint32_t dq)
{
	(void)m; (void)cs;
	if (step >= 3 && step <= 9)
		return (uint8_t)(12 + (dq % 3));
	return (uint8_t)(4 + step % 3);
}

static uint8_t plateau_tx(const struct model *m, uint32_t step, uint32_t cs,
			  uint32_t dq)
{
	(void)m; (void)cs; (void)dq;
	return step >= 2 && step <= 12 ? 10 : 3;
}

static void test_plateau_first(void)
{
	struct k1_ddrt_result r;

	model_reset(DDR, 2, 2);
	g_model.rx_margin = plateau_rx;
	g_model.tx_margin = plateau_tx;
	CHECK_EQ(run_pass(2, &r), K1_DDRT_OK);
	CHECK_CLEAN();
	CHECK_EQ(r.rx_vref_idx, 3);
	CHECK_EQ(r.tx_vref_dq, 23);
	CHECK_EQ(r.rx_min[3], 12);
	CHECK_EQ(r.tx_min[12], 10);
}

static uint8_t zero_margin(const struct model *m, uint32_t step, uint32_t cs,
			   uint32_t dq)
{
	(void)m; (void)step; (void)cs; (void)dq;
	return 0;
}

static void test_tx_all_zero(void)
{
	struct k1_ddrt_result r;

	model_reset(DDR, 2, 2);
	g_model.tx_margin = zero_margin;
	CHECK_EQ(run_pass(2, &r), K1_DDRT_OK);
	CHECK_CLEAN();
	CHECK_EQ(r.tx_vref_dq, 25);
	CHECK_EQ(g_model.die_mr14[0][0], 25);
}

/* one rank alone has a dead DQ17 around the eye centre of the other */
static uint32_t g_bad_cs;

static uint8_t one_rank_bad_dq(const struct model *m, uint32_t step,
			       uint32_t cs, uint32_t dq)
{
	if (cs == g_bad_cs && dq == 17 && step >= 4 && step <= 6)
		return 0;
	return model_default_rx_margin(m, step, cs, dq);
}

static uint8_t one_rank_bad_tx(const struct model *m, uint32_t step,
			       uint32_t cs, uint32_t dq)
{
	if (cs == g_bad_cs && dq == 30 && step >= 6 && step <= 8)
		return 0;
	return model_default_tx_margin(m, step, cs, dq);
}

static void test_all_ranks_count(void)
{
	for (g_bad_cs = 1; g_bad_cs <= 2; g_bad_cs++) {
		struct k1_ddrt_result r;

		model_reset(DDR, 2, 2);
		g_model.rx_margin = one_rank_bad_dq;
		g_model.tx_margin = one_rank_bad_tx;
		const uint32_t rx = expected_best(one_rank_bad_dq, 2);
		const uint32_t tx = expected_best(one_rank_bad_tx, 2);

		CHECK(rx < 4 || rx > 6);
		CHECK(tx < 6 || tx > 8);
		CHECK_EQ(run_pass(2, &r), K1_DDRT_OK);
		CHECK_CLEAN();
		CHECK_EQ(r.rx_vref_idx, rx);
		CHECK_EQ(r.tx_vref_dq, 21 + tx);
		CHECK_EQ(r.rx_min[5], 0);
		CHECK_EQ(r.tx_min[7], 0);
	}
}

static void test_sr_enter_timeout(void)
{
	struct k1_ddrt_result r;

	model_reset(DDR, 2, 2);
	g_model.sr_enter_stuck = true;
	CHECK_EQ(run_pass(2, &r), K1_DDRT_ETIMEDOUT);
	CHECK_CLEAN();
	CHECK_EQ(r.fail_stage, K1_STAGE_SR_ENTER);
	CHECK_EQ(g_model.ev_count[EV_SR_EXIT], 1);
	CHECK_EQ(g_model.ev_count[EV_PHY_INIT], 0);
	CHECK_EQ(g_model.ev_count[EV_OP_WL], 0);
}

static void test_gate_timeout(void)
{
	struct k1_ddrt_result r;

	model_reset(DDR, 2, 2);
	g_model.gate_timeout_cs = 2;
	/* re-train: an older pass left valid-looking codes, only PI_STATUS tells */
	for (uint32_t b = 0; b < 4; b++)
		model_poke(slice_reg(K1_BYTE_OFF[b] + (1u << K1_PHY_CS_SHIFT) +
				     K1_BYTE_GATE), 0x0001'2525);
	CHECK_EQ(run_pass(2, &r), K1_DDRT_EIO);
	CHECK_CLEAN();
	CHECK_EQ(r.fail_stage, K1_STAGE_GATE);
	CHECK_EQ(r.fail_cs, 2);
	CHECK_EQ(g_model.ev_count[EV_OP_RD], 0);
	CHECK(!g_model.in_sr);
	CHECK_EQ(model_peek(DDR + K1_MC_STATUS), 0x11);
}

static void test_pi_hang_timed(void)
{
	struct k1_ddrt_result r;

	model_reset(DDR, 2, 2);
	g_model.hang_op = EV_OP_WR;
	g_model.time_step_us = 50;
	CHECK_EQ(run_pass(2, &r), K1_DDRT_ETIMEDOUT);
	CHECK_EQ(r.fail_stage, K1_STAGE_TX);
	CHECK_EQ(r.fail_cs, 1);
	CHECK_EQ(g_model.ev_count[EV_SR_EXIT], 1);
	CHECK(!g_model.in_sr);
}

static void test_pi_hang_poll_cap(void)
{
	struct k1_ddrt_result r;
	const struct k1_ddrt_cfg cfg = { .ddr = DDR, .cs_num = 2, .boot_pp = 0 };
	const struct k1_ddrt_env env = { .vlog = test_vlog, .poll_limit = 1000 };

	model_reset(DDR, 0, 2);
	g_model.hang_op = EV_OP_WL;
	CHECK_EQ(k1_ddrt_train_pass(&cfg, &env, &r), K1_DDRT_ETIMEDOUT);
	CHECK_EQ(r.fail_stage, K1_STAGE_WL);
	CHECK(!g_model.in_sr);
}

static void test_rl_bit_cleared(void)
{
	struct k1_ddrt_result r;

	model_reset(DDR, 2, 2);
	g_model.arm_rl_in_sweep = true;
	CHECK_EQ(run_pass(2, &r), K1_DDRT_OK);
	CHECK_CLEAN();
	CHECK_EQ(r.phy_status, 0);
}

static void test_status_fails_boot(void)
{
	struct k1_ddrt_result r;

	model_reset(DDR, 2, 2);
	g_model.final_status_bits = K1_ST_WDQ;
	CHECK_EQ(run_pass(2, &r), K1_DDRT_EIO);
	CHECK_CLEAN();
	CHECK_EQ(r.fail_stage, K1_STAGE_STATUS);
	CHECK_EQ(r.phy_status, K1_ST_WDQ);
	CHECK(!g_model.in_sr);
}

static void test_bad_args(void)
{
	struct k1_ddrt_result r;
	const struct k1_ddrt_env env = env_timed();
	const struct k1_ddrt_cfg bad[] = {
		{ .ddr = DDR, .cs_num = 0, .boot_pp = 0 },
		{ .ddr = DDR, .cs_num = 3, .boot_pp = 0 },
		{ .ddr = DDR, .cs_num = 2, .boot_pp = 4 },
		{ .ddr = 0, .cs_num = 2, .boot_pp = 0 },
	};

	model_reset(DDR, 0, 2);
	for (uint32_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		CHECK_EQ(k1_ddrt_train_pass(&bad[i], &env, &r), K1_DDRT_EINVAL);
		CHECK_EQ(r.fail_stage, K1_STAGE_ARGS);
	}
	CHECK_EQ(k1_ddrt_train_pass(&bad[0], &env, nullptr), K1_DDRT_EINVAL);
	CHECK_EQ(g_model.mmio_writes, 0);
}

static void test_other_base(void)
{
	struct k1_ddrt_result r;
	constexpr uintptr_t base = 0x2000'0000;

	model_reset(base, 1, 2);
	CHECK_EQ(run_pass(2, &r), K1_DDRT_OK);
	CHECK_CLEAN();
	CHECK_EQ(model_peek(base + K1_PHY_OFF + 0x4000 + K1_SLICE_VREF0),
		 k1_pack_vref(5) | 0xa937);
	CHECK_EQ(model_peek(base + K1_PHY_OFF + K1_SLICE_VREF0), 0x5555'a937);
}

static void test_mr_read(void)
{
	const struct k1_ddrt_cfg cfg = { .ddr = DDR, .cs_num = 2 };
	const struct k1_ddrt_env env = env_timed();
	uint8_t v = 0;

	model_reset(DDR, 0, 2);
	CHECK_EQ(k1_ddrt_mr_read(&cfg, &env, 0, 0, 8, &v), K1_DDRT_OK);
	CHECK_EQ(v, 0x52);
	CHECK_EQ(k1_ddrt_mr_read(&cfg, &env, 1, 1, 5, &v), K1_DDRT_OK);
	CHECK_EQ(v, 0x01);
	CHECK_EQ(k1_ddrt_mr_read(&cfg, &env, 2, 0, 5, &v), K1_DDRT_EINVAL);
	CHECK_CLEAN();
}

static void test_max_min(void)
{
	const uint8_t a[16] = { 0 };
	const uint8_t b[16] = { 1, 5, 5, 3, 9, 9, 2, 0, 0, 9, 1, 1, 1, 1, 1, 1 };
	const uint8_t c[16] = { [15] = 1 };

	CHECK_EQ(k1_ddrt_max_min(a), 0);
	CHECK_EQ(k1_ddrt_max_min(b), 4);
	CHECK_EQ(k1_ddrt_max_min(c), 15);
}

int main(int argc, char **argv)
{
	static const struct {
		const char *name;
		void (*fn)(void);
	} tests[] = {
		{ "happy path, 2 CS, pp2", test_happy_2cs },
		{ "happy path, 1 CS, pp0", test_happy_1cs },
		{ "leftover done bits do not empty the waits", test_leftover_status },
		{ "PI counters and stale-read detector", test_pi_stats },
		{ "plateau keeps the first step", test_plateau_first },
		{ "Tx eye closed everywhere falls back to MR14 25", test_tx_all_zero },
		{ "minimum spans every rank and DQ", test_all_ranks_count },
		{ "self-refresh entry timeout still exits SR", test_sr_enter_timeout },
		{ "read-gate TIMEOUT stops the pass", test_gate_timeout },
		{ "hung PI op times out (clock)", test_pi_hang_timed },
		{ "hung PI op times out (poll cap)", test_pi_hang_poll_cap },
		{ "sweep-armed read-leveling bit is cleared", test_rl_bit_cleared },
		{ "non-zero PHY status fails the pass", test_status_fails_boot },
		{ "bad arguments touch no register", test_bad_args },
		{ "addresses relative to ddr", test_other_base },
		{ "mode register read", test_mr_read },
		{ "max-min rule", test_max_min },
		{ "gate TIMEOUT behind a leftover bit 2 (P-09)", test_gate_leftover_timeout },
		{ "gate TIMEOUT over codes of an earlier pass", test_gate_timeout_stale_codes },
		{ "data rate to final fp, DFC plan", test_rate_plan },
		{ "PLL, lock and SW-FC to fp0", test_pll_init },
		{ "Device_Init x16 from power-on", test_device_init },
		{ "Device_Init refuses an unclocked or mis-selected MC", test_device_init_guards },
		{ "Device_Init x8 overlays", test_device_init_x8 },
		{ "Device_Init LPDDR4 IO and WDQS", test_device_init_lpddr4 },
		{ "MR8 to size code and geometry", test_geometry },
		{ "address map words", test_map_apply },
		{ "DFC table: flow, snapshot, markers", test_dfc_table },
		{ "DFC window, refusals, a stuck engine", test_dfc_stuck },
		{ "DFC table: one rank, 16 GiB", test_dfc_table_variants },
		{ "hardware DFC", test_dfc },
		{ "full boot 8 GiB x8 2 CS 2400", test_full_boot_8g },
		{ "full boot 4 GiB x16 1 CS 2666", test_full_boot_1cs },
		{ "full boot at every data rate", test_full_boot_rates },
		{ "full boot 16 GiB adds MR21", test_full_boot_16g },
		{ "full boot: first MRR after DFC(2)", test_full_boot_fsp },
		{ "boot failures stop at their step", test_boot_failures },
		{ "doc 21 acceptance: final pp ranges", test_accept_final },
		{ "doc 21 acceptance: 2400 first pp2 (incl. 2666 eye)", test_accept_pp2_2400 },
		{ "doc 21 acceptance: 2nd pp0 gate", test_accept_pp0b_gate },
		{ "blob UART dump of a pass and of the MRs", test_accept_prints },
	};

	g_log = argc > 1 && !strcmp(argv[1], "-v");
	for (uint32_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
		const int before = g_fails;

		tests[i].fn();
		printf("%-4s %s\n", g_fails == before ? "ok" : "FAIL", tests[i].name);
	}
	printf("%d checks, %d failures\n", g_checks, g_fails);
	return g_fails ? 1 : 0;
}
