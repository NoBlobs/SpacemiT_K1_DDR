// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Thalles Campagnani
/*
 * Independent data-eye training by memory-pattern sweep. See k1_ddr_eye.h.
 *
 * After PI bring-up (WL + DQS gate; the gate cannot be positioned by software,
 * phy.md), this trains RX/TX Vref and then re-centres every DQ delay from a
 * pattern moved through DRAM, without the PI harvest (eye-training.md).
 *
 * Order: coarse Vref (window width) then fine delay. Edges: binary search
 * on a short pattern, then confirm the two edges with a longer pattern.
 *
 * F6.2 cache flush is not used: Zicbom `cbo.flush` traps in the SPL, and
 * T-Head `dcache.cipa` is an illegal instruction from this image (FDL
 * logsRefs after FSBL-eye-f6.bin, TVAL 0x02b5000b) even after THEADISAEE
 * and MHCR.DE. With D-cache off a short pattern already hits DRAM.
 */
#include "k1_ddr_eye.h"
#include "k1_ddrt_io.h"
#include "k1_ddrt_priv.h"
#include "k1_ddrt_regs.h"

#ifdef K1_DDRT_MOCK
bool k1_mock_eye_fail(uint32_t fp);
#endif

static constexpr uintptr_t K1_EYE_RANK_OFF = 0x0800'0000;	/* 128 MiB into the rank */
static constexpr uint64_t  K1_CPU_HOLE     = 0x8000'0000;	/* 2–4 GiB MMIO */
static constexpr uint32_t  K1_EYE_COARSE_W = 16;		/* 64 B, Vref metric */
static constexpr uint32_t  K1_EYE_RX_W     = 64;		/* 256 B, delay search */
static constexpr uint32_t  K1_EYE_TX_W     = 32;		/* 128 B */
static constexpr uint32_t  K1_EYE_FINE_W   = 1024;		/* 4 KiB, edge confirm */
static constexpr uint32_t  K1_EYE_VFY_W    = 2048;		/* 8 KiB */
static constexpr uint32_t  K1_EYE_MRW_US   = 1;

#ifdef K1_DDRT_MOCK
static uint32_t k1_mock_rank[2][K1_EYE_VFY_W];
static uint32_t k1_mock_fp;
#endif

