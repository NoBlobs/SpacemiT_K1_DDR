// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Thalles Campagnani
/*
 * SpacemiT K1 DDR controller bring-up: PLL and SW-FC (doc 25), Device_Init
 * with PHY analog and mode registers (26, 27, 28), address map (29), DFC
 * table and hardware DFC (30), byte-mode re-init (31).
 */
#include "k1_ddr_init.h"
#include "k1_ddrt_io.h"
#include "k1_ddrt_priv.h"
#include "k1_ddrt_regs.h"

static constexpr uint32_t K1_LOCK_BUDGET_US = 1'000;
static constexpr uint32_t K1_FC_BUDGET_US   = 10'000;	/* HW-DFC done in < 1 ms */
static constexpr uint32_t K1_SR_BUDGET_US   = 10'000;
static constexpr uint32_t K1_INIT_BUDGET_US = 10'000;
static constexpr uint32_t K1_MC_RESET_US    = 100;
static constexpr uint32_t K1_ZQ_START_US    = 2;
static constexpr uint32_t K1_ZQ_LATCH_US    = 1;
static constexpr uint32_t K1_MRW_US         = 1;

/* IO words of the timing banks (doc 26) */
static constexpr uint32_t K1_IO_TXVREF_SEED = 0x4314;
static constexpr uint32_t K1_IO_TXVREF_KEEP = 0x8fff;
static constexpr uint32_t K1_IO_DS_SEED     = 0x0044'0000;
static constexpr uint32_t K1_IO_DS_KEEP     = 0xff80'ffff;
static constexpr uint32_t K1_IO_PU_SHIFT    = 17;
static constexpr uint32_t K1_FREQ_AUX_KEEP  = 0xf00f'ffff;
static constexpr uint32_t K1_FREQ_AUX_VAL   = 0x10u << 20;

/* COMMON words, offsets from C0 / C1 (doc 27) */
static constexpr uint32_t K1_COM_ARM     = 0x00;
static constexpr uint32_t K1_COM_VREF    = 0x04;
static constexpr uint32_t K1_COM_DS      = 0x0c;
static constexpr uint32_t K1_COM_10      = 0x10;
static constexpr uint32_t K1_COM_14      = 0x14;
static constexpr uint32_t K1_COM_PRE_KEEP = 0xffff'0fff;
static constexpr uint32_t K1_COM_PRE_BITS = (1u << 11) | (1u << 13) | (1u << 15);
static constexpr uint32_t K1_COM_TXDS_KEEP = 0xffff'00ff;
static constexpr uint32_t K1_COM_RXODT_KEEP = 0xffc0'ffff;
static constexpr uint32_t K1_COM_RXVREF_KEEP = 0x0000'ffff;
static constexpr uint32_t K1_COM_14_KEEP = 0xff9f'ffef;
static constexpr uint32_t K1_COM_14_VAL  = 3u << 21;
static constexpr uint32_t K1_COM_10_BIT  = 0x1000'0000;
static constexpr uint32_t K1_COM_A0_30_VAL = 0x1077;
static constexpr uint32_t K1_DQ_CS0B0_VAL  = 0x4349;
static constexpr uint32_t K1_PHY_INIT_POR  = 0x100;

/* DFC table: commands whose top byte carries the CS selection */
static constexpr uint32_t K1_TBL_CS_BYTE_2CS = 0x13;

static uintptr_t k1_mc(const struct k1_ddr_hw *hw, uint32_t off)
{
	return hw->ddr + off;
}

static uintptr_t k1_pmu(const struct k1_ddr_hw *hw, uint32_t off)
{
	return hw->pmu + off;
}

static uint32_t k1_cs_field(const struct k1_ddr_hw *hw)
{
	return hw->cs_sel << K1_CS_SEL_SHIFT;
}

struct k1_ddr_hw k1_ddr_hw_init(const struct k1_ddr_boot_cfg *cfg,
				const struct k1_ddrt_env *env)
{
	return (struct k1_ddr_hw){
		.ddr = cfg->ddr,
		.phy = cfg->ddr + K1_PHY_OFF,
		.pmu = cfg->pmu,
		.win = cfg->dfc_win,
		.cs_num = cfg->cs_num,
		.cs_sel = k1_cs_sel(cfg->cs_num),
		.type = cfg->type,
		.io = cfg->type == K1_DDR_LPDDR4 ? &k1_io_lpddr4 : &k1_io_lpddr4x,
		.env = env,
	};
}

int k1_ddr_pll_init(const struct k1_ddr_hw *hw, uint32_t data_rate,
		    enum k1_ddr_step *step)
{
	const struct k1_ddrt_env *env = hw->env;
	uint32_t v = 0;

