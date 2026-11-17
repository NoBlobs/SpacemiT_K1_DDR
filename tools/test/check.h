/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Thalles Campagnani */
#ifndef K1_TEST_CHECK_H
#define K1_TEST_CHECK_H

#include <stdarg.h>
#include <stdio.h>

#include "k1_ddrt.h"
#include "model.h"

static constexpr uintptr_t DDR = 0xc000'0000;
static constexpr uintptr_t PMU = 0xd428'2800;
static constexpr uintptr_t WIN = 0xf000'0000;

extern int g_checks, g_fails;
extern bool g_log;

#define CHECK(c)                                                            \
	do {                                                                \
		g_checks++;                                                 \
		if (!(c)) {                                                 \
			g_fails++;                                          \
			printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
		}                                                           \
	} while (0)

#define CHECK_EQ(a, b)                                                      \
	do {                                                                \
		const long long a_ = (long long)(a), b_ = (long long)(b);   \
		g_checks++;                                                 \
		if (a_ != b_) {                                             \
			g_fails++;                                          \
			printf("  FAIL %s:%d: %s == %s (0x%llx vs 0x%llx)\n", \
			       __FILE__, __LINE__, #a, #b, a_, b_);         \
		}                                                           \
	} while (0)

#define CHECK_CLEAN()                                                       \
	do {                                                                \
		CHECK_EQ(g_model.violations, 0);                            \
		if (g_model.violations)                                     \
			printf("  first violation: %s\n", g_model.violation); \
	} while (0)

[[gnu::format(printf, 2, 0)]]
void test_vlog(void *ctx, const char *fmt, va_list ap);
struct k1_ddrt_env env_timed(void);
/* everything test_vlog saw since the last log_clear(), first 64 KiB */
void log_clear(void);
bool log_has(const char *s);

/* test_boot.c */
void test_gate_leftover_timeout(void);
void test_gate_timeout_stale_codes(void);
void test_rate_plan(void);
void test_pll_init(void);
void test_device_init(void);
void test_device_init_guards(void);
void test_device_init_x8(void);
void test_device_init_lpddr4(void);
void test_geometry(void);
void test_map_apply(void);
void test_dfc_table(void);
void test_dfc_table_variants(void);
void test_dfc(void);
void test_dfc_stuck(void);
void test_full_boot_8g(void);
void test_full_boot_1cs(void);
void test_full_boot_rates(void);
void test_full_boot_16g(void);
void test_full_boot_fsp(void);
void test_boot_failures(void);
void test_accept_final(void);
void test_accept_pp2_2400(void);
void test_accept_pp0b_gate(void);
void test_accept_prints(void);

#endif /* K1_TEST_CHECK_H */
