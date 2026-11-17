// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Thalles Campagnani
#include "k1_ddr_accept.h"
#include "k1_ddrt_priv.h"
#include "k1_ddrt_regs.h"

static bool in_range(uint32_t v, uint32_t lo, uint32_t hi)
{
	return v >= lo && v <= hi;
}

static void print_lanes(const struct k1_ddrt_env *env, const char *name,
			const uint8_t v[4][8])
{
	k1_log(env, "    %-8s", name);
	for (uint32_t b = 0; b < 4; b++)
		k1_log(env, " b%u:%2u", b, v[b][0]);
	k1_log(env, "\n");
}

uint32_t k1_ddr_accept_final(const struct k1_ddrt_env *env,
			     const struct k1_ddrt_result *r,
			     uint32_t cs_num, int mr14_rc)
{
	uint32_t bad = 0;

	for (uint32_t cs0 = 0; cs0 < cs_num; cs0++) {
		for (uint32_t b = 0; b < 4; b++) {
			const uint32_t g = r->gate[cs0][b];

			if (!in_range(r->wl[cs0][b], 17, 23)) {
				k1_log(env, "  out of range: cs%u b%u WL %u (17..23)\n",
				       cs0, b, r->wl[cs0][b]);
				bad++;
			}
			/* Rx training retunes the low byte: equal only right after the gate op */
			if (!(g & (1u << 16)) || !in_range((g >> 8) & 0xff, 33, 40) ||
			    !in_range(g & 0xff, 33, 40)) {
				k1_log(env, "  out of range: cs%u b%u gate 0x%08x (0x0001ggtg, 33..40)\n",
				       cs0, b, g);
				bad++;
			}
			if (!in_range(r->rx_dq[cs0][b][0], 17, 19)) {
				k1_log(env, "  out of range: cs%u b%u Rx DQ0 %u (17..19)\n",
				       cs0, b, r->rx_dq[cs0][b][0]);
				bad++;
			}
			if (!in_range(r->tx_dq[cs0][b][0], 30, 36)) {
				k1_log(env, "  out of range: cs%u b%u Tx DQ0 %u (30..36)\n",
				       cs0, b, r->tx_dq[cs0][b][0]);
				bad++;
			}
		}
	}
	if (r->phy_status) {
		k1_log(env, "  phy_status 0x%x != 0\n", r->phy_status);
		bad++;
	}
	if (mr14_rc) {
		k1_log(env, "  MR14 != CTL_TXVREF[21:16] (rc %d)\n", mr14_rc);
		bad++;
	}
	return bad;
}

uint32_t k1_ddr_accept_pp2_2400(const struct k1_ddrt_env *env,
				const struct k1_ddrt_result *r)
{
	const uint32_t rx = r->rx_min[r->rx_vref_idx];
	const uint32_t tx = in_range(r->tx_vref_dq, K1_TX_VREF_SEED0,
				     K1_TX_VREF_SEED0 + 15) ?
			    r->tx_min[r->tx_vref_dq - K1_TX_VREF_SEED0] : 0;
	uint32_t bad = 0;

	if (!in_range(r->rx_vref_idx, 4, 6) || rx < 10) {
		k1_log(env, "  out of range: first pp2 Rx idx %u min %u (4..6, min 10)\n",
		       r->rx_vref_idx, rx);
		bad++;
	}
	if (!in_range(r->tx_vref_dq, 0x1d, 0x1f) || tx < 10) {
		k1_log(env, "  out of range: first pp2 Tx MR14 %u min %u (29..31, min 10)\n",
		       r->tx_vref_dq, tx);
		bad++;
	}
	if (bad)
		k1_log(env, "  at 2400 a min of 8/9 is the 2666 CK: check PLL1 (doc 25)\n");
	return bad;
}

