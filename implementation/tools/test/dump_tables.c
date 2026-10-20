// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Thalles Campagnani
/*
 * Prints the silicon tables of the bring-up, one fact per line, in the
 * format test/check_tables.py builds from the documentation.
 */
#include <stdio.h>

#include "k1_ddr_boot.h"
#include "../lib/k1_ddr_init.h"
#include "../lib/k1_ddr_tables.h"
#include "../lib/k1_ddrt_regs.h"

static void dump_io(const char *name, const struct k1_io_cfg *io)
{
	printf("io %s ca_vref 0x%x\n", name, io->ca_vref);
	printf("io %s tx_vref 0x%x\n", name, io->tx_vref);
	printf("io %s soc_odt 0x%x\n", name, io->soc_odt);
	printf("io %s tx_odt 0x%x\n", name, io->tx_odt);
	printf("io %s rx_drv 0x%x\n", name, io->rx_drv);
	printf("io %s tx_pn 0x%x\n", name, (io->tx_pdrv << 5) | (io->tx_ndrv << 2));
	printf("io %s rx_odt 0x%x\n", name, io->rx_odt);
	printf("io %s rx_vref 0x%x\n", name, io->rx_vref);
	printf("io %s voh 0x%x\n", name, io->rx_pu_cal);
}

static void dump_seq(const char *name, const struct k1_tbl_entry *t, size_t n)
{
	for (size_t i = 0; i < n; i++)
		printf("%s 0x%x 0x%x 0x%x\n", name, t[i].idx, t[i].data, t[i].addr);
}

