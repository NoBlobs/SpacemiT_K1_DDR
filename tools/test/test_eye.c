// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Thalles Campagnani
#include "check.h"
#include "k1_ddr_eye.h"
#include "../lib/k1_ddrt_regs.h"

static uint32_t eye_tap(uint32_t fp, uint32_t cs, uint32_t b, uint32_t off)
{
	return model_peek(DDR + K1_PHY_OFF + (fp << K1_PHY_SLICE_SHIFT) +
			  K1_BYTE_OFF[b] + (cs << K1_PHY_CS_SHIFT) + off);
}

void test_cs1_cpu_buf(void)
{
	/* 16 GiB final map: CS1 at 10 GiB, buffer 128 MiB into the rank. */
	CHECK_EQ(k1_eye_cs1_cpu(2, 0x0011'0001), 0x2'8800'0000ull);
	/* 16 GiB default map during first fp0: CS1 at 6 GiB. */
	CHECK_EQ(k1_eye_cs1_cpu(1, 0x0010'0001), 0x1'8800'0000ull);
	/* 8 GiB final map: CS1 at 6 GiB. */
	CHECK_EQ(k1_eye_cs1_cpu(1, 0x0010'0001), 0x1'8800'0000ull);
}

void test_indep_eye_2cs(void)
{
	const uint32_t fp = 2;
	const struct k1_ddrt_env env = env_timed();

	model_reset(DDR, fp, 2);
	model_seed_indep_eye(fp);
	log_clear();
	CHECK_EQ(k1_ddr_eye_measure(DDR, 2, fp, &env), K1_DDRT_OK);
	CHECK_CLEAN();
	CHECK(log_has("verify OK"));
	CHECK(log_has("MR14 check rc 0"));
	CHECK_EQ((model_peek(DDR + K1_PHY_OFF + (fp << K1_PHY_SLICE_SHIFT) +
			     K1_SLICE_VREF0) >> 16) & 0xf, 5);
	CHECK_EQ((model_peek(DDR + K1_PHY_OFF + (fp << K1_PHY_SLICE_SHIFT) +
			     K1_SLICE_VREF1) >> 16) & 0xf, 5);
	CHECK_EQ((model_peek_bank(fp, K1_MC_TXVREF) >> 16) & 0x3f, 28);
	for (uint32_t cs = 0; cs < 2; cs++) {
		for (uint32_t b = 0; b < K1_N_BYTES; b++) {
			CHECK_EQ(eye_tap(fp, cs, b, K1_BYTE_WL) & 31, 20);
			for (uint32_t l = 0; l < K1_N_LANES; l++) {
				CHECK_EQ(eye_tap(fp, cs, b, K1_BYTE_RX_DQ + 4 * l) &
					 K1_RX_DLY_MASK, 16);
				CHECK_EQ(eye_tap(fp, cs, b, K1_BYTE_TX_DQ + 4 * l) &
					 K1_TX_DLY_MASK, 36);
			}
		}
	}
}

void test_indep_eye_seed_outside(void)
{
	const uint32_t fp = 2;
	const struct k1_ddrt_env env = env_timed();

	model_reset(DDR, fp, 1);
	model_seed_indep_eye(fp);
	/* PI-like seed already outside the geometric RX window. */
	model_poke(DDR + K1_PHY_OFF + (fp << K1_PHY_SLICE_SHIFT) +
		   K1_BYTE_OFF[0] + K1_BYTE_RX_DQ, 0);
	log_clear();
	CHECK_EQ(k1_ddr_eye_measure(DDR, 1, fp, &env), K1_DDRT_OK);
	CHECK_CLEAN();
	CHECK(log_has("verify OK"));
	CHECK_EQ(eye_tap(fp, 0, 0, K1_BYTE_RX_DQ) & K1_RX_DLY_MASK, 16);
}

void test_indep_eye_verify_fail(void)
{
	const uint32_t fp = 2;
	const struct k1_ddrt_env env = env_timed();

	model_reset(DDR, fp, 1);
	model_seed_indep_eye(fp);
	g_model.eye_force_fail = true;
	log_clear();
	CHECK_EQ(k1_ddr_eye_measure(DDR, 1, fp, &env), K1_DDRT_EIO);
	CHECK(log_has("verify FAIL"));
}