	/* reset values are not established: logged for the record only */
	k1_log(env, "k1-ddr: pmu before: cc_ap 0x%08x hw_slp 0x%08x clksel 0x%08x pll1 0x%08x 0x%08x mck 0x%08x\n",
	       k1_r32(k1_pmu(hw, K1_PMU_CC_AP)), k1_r32(k1_pmu(hw, K1_PMU_HW_SLP)),
	       k1_r32(k1_pmu(hw, K1_PMU_CLKSEL)),
	       k1_r32(k1_pmu(hw, K1_PMU_PLL1_CTL1)),
	       k1_r32(k1_pmu(hw, K1_PMU_PLL1_CTL2)),
	       k1_r32(k1_pmu(hw, K1_PMU_MCK_CTRL)));

	/* 2666 and 1333: complete pair (fp0 = PLL1/2 ≈1333). 2400: RMW leftover
	 * [15:8]=0x3B, leave +0x3a0. */
	if (data_rate == 2666 || data_rate == 1333) {
		k1_w32(k1_pmu(hw, K1_PMU_PLL1_CTL1), K1_PLL1_2666);
		k1_w32(k1_pmu(hw, K1_PMU_PLL1_CTL2), K1_PLL1_ANALOG);
	} else {
		k1_rmw32(k1_pmu(hw, K1_PMU_PLL1_CTL1), K1_PLL1_2400_KEEP,
			 K1_PLL1_2400_BYTE << 8);
	}
	k1_w32(k1_pmu(hw, K1_PMU_PLL2_CTL1), K1_PLL2_VCO);
	k1_w32(k1_pmu(hw, K1_PMU_PLL2_CTL2), K1_PLL2_ANALOG);

	k1_rmw32(k1_pmu(hw, K1_PMU_CLKSEL),
		 ~(K1_CLKSEL_PLL2_EN | K1_CLKSEL_PLL1_EN), 0);
#ifndef K1_EXP_PLL_NO_LOCK
	k1_rmw32(k1_pmu(hw, K1_PMU_CLKSEL), ~0u,
		 K1_CLKSEL_PLL2_EN | K1_CLKSEL_PLL1_EN | K1_CLKSEL_EXTRA_EN);
#else
	k1_log(env, "k1-ddr: EXP PLL_NO_LOCK: leaving PLL1/PLL2 disabled\n");
#endif
	if (k1_wait(env, k1_pmu(hw, K1_PMU_CLKSEL), K1_CLKSEL_LOCK,
		    K1_CLKSEL_LOCK, K1_LOCK_BUDGET_US, &v)) {
		k1_log(env, "k1-ddr: PLL1/PLL2 not locked, clksel 0x%08x\n", v);
		*step = K1_STEP_PLL;
		return K1_DDRT_ETIMEDOUT;
	}

	k1_w32(k1_pmu(hw, K1_PMU_CLKSEL), k1_clksel[0]);
	k1_w32(k1_pmu(hw, K1_PMU_HW_SLP), K1_HW_SLP_SWFC);
	k1_rmw32(k1_pmu(hw, K1_PMU_CC_AP), ~0u, K1_CC_SWFC_KICK);
	if (k1_wait(env, k1_pmu(hw, K1_PMU_CC_AP), K1_CC_SWFC_KICK, 0,
		    K1_FC_BUDGET_US, &v)) {
		k1_log(env, "k1-ddr: SW-FC to fp0 did not finish, cc_ap 0x%08x\n",
		       v);
		*step = K1_STEP_SWFC;
		return K1_DDRT_ETIMEDOUT;
	}
	k1_rmw32(k1_pmu(hw, K1_PMU_MCK_CTRL), ~K1_MCK_ON, K1_MCK_ON);

	k1_log(env, "k1-ddr: PLL locked, fp0 up: clksel 0x%08x hw_slp 0x%08x mck 0x%08x\n",
	       k1_r32(k1_pmu(hw, K1_PMU_CLKSEL)),
	       k1_r32(k1_pmu(hw, K1_PMU_HW_SLP)),
	       k1_r32(k1_pmu(hw, K1_PMU_MCK_CTRL)));
	k1_log(env, "k1-ddr: pll1 0x%08x 0x%08x\n",
	       k1_r32(k1_pmu(hw, K1_PMU_PLL1_CTL1)),
	       k1_r32(k1_pmu(hw, K1_PMU_PLL1_CTL2)));
	return K1_DDRT_OK;
}

