// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Thalles Campagnani
/*
 * Host check of one silicon-injection flag (K1_EXP_*). Compiled on its
 * own; the production suite must not define those flags.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "check.h"
#include "k1_ddr_boot.h"

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

int main(void)
{
	const struct k1_ddrt_env env = {
		.now_us = model_now_us,
		.vlog = test_vlog,
	};
	const struct k1_ddr_boot_cfg cfg = {
		.ddr = DDR,
		.pmu = PMU,
		.dfc_win = WIN,
		.cs_num = 2,
		.data_rate = 2400,
		.type = K1_DDR_LPDDR4X,
	};
	struct k1_ddr_boot_report rep;
	int rc;

	model_reset_por(DDR, 2);
	log_clear();
	rc = k1_ddr_boot(&cfg, &env, &rep);
#if defined(K1_EXP_PLL_NO_LOCK)
	CHECK_EQ(rc, K1_DDRT_ETIMEDOUT);
	CHECK_EQ(rep.rc, K1_DDRT_ETIMEDOUT);
	CHECK_EQ(rep.fail_step, K1_STEP_PLL);
	CHECK_EQ(rep.n_pass, 0);
	CHECK(log_has("EXP PLL_NO_LOCK"));
	CHECK(log_has("PLL1/PLL2 not locked"));
	CHECK(log_has("pll-lock failed"));
	CHECK_EQ(g_model.mc_writes, 0);
#elif defined(K1_EXP_GATE_TIMEOUT)
	CHECK_EQ(rc, K1_DDRT_EIO);
	CHECK_EQ(rep.fail_step, K1_STEP_TRAIN);
	CHECK_EQ(rep.n_pass, 1);
	CHECK_EQ(rep.pass[0].fail_stage, K1_STAGE_GATE);
	CHECK(log_has("EXP GATE_TIMEOUT"));
	CHECK(log_has("read-gate TIMEOUT on cs 1"));
	CHECK(log_has("read-gate failed"));
	CHECK_EQ(g_model.n_dfc, 0);
#elif defined(K1_EXP_DFC_TIMEOUT)
	CHECK_EQ(rc, K1_DDRT_ETIMEDOUT);
	CHECK_EQ(rep.fail_step, K1_STEP_DFC);
	CHECK_EQ(rep.n_pass, 2);
	CHECK(log_has("EXP DFC_TIMEOUT"));
	CHECK(log_has("DFC to fp1 not done"));
	CHECK(log_has("dfc failed"));
#else
#error "compile with one of K1_EXP_PLL_NO_LOCK, K1_EXP_GATE_TIMEOUT, K1_EXP_DFC_TIMEOUT"
#endif
	CHECK_CLEAN();
	printf("%d checks, %d failures\n", g_checks, g_fails);
	return g_fails ? 1 : 0;
}