int main(void)
{
	static const uint32_t rates[] = {
		1200, 1066, 2133, 2400, 2666, 1600, 2700, 2800, 2933, 3000, 3200,
	};
	static const uint32_t dens_gb[] = { 2, 3, 4, 6, 8, 12, 16, 0, 0, 0, 0, 0, 1 };

	for (size_t i = 0; i < K1_N_MC_GLOBALS; i++)
		printf("global 0x%x 0x%x\n", k1_mc_globals[i].off, k1_mc_globals[i].val);
	for (uint32_t fp = 0; fp < 4; fp++)
		printf("freq %u 0x%x\n", fp, k1_freq_bank(fp));
	for (size_t i = 0; i < K1_N_MC_TIMING; i++) {
		const struct k1_bank_row *r = &k1_mc_timing[i];

		if (r->io)
			printf("timing 0x%x io\n", r->off);
		else
			printf("timing 0x%x 0x%x 0x%x 0x%x 0x%x\n", r->off,
			       r->fp[3], r->fp[2], r->fp[1], r->fp[0]);
	}
	for (size_t i = 0; i < 3; i++)
		printf("refresh 0x%x 0x%x\n", k1_mc_refresh[i].off, k1_mc_refresh[i].val);
	for (uint32_t fp = 0; fp < 4; fp++)
		printf("wdqs %u 0x%x\n", fp, k1_mc_wdqs[fp]);
	for (size_t i = 0; i < K1_N_X8_OVERLAY; i++)
		for (uint32_t fp = 0; fp < 4; fp++)
			printf("overlay %u 0x%x 0x%x\n", fp, k1_mc_x8_overlay[i].off,
			       k1_mc_x8_overlay[i].fp[fp]);
	for (uint32_t fp = 0; fp < 4; fp++)
		printf("clksel %u 0x%x\n", fp, k1_clksel[fp]);
	for (uint32_t fp = 0; fp < 4; fp++)
		printf("hwslp %u 0x%x\n", fp,
		       K1_HW_SLP_DFC | (fp << K1_HW_SLP_FP_SHIFT));
	printf("swfc_slp 0x%x\n", K1_HW_SLP_SWFC);
	printf("pll 2666 0x%x 0x%x\n", K1_PLL1_2666, K1_PLL1_ANALOG);
	printf("pll 2400 0x%x 0x%x\n", K1_PLL1_2400, K1_PLL1_BOOTROM_ANALOG);
	printf("pll pll2 0x%x 0x%x\n", K1_PLL2_VCO, K1_PLL2_ANALOG);
	dump_io("lpddr4x", &k1_io_lpddr4x);
	dump_io("lpddr4", &k1_io_lpddr4);

	printf("mr_init1");
	for (size_t i = 0; i < K1_N_MR_INIT1; i++)
		printf(" %u", k1_mr_init1[i]);
	printf("\nmr_init2");
	for (size_t i = 0; i < K1_N_MR_INIT2; i++)
		printf(" %u", k1_mr_init2[i]);
	printf("\nmr21 0x%x\n", K1_CA_MR_WRITE | (3u << K1_CS_SEL_SHIFT) | K1_MR21_16G);

	dump_seq("flow", k1_dfc_flow, K1_N_DFC_FLOW);
	dump_seq("exit", k1_dfc_exit, K1_N_DFC_EXIT);
	printf("mr21_after flow 0x%x 0x%x\n", K1_DFC_FLOW_MR21_AFTER, K1_DFC_FLOW_RUN_END);
	printf("mr21_after exit 0x%x 0x%x\n", K1_DFC_EXIT_MR21_AFTER, K1_DFC_EXIT_RUN_END);
	for (size_t i = 0; i < K1_N_SNAP_GLOBALS; i++)
		printf("snapg %zu 0x%x\n", i, k1_dfc_snap_globals[i]);
	for (size_t i = 0; i < K1_N_SNAP_BANK; i++)
		printf("snapb %zu 0x%x\n", i, k1_dfc_snap_bank[i]);
	printf("snap_ctl0_bits 0x%x\n", K1_TBL_CTL0_BITS);
	printf("snap_first 0x%x\n", K1_MC_CTL0 | K1_TBL_REQ_PMU);
	printf("snap_end 0x%x 0x%x\n", K1_TBL_SNAP_END, K1_MC_CTL0 | K1_TBL_EOP);
	printf("tbl_flag POLL 0x%x\ntbl_flag REQ_PMU 0x%x\ntbl_flag EOP 0x%x\n",
	       K1_TBL_POLL, K1_TBL_REQ_PMU, K1_TBL_EOP);
	for (uint32_t fp = 0; fp <= K1_MAX_PP; fp++)
		printf("snap_bank %u 0x%x 0x%zx\n", fp,
		       (k1_freq_bank(fp) & ~K1_FREQ_SEL_MASK) >> 28,
		       K1_TBL_SNAPSHOT + 1 + K1_N_SNAP_GLOBALS + fp * K1_N_SNAP_BANK);
	dump_seq("snapt", k1_dfc_snap_tail, K1_N_SNAP_TAIL);
	for (size_t i = 0; i < K1_N_DFC_MARKS; i++)
		printf("mark %zu 0x%x 0x%x 0x%x\n", i, k1_dfc_marks[i][0],
		       k1_dfc_marks[i][1], k1_dfc_marks[i][2]);
	printf("dfc_pre 0x%x\n", K1_DFC_PRE_VAL);

	for (uint32_t mb = 256; mb <= 16384; mb += 256)
		if (k1_ddr_size_code(mb))
			printf("size 0x%x %u\n", k1_ddr_size_code(mb), mb);
	for (size_t i = 0; i < sizeof(rates) / sizeof(rates[0]); i++) {
		printf("rate lpddr4x %u %u\n", rates[i],
		       k1_ddr_final_fp(K1_DDR_LPDDR4X, rates[i]));
		printf("rate lpddr4 %u %u\n", rates[i],
		       k1_ddr_final_fp(K1_DDR_LPDDR4, rates[i]));
	}
	for (uint32_t io = 0; io < 2; io++) {
		for (uint32_t dens = 0; dens < sizeof(dens_gb) / sizeof(dens_gb[0]); dens++) {
			struct k1_ddr_geom g;

			if (!dens_gb[dens])
				continue;
			if (k1_ddr_geometry((uint8_t)(io << 6 | dens << 2), &g))
				printf("geom x%u %u none\n", io ? 8 : 16, dens_gb[dens]);
			else
				printf("geom x%u %u %u 0x%x %u\n", io ? 8 : 16,
				       dens_gb[dens], g.area_mb, g.code, g.row);
		}
	}
	return 0;
}