void k1_ddr_mr_write(const struct k1_ddr_hw *hw, uint32_t mr)
{
	/* no done bit for writes: tMRW between commands */
	k1_w32(k1_mc(hw, K1_MC_CA_CMD),
	       K1_CA_MR_WRITE | k1_cs_field(hw) | (mr & 0xff));
	k1_udelay(hw->env, K1_MRW_US);
}

static void k1_write_io(const struct k1_ddr_hw *hw, uint32_t off)
{
	const struct k1_io_cfg *io = hw->io;
	const uintptr_t a = k1_mc(hw, off);

	if (off == K1_MC_TXVREF) {
		k1_w32(a, K1_IO_TXVREF_SEED);
		k1_rmw32(a, K1_IO_TXVREF_KEEP,
			 ((uint32_t)io->ca_vref << 24) |
			 ((uint32_t)io->tx_vref << 16) |
			 ((uint32_t)io->soc_odt << 12));
	} else {
		k1_w32(a, K1_IO_DS_SEED);
		k1_rmw32(a, K1_IO_DS_KEEP,
			 ((uint32_t)io->tx_odt << 20) |
			 ((uint32_t)io->rx_drv << 16));
	}
}

static int k1_mc_program(const struct k1_ddr_hw *hw, bool x8)
{
	for (size_t i = 0; i < K1_N_MC_GLOBALS; i++)
		k1_w32(k1_mc(hw, k1_mc_globals[i].off), k1_mc_globals[i].val);
	/* a controller without clock or still in reset drops writes */
	const uint32_t ctl0 = k1_r32(k1_mc(hw, K1_MC_CTL0));
	if (ctl0 != k1_mc_globals[0].val) {
		k1_log(hw->env, "k1-ddr: CTL0 reads 0x%08x after writing 0x%08x\n",
		       ctl0, k1_mc_globals[0].val);
		return K1_DDRT_EIO;
	}
	k1_rmw32(k1_mc(hw, K1_MC_IO_PU), ~(1u << K1_IO_PU_SHIFT),
		 (uint32_t)hw->io->rx_pu_cal << K1_IO_PU_SHIFT);

	for (uint32_t n = 4; n-- > 0;) {
		k1_w32(k1_mc(hw, K1_MC_FREQ), k1_freq_bank(n));
		for (size_t i = 0; i < K1_N_MC_TIMING; i++) {
			const struct k1_bank_row *row = &k1_mc_timing[i];

			if (row->io)
				k1_write_io(hw, row->off);
			else
				k1_w32(k1_mc(hw, row->off), row->fp[n]);
		}
		if (hw->type == K1_DDR_LPDDR4)
			k1_w32(k1_mc(hw, K1_MC_WDQS), k1_mc_wdqs[n]);
	}
	if (x8) {
		for (uint32_t n = 4; n-- > 0;) {
			k1_w32(k1_mc(hw, K1_MC_FREQ), k1_freq_bank(n));
			for (size_t i = 0; i < K1_N_X8_OVERLAY; i++)
				k1_w32(k1_mc(hw, k1_mc_x8_overlay[i].off),
				       k1_mc_x8_overlay[i].fp[n]);
		}
	}

	k1_rmw32(k1_mc(hw, K1_MC_FREQ_AUX), K1_FREQ_AUX_KEEP, K1_FREQ_AUX_VAL);
	k1_rmw32(k1_mc(hw, K1_MC_FREQ), ~K1_FREQ_FP_MASK, 0);
	k1_rmw32(k1_mc(hw, K1_MC_PI_CFG), ~0u, 0u << K1_PI_CFG_SEL_SHIFT);
	for (size_t i = 0; i < 3; i++)
		k1_w32(k1_mc(hw, k1_mc_refresh[i].off), k1_mc_refresh[i].val);
	return K1_DDRT_OK;
}