static inline uint32_t k1_eye_pat(uint32_t i, uint32_t salt)
{
	uint32_t x = i ^ (salt * 0x9e37'79b9u);

	x *= 2654435761u;
	x ^= x >> 15;
	return x;
}

static void k1_eye_fill(volatile uint32_t *p, uint32_t nwords, uint32_t salt)
{
	for (uint32_t i = 0; i < nwords; i++)
		p[i] = k1_eye_pat(i, salt);
	k1_io_fence();
}

static uint32_t k1_eye_bad(volatile uint32_t *p, uint32_t nwords, uint32_t salt)
{
#ifdef K1_DDRT_MOCK
	if (k1_mock_eye_fail(k1_mock_fp))
		return 1;
#endif
	for (uint32_t i = 0; i < nwords; i++)
		if (p[i] != k1_eye_pat(i, salt))
			return 1;
	return 0;
}

uint64_t k1_eye_cs1_cpu(uint32_t cs1_hi, uint32_t map)
{
	const uint32_t start_8mb = (cs1_hi << 9) |
		((map >> K1_MAP_START_SHIFT) & K1_MAP_START_LO);
	uint64_t dram = (uint64_t)start_8mb << 23;

	if (dram >= K1_CPU_HOLE)
		dram += K1_CPU_HOLE;
	return dram + K1_EYE_RANK_OFF;
}

/* CS1 CPU base: map start (8 MB units) plus the 2–4 GiB hole (address-map.md). */
static uintptr_t k1_eye_rank_buf(uintptr_t ddr, uint32_t cs)
{
#ifdef K1_DDRT_MOCK
	(void)ddr;
	if (cs > 1)
		cs = 1;
	return (uintptr_t)k1_mock_rank[cs];
#else
	if (cs == 0)
		return K1_EYE_RANK_OFF;
	return (uintptr_t)k1_eye_cs1_cpu(k1_r32(ddr + K1_MC_CS1_HI),
					 k1_r32(ddr + K1_MC_CS1_MAP));
#endif
}

static uintptr_t k1_eye_slice(uintptr_t ddr, uint32_t fp)
{
	return ddr + K1_PHY_OFF + ((uintptr_t)fp << K1_PHY_SLICE_SHIFT);
}

static uintptr_t k1_eye_lane(uintptr_t ddr, uint32_t fp, uint32_t cs,
			     uint32_t b, uint32_t reg, uint32_t l)
{
	return k1_eye_slice(ddr, fp) + K1_BYTE_OFF[b] +
	       ((uintptr_t)cs << K1_PHY_CS_SHIFT) + reg + 4u * l;
}

static uint32_t k1_eye_probe(volatile uint32_t *mem, uintptr_t a, uint32_t mask,
			     uint32_t code, uint32_t words, bool rewrite)
{
	k1_rmw32(a, ~mask, code & mask);
	if (rewrite)
		k1_eye_fill(mem, words, 0);
	return k1_eye_bad(mem, words, 0);
}

/* Widest 0-error run in pass[0..ncode). Width 0 if none. */
static uint32_t k1_eye_window(const uint8_t *pass, uint32_t ncode, int *lo)
{
	int best_lo = -1, best_w = 0, cur = -1;

	for (uint32_t c = 0; c < ncode; c++) {
		if (pass[c]) {
			if (cur < 0)
				cur = (int)c;
			if ((int)c - cur + 1 > best_w) {
				best_w = (int)c - cur + 1;
				best_lo = cur;
			}
		} else {
			cur = -1;
		}
	}
	*lo = best_lo;
	return (uint32_t)best_w;
}

/* Linear fallback when the PI seed is already outside the window. */
static uint32_t k1_eye_linear(volatile uint32_t *mem, uintptr_t a, uint32_t mask,
			      uint32_t ncode, uint32_t words, bool rewrite,
			      int *lo)
{
	uint8_t pass[64];

	for (uint32_t c = 0; c < ncode; c++)
		pass[c] = k1_eye_probe(mem, a, mask, c, words, rewrite) ? 0 : 1;
	return k1_eye_window(pass, ncode, lo);
}

/*
 * Window containing `seed`: binary-search the left and right 0-error edges
 * with `words`, then confirm/expand those two edges with `fine` (F6.3).
 * Restores nothing; the caller writes the centre or the saved tap.
 */
static uint32_t k1_eye_edges(volatile uint32_t *mem, uintptr_t a, uint32_t mask,
			     uint32_t ncode, uint32_t seed, uint32_t words,
			     uint32_t fine, bool rewrite, int *lo)
{
	uint32_t left, right, l, h;

	if (seed >= ncode)
		seed = ncode / 2;
	if (k1_eye_probe(mem, a, mask, seed, words, rewrite))
		return k1_eye_linear(mem, a, mask, ncode, words, rewrite, lo);

	l = 0;
	h = seed;
	while (l < h) {
		const uint32_t mid = l + (h - l) / 2;

		if (k1_eye_probe(mem, a, mask, mid, words, rewrite) == 0)
			h = mid;
		else
			l = mid + 1;
	}
	left = l;

	l = seed;
	h = ncode - 1;
	while (l < h) {
		const uint32_t mid = l + (h - l + 1) / 2;

		if (k1_eye_probe(mem, a, mask, mid, words, rewrite) == 0)
			l = mid;
		else
			h = mid - 1;
	}
	right = l;

	if (fine > words) {
		while (left > 0 &&
		       k1_eye_probe(mem, a, mask, left - 1, fine, rewrite) == 0)
			left--;
		while (right + 1 < ncode &&
		       k1_eye_probe(mem, a, mask, right + 1, fine, rewrite) == 0)
			right++;
		while (left <= right &&
		       k1_eye_probe(mem, a, mask, left, fine, rewrite))
			left++;
		while (left <= right &&
		       k1_eye_probe(mem, a, mask, right, fine, rewrite))
			right--;
		if (left > right) {
			*lo = -1;
			return 0;
		}
	}
	*lo = (int)left;
	return right - left + 1;
}

static uint32_t k1_eye_min_width(volatile uint32_t *mem, uintptr_t ddr,
				 uint32_t fp, uint32_t cs, uint32_t b0,
				 uint32_t nb, uint32_t reg, uint32_t mask,
				 uint32_t ncode, uint32_t words, bool rewrite)
{
	uint32_t worst = ~0u;

	if (!rewrite)
		k1_eye_fill(mem, words, 0);
	for (uint32_t b = b0; b < b0 + nb; b++) {
		for (uint32_t l = 0; l < K1_N_LANES; l++) {
			const uintptr_t a = k1_eye_lane(ddr, fp, cs, b, reg, l);
			const uint32_t save = k1_r32(a);
			const uint32_t seed = save & mask;
			int lo;
			const uint32_t w = k1_eye_edges(mem, a, mask, ncode, seed,
							words, words, rewrite, &lo);

			k1_w32(a, save);
			if (w < worst)
				worst = w;
		}
	}
	return worst == ~0u ? 0 : worst;
}

static void k1_eye_train_byte(volatile uint32_t *mem,
			      const struct k1_ddrt_env *env, uintptr_t ddr,
			      uint32_t fp, uint32_t cs, uint32_t b,
			      const char *tag, uint32_t reg, uint32_t mask,
			      uint32_t ncode, uint32_t words, bool rewrite)
{
	uint8_t centre[K1_N_LANES];
	uint8_t width[K1_N_LANES];

	if (!rewrite)
		k1_eye_fill(mem, words, 0);
	for (uint32_t l = 0; l < K1_N_LANES; l++) {
		const uintptr_t a = k1_eye_lane(ddr, fp, cs, b, reg, l);
		const uint32_t save = k1_r32(a);
		int lo;
		const uint32_t w = k1_eye_edges(mem, a, mask, ncode, save & mask,
						words, K1_EYE_FINE_W, rewrite, &lo);

		if (w) {
			const uint32_t c = (uint32_t)lo + w / 2;

			k1_rmw32(a, ~mask, c & mask);
			centre[l] = (uint8_t)c;
			width[l] = (uint8_t)w;
		} else {
			k1_w32(a, save);
			centre[l] = 0xff;
			width[l] = 0;
		}
	}
	k1_log(env, "indep: fp%u cs%u %s b%u centre %u %u %u %u %u %u %u %u\n",
	       fp, cs, tag, b, centre[0], centre[1], centre[2], centre[3],
	       centre[4], centre[5], centre[6], centre[7]);
	k1_log(env, "indep: fp%u cs%u %s b%u width  %u %u %u %u %u %u %u %u\n",
	       fp, cs, tag, b, width[0], width[1], width[2], width[3],
	       width[4], width[5], width[6], width[7]);
}

static void k1_eye_set_rx_vref(uintptr_t slice, uint32_t sub, uint32_t step)
{
	const uintptr_t a = slice + (sub ? K1_SLICE_VREF1 : K1_SLICE_VREF0);
	const uint32_t packed = (k1_r32(a) & K1_VREF_LO_MASK) |
				k1_pack_vref(step);

	k1_w32(a, packed);
}

static void k1_eye_set_mr14(uintptr_t ddr, uint32_t cs_num, uint32_t vref_dq,
			    const struct k1_ddrt_env *env)
{
	k1_rmw32(ddr + K1_MC_TXVREF, K1_TXVREF_MASK,
		 (vref_dq & K1_TXVREF_FIELD) << K1_TXVREF_SHIFT);
	k1_w32(ddr + K1_MC_CA_CMD, k1_ca_mr14(cs_num));
	k1_udelay(env, K1_EYE_MRW_US);
}

/* Sub-PHY A = bytes 0/1 (VREF0), B = bytes 2/3 (VREF1). */
static uint32_t k1_eye_train_rx_vref(volatile uint32_t *mem,
				     const struct k1_ddrt_env *env,
				     uintptr_t ddr, uint32_t fp)
{
	const uintptr_t slice = k1_eye_slice(ddr, fp);
	const uintptr_t v0 = slice + K1_SLICE_VREF0;
	uint32_t best[2] = { 0, 0 };

	k1_rmw32(v0, ~K1_VREF_BIT4, 0);
	for (uint32_t sub = 0; sub < 2; sub++) {
		uint32_t width[K1_N_VREF];
		uint32_t win_best = 0, step_best = 0;
		const uint32_t b0 = sub * 2;

		for (uint32_t step = 0; step < K1_N_VREF; step++) {
			k1_eye_set_rx_vref(slice, sub, step);
			width[step] = k1_eye_min_width(mem, ddr, fp, 0, b0, 2,
						       K1_BYTE_RX_DQ,
						       K1_RX_DLY_MASK, 32,
						       K1_EYE_COARSE_W, false);
			if (width[step] > win_best) {
				win_best = width[step];
				step_best = step;
			}
		}
		best[sub] = step_best;
		k1_eye_set_rx_vref(slice, sub, step_best);
		k1_log(env,
		       "indep: fp%u rx vref %c best %u (min window %u) widths %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u\n",
		       fp, sub ? 'B' : 'A', step_best, win_best,
		       width[0], width[1], width[2], width[3],
		       width[4], width[5], width[6], width[7],
		       width[8], width[9], width[10], width[11],
		       width[12], width[13], width[14], width[15]);
	}
	k1_rmw32(v0, ~0u, K1_VREF_BIT4);
	return (best[0] << 8) | best[1];
}

static uint32_t k1_eye_train_tx_vref(volatile uint32_t *mem,
				     const struct k1_ddrt_env *env,
				     uintptr_t ddr, uint32_t cs_num, uint32_t fp)
{
	uint32_t width[K1_N_VREF];
	uint32_t win_best = 0, step_best = 0;
	uint32_t mr14;

	for (uint32_t i = 0; i < K1_N_VREF; i++) {
		const uint32_t v = K1_TX_VREF_SEED0 + i;

		k1_eye_set_mr14(ddr, cs_num, v, env);
		width[i] = k1_eye_min_width(mem, ddr, fp, 0, 0, K1_N_BYTES,
					    K1_BYTE_TX_DQ, K1_TX_DLY_MASK, 48,
					    K1_EYE_COARSE_W, true);
		if (width[i] > win_best) {
			win_best = width[i];
			step_best = i;
		}
	}
	mr14 = K1_TX_VREF_SEED0 + step_best;
	k1_eye_set_mr14(ddr, cs_num, mr14, env);
	k1_log(env,
	       "indep: fp%u tx mr14 best %u (min window %u) widths %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u\n",
	       fp, mr14, win_best,
	       width[0], width[1], width[2], width[3],
	       width[4], width[5], width[6], width[7],
	       width[8], width[9], width[10], width[11],
	       width[12], width[13], width[14], width[15]);
	return mr14;
}

/* One WL code per byte (5-bit field at +0x24; bit 5 is ignored, phy.md). */
static void k1_eye_train_wl(volatile uint32_t *mem, const struct k1_ddrt_env *env,
			    uintptr_t ddr, uint32_t fp, uint32_t cs)
{
	uint8_t centre[K1_N_BYTES];
	uint8_t width[K1_N_BYTES];

	for (uint32_t b = 0; b < K1_N_BYTES; b++) {
		const uintptr_t a = k1_eye_lane(ddr, fp, cs, b, K1_BYTE_WL, 0);
		const uint32_t save = k1_r32(a);
		int lo;
		const uint32_t w = k1_eye_edges(mem, a, 31, 32, save & 31,
						K1_EYE_TX_W, K1_EYE_FINE_W,
						true, &lo);

		if (w) {
			const uint32_t c = (uint32_t)lo + w / 2;

			k1_rmw32(a, ~31u, c);
			centre[b] = (uint8_t)c;
			width[b] = (uint8_t)w;
		} else {
			k1_w32(a, save);
			centre[b] = 0xff;
			width[b] = 0;
		}
	}
	k1_log(env, "indep: fp%u cs%u wl centre %u %u %u %u width %u %u %u %u\n",
	       fp, cs, centre[0], centre[1], centre[2], centre[3],
	       width[0], width[1], width[2], width[3]);
}

static uint32_t k1_eye_train_rank(volatile uint32_t *mem,
				  const struct k1_ddrt_env *env, uintptr_t ddr,
				  uint32_t fp, uint32_t cs)
{
	uint32_t bad = 0;

	k1_log(env, "indep: fp%u cs%u buf 0x%lx\n", fp, cs,
	       (unsigned long)(uintptr_t)mem);
	for (uint32_t b = 0; b < K1_N_BYTES; b++)
		k1_eye_train_byte(mem, env, ddr, fp, cs, b, "rx", K1_BYTE_RX_DQ,
				  K1_RX_DLY_MASK, 32, K1_EYE_RX_W, false);
	for (uint32_t b = 0; b < K1_N_BYTES; b++)
		k1_eye_train_byte(mem, env, ddr, fp, cs, b, "tx", K1_BYTE_TX_DQ,
				  K1_TX_DLY_MASK, 48, K1_EYE_TX_W, true);
	for (uint32_t s = 1; s <= 3; s++) {
		k1_eye_fill(mem, K1_EYE_VFY_W, s);
		bad += k1_eye_bad(mem, K1_EYE_VFY_W, s);
	}
	k1_log(env, "indep: fp%u cs%u verify %s (%u/3 patterns bad)\n",
	       fp, cs, bad ? "FAIL" : "OK", bad);
	return bad;
}

int k1_ddr_eye_measure(uintptr_t ddr, uint32_t cs_num, uint32_t fp,
		       const struct k1_ddrt_env *env)
{
	const uint64_t t0 = k1_now(env);
	const struct k1_ddrt_cfg mc = { .ddr = ddr, .cs_num = cs_num };
	volatile uint32_t *mem0;
	uint32_t bad = 0;
	int mr14_rc;

	if (cs_num < 1)
		cs_num = 1;
	mem0 = (volatile uint32_t *)k1_eye_rank_buf(ddr, 0);
#ifdef K1_DDRT_MOCK
	k1_mock_fp = fp;
#endif
	k1_log(env, "indep: eye training at fp%u, %u rank(s)\n", fp, cs_num);

	for (uint32_t cs = 0; cs < cs_num; cs++) {
		volatile uint32_t *mem =
			(volatile uint32_t *)k1_eye_rank_buf(ddr, cs);

		k1_eye_train_wl(mem, env, ddr, fp, cs);
	}
	k1_eye_train_rx_vref(mem0, env, ddr, fp);
	k1_eye_train_tx_vref(mem0, env, ddr, cs_num, fp);

	for (uint32_t cs = 0; cs < cs_num; cs++) {
		volatile uint32_t *mem =
			(volatile uint32_t *)k1_eye_rank_buf(ddr, cs);

		bad += k1_eye_train_rank(mem, env, ddr, fp, cs);
	}

	mr14_rc = k1_ddrt_check_mr14(&mc, env);
	k1_log(env,
	       "indep: eye training done in %u us, verify %s (%u rank-fails), MR14 check rc %d\n",
	       (uint32_t)(k1_now(env) - t0), bad ? "FAIL" : "OK", bad, mr14_rc);
	if (bad)
		return K1_DDRT_EIO;
	return mr14_rc;
}
