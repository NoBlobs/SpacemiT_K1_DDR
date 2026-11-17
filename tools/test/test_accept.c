// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Thalles Campagnani
/* Host tests for the blob's doc 21 diagnostics (check + UART dump). */
#include <string.h>

#include "check.h"
#include "k1_ddr_accept.h"
#include "../lib/k1_ddrt_regs.h"

static constexpr uint32_t GATE_OK = (1u << 16) | (36u << 8) | 36u; /* 0x00012424 */

static struct k1_ddrt_result gold_pp2(void)
{
	struct k1_ddrt_result r = {
		.rx_vref_idx = 6,
		.tx_vref_dq = 30,
		.rx_min[6] = 10,
		.tx_min[30 - K1_TX_VREF_SEED0] = 10,
		.vref0 = 0x6666a937,
		.vref1 = 0x6666a927,
		.txvref = 0x9d1e4314,
		.pi_status = 0x80008000,
		.sr_stat[0] = 0x33,
		.sr_stat[1] = 0x77,
		.sr_stat[2] = 0x33,
		.pi[0].ops = 2,
		.stage_us[0] = 48,
	};

	for (uint32_t cs = 0; cs < 2; cs++) {
		for (uint32_t b = 0; b < 4; b++) {
			r.wl[cs][b] = 20;
			r.gate[cs][b] = GATE_OK;
			r.rx_dq[cs][b][0] = 18;
			r.tx_dq[cs][b][0] = 33;
			r.tx_dm[cs][b] = 32;
		}
		r.rx_min_cs[cs][6] = 10;
		r.tx_min_cs[cs][30 - K1_TX_VREF_SEED0] = 10;
	}
	return r;
}

static const struct k1_ddrt_env *log_env(void)
{
	static const struct k1_ddrt_env env = {
		.now_us = model_now_us,
		.vlog = test_vlog,
	};

	return &env;
}

void test_accept_final(void)
{
	struct k1_ddrt_result r = gold_pp2();
	const struct k1_ddrt_env *env = log_env();

	log_clear();
	CHECK_EQ(k1_ddr_accept_final(env, &r, 2, K1_DDRT_OK), 0);
	CHECK(!log_has("out of range"));

	r.wl[1][2] = 10;
	log_clear();
	CHECK_EQ(k1_ddr_accept_final(env, &r, 2, K1_DDRT_OK), 1);
	CHECK(log_has("out of range: cs1 b2 WL 10 (17..23)"));
	r.wl[1][2] = 20;

	r.gate[0][0] = 0x00007070;	/* no bit 16, bytes 112 */
	log_clear();
	CHECK_EQ(k1_ddr_accept_final(env, &r, 2, K1_DDRT_OK), 1);
	CHECK(log_has("out of range: cs0 b0 gate"));
	r.gate[0][0] = GATE_OK;

	r.rx_dq[1][1][0] = 20;
	log_clear();
	CHECK_EQ(k1_ddr_accept_final(env, &r, 2, K1_DDRT_OK), 1);
	CHECK(log_has("out of range: cs1 b1 Rx DQ0 20 (17..19)"));
	r.rx_dq[1][1][0] = 18;

	r.tx_dq[0][3][0] = 40;
	log_clear();
	CHECK_EQ(k1_ddr_accept_final(env, &r, 2, K1_DDRT_OK), 1);
	CHECK(log_has("out of range: cs0 b3 Tx DQ0 40 (30..36)"));
	r.tx_dq[0][3][0] = 33;

	r.phy_status = 0x10;
	log_clear();
	CHECK_EQ(k1_ddr_accept_final(env, &r, 2, K1_DDRT_OK), 1);
	CHECK(log_has("phy_status 0x10 != 0"));
	r.phy_status = 0;

	log_clear();
	CHECK_EQ(k1_ddr_accept_final(env, &r, 2, K1_DDRT_EIO), 1);
	CHECK(log_has("MR14 != CTL_TXVREF[21:16] (rc -5)"));
}

void test_accept_pp2_2400(void)
{
	struct k1_ddrt_result r = gold_pp2();
	const struct k1_ddrt_env *env = log_env();

	log_clear();
	CHECK_EQ(k1_ddr_accept_pp2_2400(env, &r), 0);
	CHECK(!log_has("out of range"));
	CHECK(!log_has("check PLL1"));

	/* 2666 eye on a 2400 boot: the negative side that the silicon never hit */
	r.rx_vref_idx = 7;
	r.rx_min[7] = 9;
	r.tx_vref_dq = 25;
	r.tx_min[25 - K1_TX_VREF_SEED0] = 8;
	log_clear();
	CHECK_EQ(k1_ddr_accept_pp2_2400(env, &r), 2);
	CHECK(log_has("out of range: first pp2 Rx idx 7 min 9 (4..6, min 10)"));
	CHECK(log_has("out of range: first pp2 Tx MR14 25 min 8 (29..31, min 10)"));
	CHECK(log_has("at 2400 a min of 8/9 is the 2666 CK: check PLL1 (doc 25)"));

	r = gold_pp2();
	r.tx_vref_dq = 20;	/* outside the 21..36 sweep */
	log_clear();
	CHECK_EQ(k1_ddr_accept_pp2_2400(env, &r), 1);
	CHECK(log_has("first pp2 Tx MR14 20 min 0"));
	CHECK(log_has("check PLL1"));
}

void test_accept_pp0b_gate(void)
{
	struct k1_ddrt_result r = gold_pp2();
	const struct k1_ddrt_env *env = log_env();

	log_clear();
	CHECK_EQ(k1_ddr_accept_pp0b_gate(env, &r, 2), 0);
	r.gate[0][1] = 0x00017070;	/* 112 taps of the 1st pp0 */
	log_clear();
	CHECK_EQ(k1_ddr_accept_pp0b_gate(env, &r, 2), 1);
	CHECK(log_has("second pp0: cs0 b1 gate 0x00017070 not ~32..40"));
}

void test_accept_prints(void)
{
	struct k1_ddrt_result r = gold_pp2();
	const struct k1_ddrt_env *env = log_env();
	const struct k1_ddrt_cfg mc = { .ddr = DDR, .cs_num = 2 };

	log_clear();
	k1_ddr_print_result(env, "state", &r, 2);
	CHECK(log_has("state: vref0 0x6666a937"));
	CHECK(log_has("mr14 field 30"));
	CHECK(log_has("cs0 wl"));
	CHECK(log_has("cs1 wl"));
	CHECK(log_has("rx dq0"));
	CHECK(log_has("tx dq0"));
	CHECK(log_has("tx dm"));
	CHECK(log_has("b0:18"));
	CHECK(log_has("0x00012424"));

	log_clear();
	k1_ddr_print_pass_stats(env, &r, 2);
	CHECK(log_has("sr_stat before 0x33 after-enter 0x77 after-exit 0x33"));
	CHECK(log_has("pi wl   ops 2"));
	CHECK(log_has("rx min cs1"));
	CHECK(log_has("tx min cs2"));

	model_reset(DDR, 2, 2);
	log_clear();
	k1_ddr_print_mrs(env, &mc);
	CHECK(log_has("cs0 ch0"));
	CHECK(log_has("MR5:0x01"));
	CHECK(log_has("MR8:0x52"));
	CHECK(log_has("MR14:0x19"));
	CHECK(log_has("cs1 ch1"));
}