static void k1_phy_analog(const struct k1_ddr_hw *hw)
{
	const struct k1_io_cfg *io = hw->io;
	const uint32_t d2 = ((uint32_t)io->tx_pdrv << 5) | ((uint32_t)io->tx_ndrv << 2);
	const uint32_t d3 = ((uint32_t)io->rx_odt << 3) | io->rx_odt;
	const uint32_t rxv = ((uint32_t)io->rx_vref << 16) | ((uint32_t)io->rx_vref << 24);

	k1_rmw32(k1_pmu(hw, K1_PMU_PHY_BIAS), K1_PHY_BIAS_KEEP, K1_PHY_BIAS_VAL);
	if (hw->type == K1_DDR_LPDDR4) {
		k1_rmw32(k1_pmu(hw, K1_PMU_PHY_LDO), ~0u, K1_LDO_4);
		k1_rmw32(k1_pmu(hw, K1_PMU_PHY_LDO), K1_LDO_4_KEEP, K1_LDO_4_088V);
	} else {
		k1_rmw32(k1_pmu(hw, K1_PMU_PHY_LDO), ~0u, K1_LDO_4X);
	}

	for (uint32_t n = 0; n <= K1_MAX_PP; n++) {
		const uintptr_t s = hw->phy + (n << K1_PHY_SLICE_SHIFT);
		const uintptr_t c[2] = { s + K1_SLICE_COMMON0, s + K1_SLICE_COMMON1 };

		k1_w32(c[0] + K1_COM_ARM, 0);
		k1_w32(c[1] + K1_COM_ARM, 0);
		k1_w32(c[0] + K1_COM_ARM, 1);
		k1_w32(c[1] + K1_COM_ARM, 1);
		for (uint32_t i = 0; i < 2; i++) {
			k1_rmw32(c[i] + K1_COM_VREF, K1_COM_PRE_KEEP, K1_COM_PRE_BITS);
			k1_rmw32(c[i] + K1_COM_DS, K1_COM_TXDS_KEEP, d2 << 8);
			k1_rmw32(c[i] + K1_COM_DS, K1_COM_RXODT_KEEP, d3 << 16);
			k1_rmw32(c[i] + K1_COM_VREF, K1_COM_RXVREF_KEEP, rxv);
			k1_rmw32(c[i] + K1_COM_14, K1_COM_14_KEEP, K1_COM_14_VAL);
			k1_rmw32(c[i] + K1_COM_10, ~0u, K1_COM_10_BIT);
		}
	}
	k1_w32(hw->phy + K1_PHY_COMMON_A0_30, K1_COM_A0_30_VAL);
	for (uint32_t n = 0; n <= K1_MAX_PP; n++)
		k1_w32(hw->phy + (n << K1_PHY_SLICE_SHIFT) + K1_SLICE_DQ_CS0B0,
		       K1_DQ_CS0B0_VAL);
	k1_w32(hw->phy + K1_PHY_OTHER_24, 0);
	k1_rmw32(hw->phy + K1_PHY_OTHER, ~0u, 1);
}

static int k1_dram_start(const struct k1_ddr_hw *hw, enum k1_ddr_step *step)
{
	const struct k1_ddrt_env *env = hw->env;
	const uint32_t chk = hw->cs_num == 1 ? K1_INIT_DONE_1CS : K1_INIT_DONE_2CS;
	uint32_t v = 0;

	k1_w32(k1_mc(hw, K1_MC_PI_CMD), K1_DFI_INIT_START | k1_cs_field(hw));
	if (k1_wait(env, k1_mc(hw, K1_MC_PI_STATUS), K1_PI_INIT_DONE,
		    K1_PI_INIT_DONE, K1_INIT_BUDGET_US, &v)) {
		k1_log(env, "k1-ddr: DFI init_start not done, pi_status 0x%08x\n",
		       v);
		*step = K1_STEP_DFI_INIT;
		return K1_DDRT_ETIMEDOUT;
	}
	k1_w32(k1_mc(hw, K1_MC_PI_CMD), K1_DFI_INIT_CLEAR | k1_cs_field(hw));

	k1_w32(k1_mc(hw, K1_MC_USER0), K1_USER0_INIT | k1_cs_field(hw));
	if (k1_wait(env, k1_mc(hw, K1_MC_STATUS), chk, chk, K1_INIT_BUDGET_US,
		    &v)) {
		k1_log(env, "k1-ddr: DRAM init not done, status 0x%02x\n", v);
		*step = K1_STEP_DRAM_INIT;
		return K1_DDRT_ETIMEDOUT;
	}

	for (size_t i = 0; i < K1_N_MR_INIT1; i++)
		k1_ddr_mr_write(hw, k1_mr_init1[i]);
	for (uint32_t cs0 = 0; cs0 < hw->cs_num; cs0++) {
		const uint32_t sel = (1u << cs0) << K1_CS_SEL_SHIFT;

		k1_w32(k1_mc(hw, K1_MC_USER0), K1_USER0_ZQ_START | sel);
		k1_udelay(env, K1_ZQ_START_US);
		k1_w32(k1_mc(hw, K1_MC_USER0), K1_USER0_ZQ_LATCH | sel);
		k1_udelay(env, K1_ZQ_LATCH_US);
	}
	for (size_t i = 0; i < K1_N_MR_INIT2; i++)
		k1_ddr_mr_write(hw, k1_mr_init2[i]);
	return K1_DDRT_OK;
}