uint32_t k1_ddr_accept_pp0b_gate(const struct k1_ddrt_env *env,
				 const struct k1_ddrt_result *r,
				 uint32_t cs_num)
{
	uint32_t bad = 0;

	for (uint32_t cs0 = 0; cs0 < cs_num; cs0++)
		for (uint32_t b = 0; b < 4; b++) {
			const uint32_t g = r->gate[cs0][b];

			if (!in_range((g >> 8) & 0xff, 32, 40)) {
				k1_log(env, "  second pp0: cs%u b%u gate 0x%08x not ~32..40\n",
				       cs0, b, g);
				bad++;
			}
		}
	return bad;
}

void k1_ddr_print_result(const struct k1_ddrt_env *env, const char *label,
			 const struct k1_ddrt_result *r, uint32_t cs_num)
{
	k1_log(env, "  %s: vref0 0x%08x vref1 0x%08x txvref 0x%08x (mr14 field %u) phy_status 0x%x pi_status 0x%08x\n",
	       label, r->vref0, r->vref1, r->txvref,
	       (r->txvref >> K1_TXVREF_SHIFT) & K1_TXVREF_FIELD, r->phy_status,
	       r->pi_status);
	for (uint32_t cs0 = 0; cs0 < cs_num; cs0++) {
		k1_log(env, "   cs%u wl   ", cs0);
		for (uint32_t b = 0; b < 4; b++)
			k1_log(env, " %3u", r->wl[cs0][b]);
		k1_log(env, "   gate");
		for (uint32_t b = 0; b < 4; b++)
			k1_log(env, " 0x%08x", r->gate[cs0][b]);
		k1_log(env, "\n");
		print_lanes(env, "rx dq0", r->rx_dq[cs0]);
		print_lanes(env, "tx dq0", r->tx_dq[cs0]);
		k1_log(env, "    tx dm   ");
		for (uint32_t b = 0; b < 4; b++)
			k1_log(env, " b%u:%2u", b, r->tx_dm[cs0][b]);
		k1_log(env, "\n");
	}
}

void k1_ddr_print_pass_stats(const struct k1_ddrt_env *env,
			     const struct k1_ddrt_result *r, uint32_t cs_num)
{
	static const char *const names[] = { "wl", "gate", "rx", "tx" };

	k1_log(env, "  sr_stat before 0x%02x after-enter 0x%02x after-exit 0x%02x\n",
	       r->sr_stat[0], r->sr_stat[1], r->sr_stat[2]);
	for (uint32_t i = 0; i < 4; i++)
		k1_log(env, "  pi %-4s ops %u stale-first %u max-polls %u max-us %u stage-us %u\n",
		       names[i], r->pi[i].ops, r->pi[i].stale_first,
		       r->pi[i].max_polls, r->pi[i].max_us, r->stage_us[i]);
	for (uint32_t cs0 = 0; cs0 < cs_num; cs0++) {
		k1_log(env, "  rx min cs%u", cs0 + 1);
		for (uint32_t s = 0; s < 16; s++)
			k1_log(env, " %2u", r->rx_min_cs[cs0][s]);
		k1_log(env, "\n  tx min cs%u", cs0 + 1);
		for (uint32_t s = 0; s < 16; s++)
			k1_log(env, " %2u", r->tx_min_cs[cs0][s]);
		k1_log(env, "\n");
	}
}

void k1_ddr_print_mrs(const struct k1_ddrt_env *env,
		      const struct k1_ddrt_cfg *cfg)
{
	static const uint8_t mrs[] = { 5, 6, 8, 12, 14 };

	for (uint32_t cs = 0; cs < cfg->cs_num; cs++) {
		for (uint32_t ch = 0; ch < 2; ch++) {
			k1_log(env, "  cs%u ch%u", cs, ch);
			for (uint32_t i = 0; i < sizeof(mrs); i++) {
				uint8_t v = 0;
				const int rc = k1_ddrt_mr_read(cfg, env, cs, ch,
							       mrs[i], &v);

				if (rc)
					k1_log(env, " MR%u:err%d", mrs[i], rc);
				else
					k1_log(env, " MR%u:0x%02x", mrs[i], v);
			}
			k1_log(env, "\n");
		}
	}
}