int k1_ddr_device_init(const struct k1_ddr_hw *hw, bool x8,
		       enum k1_ddr_step *step)
{
	const struct k1_ddrt_env *env = hw->env;
	int rc;

	rc = k1_mc_program(hw, x8);
	if (rc) {
		*step = K1_STEP_MC;
		return rc;
	}
	k1_phy_analog(hw);
	rc = k1_dram_start(hw, step);
	if (rc)
		return rc;

	const uint32_t freq = k1_r32(k1_mc(hw, K1_MC_FREQ));
	const uint32_t pi_cfg = k1_r32(k1_mc(hw, K1_MC_PI_CFG));
	const uint32_t phy_init = k1_r32(hw->phy + K1_PHY_INIT);

	k1_log(env, "k1-ddr: device init%s: status 0x%02x ctl_freq 0x%08x pi_cfg 0x%x phy_init 0x%x pi_status 0x%08x common 0x%08x\n",
	       x8 ? " x8" : "", k1_r32(k1_mc(hw, K1_MC_STATUS)), freq, pi_cfg,
	       phy_init, k1_r32(k1_mc(hw, K1_MC_PI_STATUS)),
	       k1_r32(hw->phy + K1_SLICE_COMMON0 + K1_COM_DS));
	if (freq != k1_freq_bank(0) || (pi_cfg & K1_PI_CFG_SEL_MASK)) {
		*step = K1_STEP_MC;
		return K1_DDRT_EIO;
	}
	if (phy_init != K1_PHY_INIT_POR)
		k1_log(env, "k1-ddr: warning: phy_init 0x%x, expected 0x%x\n",
		       phy_init, K1_PHY_INIT_POR);
	return K1_DDRT_OK;
}

void k1_ddr_mc_reset(const struct k1_ddr_hw *hw)
{
	k1_rmw32(k1_pmu(hw, K1_PMU_HW_SLP), ~K1_HW_SLP_RESET, 0);
	k1_udelay(hw->env, K1_MC_RESET_US);
	k1_rmw32(k1_pmu(hw, K1_PMU_HW_SLP), ~0u, K1_HW_SLP_RESET);
	k1_udelay(hw->env, K1_MC_RESET_US);
}

uint32_t k1_ddr_size_code(uint32_t mb)
{
	switch (mb) {
	case 512:	return 0x0d;
	case 768:	return 0x01;
	case 1024:	return 0x0e;
	case 1536:	return 0x02;
	case 2048:	return 0x0f;
	case 3072:	return 0x03;
	case 4096:	return 0x10;
	case 8192:	return 0x11;
	case 16384:	return 0x12;
	default:	return 0;
	}
}

int k1_ddr_geometry(uint8_t mr8, struct k1_ddr_geom *g)
{
	/* x16 per JEDEC channel; 1 Gb has a row count but no listed size */
	static const struct {
		uint8_t dens;
		uint8_t row;
		uint16_t ch_mb;
	} t[] = {
		{ 0, 14, 256 }, { 1, 15, 384 }, { 2, 15, 512 }, { 3, 16, 768 },
		{ 4, 16, 1024 }, { 5, 17, 1536 }, { 6, 17, 2048 },
	};
	const uint32_t io = mr8 >> 6;
	const uint32_t dens = (mr8 >> 2) & 0xf;

	if (io > 1)
		return K1_DDRT_EINVAL;
	for (size_t i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
		if (t[i].dens != dens)
			continue;
		const uint32_t area = 2u * ((uint32_t)t[i].ch_mb << io);
		const uint32_t code = k1_ddr_size_code(area);

		if (!code)
			return K1_DDRT_EINVAL;
		*g = (struct k1_ddr_geom){
			.area_mb = area,
			.code = code,
			.row = t[i].row + io,
			.x8 = io == 1,
		};
		return K1_DDRT_OK;
	}
	return K1_DDRT_EINVAL;
}

void k1_ddr_map_apply(const struct k1_ddr_hw *hw, const struct k1_ddr_geom *g)
{
	const uint32_t start = g->area_mb >> 3;
	const uint32_t code = g->code << K1_MAP_CODE_SHIFT;
	const uint32_t geom = ((g->row - 10) << K1_GEOM_ROW_SHIFT) |
			      K1_GEOM_COL10 | K1_GEOM_BANK8;

	k1_rmw32(k1_mc(hw, K1_MC_CS0_MAP), K1_MAP_KEEP, code | K1_MAP_PRESENT);
	k1_rmw32(k1_mc(hw, K1_MC_CS1_MAP), K1_MAP_KEEP & ~K1_MAP_PRESENT,
		 ((start & K1_MAP_START_LO) << K1_MAP_START_SHIFT) | code |
		 (hw->cs_num == 2 ? K1_MAP_PRESENT : 0));
	k1_w32(k1_mc(hw, K1_MC_CS1_HI), start >> 9);
	k1_rmw32(k1_mc(hw, K1_MC_CS0_GEOM), K1_GEOM_KEEP, geom);
	k1_rmw32(k1_mc(hw, K1_MC_CS1_GEOM), K1_GEOM_KEEP, geom);
}

/* the port of the MC itself does not feed the engine unless it aliases the window */
static void k1_tbl_put(const struct k1_ddr_hw *hw, uint32_t idx, uint32_t data,
		       uint32_t addr)
{
	k1_w32(hw->win + K1_MC_TBL_DATA, data);
	k1_w32(hw->win + K1_MC_TBL_ADDR, addr);
	k1_w32(hw->win + K1_MC_TBL_INDEX, idx);
}

/* the listed flow addresses both ranks; one rank gets 0x11 in that byte */
static uint32_t k1_tbl_data(const struct k1_ddr_hw *hw,
			    const struct k1_tbl_entry *e)
{
	const uint32_t a = e->addr & 0xffff;

	if (hw->cs_sel != 3 && (e->data >> 24) == K1_TBL_CS_BYTE_2CS &&
	    (a == K1_MC_USER0 || a == K1_MC_CA_CMD || a == K1_MC_PI_CMD))
		return (e->data & 0x00ff'ffff) |
		       ((0x10 | hw->cs_sel) << K1_CS_SEL_SHIFT);
	return e->data;
}

static void k1_tbl_seq(const struct k1_ddr_hw *hw,
		       const struct k1_tbl_entry *seq, size_t n, bool mr21,
		       uint16_t mr21_after, uint16_t run_end)
{
	const struct k1_tbl_entry e21 = {
		.data = K1_CA_MR_WRITE | (3u << K1_CS_SEL_SHIFT) | K1_MR21_16G,
		.addr = K1_MC_CA_CMD,
	};

	for (size_t i = 0; i < n; i++) {
		const struct k1_tbl_entry *e = &seq[i];
		const uint32_t shift = mr21 && e->idx > mr21_after &&
				       e->idx <= run_end;

		k1_tbl_put(hw, e->idx + shift, k1_tbl_data(hw, e), e->addr);
		if (mr21 && e->idx == mr21_after)
			k1_tbl_put(hw, mr21_after + 1u, k1_tbl_data(hw, &e21),
				   e21.addr);
	}
}

static_assert(K1_TBL_SNAPSHOT + 1 + K1_N_SNAP_GLOBALS +
	      (K1_MAX_PP + 1) * K1_N_SNAP_BANK == K1_TBL_SNAP_TAIL);
static_assert(K1_TBL_SNAP_TAIL + K1_N_SNAP_TAIL == K1_TBL_SNAP_END);

void k1_ddr_dfc_table_load(const struct k1_ddr_hw *hw, bool mr21)
{
	const uint32_t freq = k1_r32(k1_mc(hw, K1_MC_FREQ));
	const uint32_t ctl0 = k1_r32(k1_mc(hw, K1_MC_CTL0));
	const uint32_t win_ctl0 = k1_r32(hw->win + K1_MC_CTL0);
	uint32_t idx = K1_TBL_SNAPSHOT;

	/* the window may or may not alias the MC: worth one line on the UART */
	k1_log(hw->env, "k1-ddr: DFC window ctl0 0x%08x, ddr ctl0 0x%08x (%s)\n",
	       win_ctl0, ctl0, win_ctl0 == ctl0 ? "alias" : "separate");
	k1_tbl_seq(hw, k1_dfc_flow, K1_N_DFC_FLOW, mr21, K1_DFC_FLOW_MR21_AFTER,
		   K1_DFC_FLOW_RUN_END);
	k1_tbl_seq(hw, k1_dfc_exit, K1_N_DFC_EXIT, mr21, K1_DFC_EXIT_MR21_AFTER,
		   K1_DFC_EXIT_RUN_END);

	k1_tbl_put(hw, idx++, ctl0 | K1_TBL_CTL0_BITS,
		   K1_MC_CTL0 | K1_TBL_REQ_PMU);
	for (size_t i = 0; i < K1_N_SNAP_GLOBALS; i++)
		k1_tbl_put(hw, idx++, k1_r32(k1_mc(hw, k1_dfc_snap_globals[i])),
			   k1_dfc_snap_globals[i]);
	for (uint32_t n = 0; n <= K1_MAX_PP; n++) {
		k1_w32(k1_mc(hw, K1_MC_FREQ), (freq & ~K1_FREQ_FP_MASK) |
		       (k1_freq_bank(n) & K1_FREQ_FP_MASK));
		for (size_t i = 0; i < K1_N_SNAP_BANK; i++) {
			const uint32_t off = k1_dfc_snap_bank[i];
			uint32_t v = k1_r32(k1_mc(hw, off));

			/* the table keeps [31:30] only */
			if (off == K1_MC_FREQ)
				v &= ~K1_FREQ_SEL_MASK;
			k1_tbl_put(hw, idx++, v, off);
		}
	}
	k1_w32(k1_mc(hw, K1_MC_FREQ), freq);
	for (size_t i = 0; i < K1_N_SNAP_TAIL; i++)
		k1_tbl_put(hw, k1_dfc_snap_tail[i].idx,
			   k1_tbl_data(hw, &k1_dfc_snap_tail[i]),
			   k1_dfc_snap_tail[i].addr);
	k1_tbl_put(hw, K1_TBL_SNAP_END, ctl0, K1_MC_CTL0 | K1_TBL_EOP);

	for (size_t i = 0; i < K1_N_DFC_MARKS; i++) {
		const uintptr_t phy = hw->win + K1_PHY_OFF;

		k1_w32(phy + K1_PHY_MARK_DATA, k1_dfc_marks[i][0]);
		k1_w32(phy + K1_PHY_MARK_ADDR, k1_dfc_marks[i][1]);
		k1_w32(phy + K1_PHY_MARK_CMD, k1_dfc_marks[i][2]);
	}
	k1_log(hw->env, "k1-ddr: DFC table loaded: snapshot 0x%x..0x%x%s\n",
	       K1_TBL_SNAPSHOT, K1_TBL_SNAP_END, mr21 ? ", MR21" : "");
}

/* the doc 30 list, read right before the kick and again on a timeout */
struct k1_dfc_regs {
	uint32_t status, ctl0, misc, pre, freq, pi_cfg, pi_status, phy_init;
	uint32_t cc_ap, isr, hw_slp, clksel, win_ctl0;
};

static struct k1_dfc_regs k1_dfc_regs_read(const struct k1_ddr_hw *hw)
{
	return (struct k1_dfc_regs){
		.status = k1_r32(k1_mc(hw, K1_MC_STATUS)),
		.ctl0 = k1_r32(k1_mc(hw, K1_MC_CTL0)),
		.misc = k1_r32(k1_mc(hw, K1_MC_MISC)),
		.pre = k1_r32(k1_mc(hw, K1_MC_DFC_PRE)),
		.freq = k1_r32(k1_mc(hw, K1_MC_FREQ)),
		.pi_cfg = k1_r32(k1_mc(hw, K1_MC_PI_CFG)),
		.pi_status = k1_r32(k1_mc(hw, K1_MC_PI_STATUS)),
		.phy_init = k1_r32(hw->phy + K1_PHY_INIT),
		.cc_ap = k1_r32(k1_pmu(hw, K1_PMU_CC_AP)),
		.isr = k1_r32(k1_pmu(hw, K1_PMU_AP_ISR)),
		.hw_slp = k1_r32(k1_pmu(hw, K1_PMU_HW_SLP)),
		.clksel = k1_r32(k1_pmu(hw, K1_PMU_CLKSEL)),
		.win_ctl0 = k1_r32(hw->win + K1_MC_CTL0),
	};
}

static void k1_dfc_regs_log(const struct k1_ddr_hw *hw, const char *when,
			    const struct k1_dfc_regs *r)
{
	k1_log(hw->env, "k1-ddr: dfc %s: status 0x%02x ctl0 0x%08x misc 0x%08x 0x148 0x%08x freq 0x%08x pi_cfg 0x%x pi_status 0x%08x\n",
	       when, r->status, r->ctl0, r->misc, r->pre, r->freq, r->pi_cfg,
	       r->pi_status);
	k1_log(hw->env, "k1-ddr: dfc %s: phy_init 0x%x cc_ap 0x%08x isr 0x%08x hw_slp 0x%08x clksel 0x%08x win ctl0 0x%08x\n",
	       when, r->phy_init, r->cc_ap, r->isr, r->hw_slp, r->clksel,
	       r->win_ctl0);
}

/* doc 30: SR exit gives the DRAM back; a held bit 22 needs an MC reset */
static void k1_dfc_unstick(const struct k1_ddr_hw *hw, uint32_t sr)
{
	const struct k1_ddrt_env *env = hw->env;
	uint32_t st = 0;

	k1_w32(k1_mc(hw, K1_MC_USER0), K1_SR_CMD | K1_SR_EXIT |
	       (hw->cs_sel << K1_SR_MODE_SHIFT));
	const bool parked = k1_wait(env, k1_mc(hw, K1_MC_STATUS), sr, 0,
				    K1_SR_BUDGET_US, &st) != 0;
	const uint32_t cc = k1_r32(k1_pmu(hw, K1_PMU_CC_AP));

	k1_log(env, "k1-ddr: after SR exit: status 0x%02x%s, cc_ap 0x%08x%s\n",
	       st, parked ? " (still parked)" : "", cc,
	       (cc & K1_CC_DFC_KICK) ? ", bit 22 held: MC reset before any DFC" : "");
}

int k1_ddr_dfc(const struct k1_ddr_hw *hw, uint32_t fp)
{
	const struct k1_ddrt_env *env = hw->env;
	const uint32_t sr = hw->cs_num == 2 ? K1_SR_STAT_2CS : K1_SR_STAT_1CS;
	const uint64_t t0 = k1_now(env);
	uint32_t v = k1_r32(k1_mc(hw, K1_MC_STATUS));

	if (fp > K1_MAX_PP)
		return K1_DDRT_EINVAL;
	/* the engine would park the ranks and never reach the PLL handshake */
	if (v & sr) {
		k1_log(env, "k1-ddr: DFC to fp%u refused: status 0x%02x, ranks in self-refresh\n",
		       fp, v);
		return K1_DDRT_EIO;
	}
	if (k1_r32(k1_pmu(hw, K1_PMU_CC_AP)) & K1_CC_DFC_KICK) {
		k1_log(env, "k1-ddr: DFC to fp%u refused: bit 22 still held\n", fp);
		return K1_DDRT_EIO;
	}
	k1_rmw32(k1_pmu(hw, K1_PMU_AP_IMR), ~0u, K1_IMR_DCLK_FC);
	k1_w32(k1_mc(hw, K1_MC_DFC_PRE), K1_DFC_PRE_VAL);
	k1_w32(k1_pmu(hw, K1_PMU_CLKSEL), k1_clksel[fp]);
	k1_w32(k1_pmu(hw, K1_PMU_HW_SLP),
	       K1_HW_SLP_DFC | (fp << K1_HW_SLP_FP_SHIFT));
	const struct k1_dfc_regs before = k1_dfc_regs_read(hw);
	k1_w32(k1_pmu(hw, K1_PMU_CC_AP), K1_CC_DFC_KICK);
	if (k1_wait(env, k1_pmu(hw, K1_PMU_CC_AP), K1_CC_DFC_KICK, 0,
		    K1_FC_BUDGET_US, &v)) {
		const struct k1_dfc_regs stuck = k1_dfc_regs_read(hw);

		k1_log(env, "k1-ddr: DFC to fp%u not done in %u us, cc_ap 0x%08x\n",
		       fp, K1_FC_BUDGET_US, v);
		k1_dfc_regs_log(hw, "before", &before);
		k1_dfc_regs_log(hw, "stuck", &stuck);
		k1_dfc_unstick(hw, sr);
		return K1_DDRT_ETIMEDOUT;
	}

	const uint32_t freq = k1_r32(k1_mc(hw, K1_MC_FREQ));
	const uint32_t pi_cfg = k1_r32(k1_mc(hw, K1_MC_PI_CFG));
	const uint32_t st = k1_r32(k1_mc(hw, K1_MC_STATUS));
	const uint32_t clk = k1_r32(k1_pmu(hw, K1_PMU_CLKSEL));

	k1_log(env, "k1-ddr: DFC fp%u in %u us: ctl_freq 0x%08x pi_cfg 0x%x status 0x%02x clksel 0x%08x\n",
	       fp, (uint32_t)(k1_now(env) - t0), freq, pi_cfg, st, clk);
	if ((freq & K1_FREQ_FP_MASK) != (k1_freq_bank(fp) & K1_FREQ_FP_MASK) ||
	    ((pi_cfg & K1_PI_CFG_SEL_MASK) >> K1_PI_CFG_SEL_SHIFT) != fp ||
	    (clk & K1_CLKSEL_LOCK) != K1_CLKSEL_LOCK || (st & sr))
		return K1_DDRT_EIO;
	return K1_DDRT_OK;
}
