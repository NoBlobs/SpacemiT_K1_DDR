/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Thalles Campagnani */
/*
 * SpacemiT K1 LPDDR4/4X controller, PHY, PI engine and DDR PMU register map.
 * Every value below is taken from cleanroom/docs (03..08, 20, 25..31).
 */
#ifndef K1_DDRT_REGS_H
#define K1_DDRT_REGS_H

#include <stdint.h>

/* MC, offsets from ddr */
static constexpr uint32_t K1_MC_STATUS    = 0x0008;
static constexpr uint32_t K1_MC_USER0     = 0x0020;	/* init, ZQCAL, SR */
static constexpr uint32_t K1_MC_CA_CMD    = 0x0024;	/* MR read/write */
static constexpr uint32_t K1_MC_MISC      = 0x0028;	/* CA on/off port */
static constexpr uint32_t K1_MC_CTL0      = 0x0044;
static constexpr uint32_t K1_MC_TBL_INDEX = 0x0070;	/* DFC table port */
static constexpr uint32_t K1_MC_TBL_DATA  = 0x0074;
static constexpr uint32_t K1_MC_TBL_ADDR  = 0x0078;
static constexpr uint32_t K1_MC_DFC_PRE   = 0x0148;
static constexpr uint32_t K1_MC_CS0_MAP   = 0x0200;
static constexpr uint32_t K1_MC_CS1_MAP   = 0x0208;
static constexpr uint32_t K1_MC_CS1_HI    = 0x020c;
static constexpr uint32_t K1_MC_CS0_GEOM  = 0x0220;
static constexpr uint32_t K1_MC_CS1_GEOM  = 0x0224;
static constexpr uint32_t K1_MC_IO_PU     = 0x02c8;
static constexpr uint32_t K1_MC_FREQ      = 0x0304;
static constexpr uint32_t K1_MC_FREQ_AUX  = 0x0308;
static constexpr uint32_t K1_MC_TXVREF    = 0x030c;
static constexpr uint32_t K1_MC_DS0       = 0x0310;
static constexpr uint32_t K1_MC_DS1       = 0x0314;
static constexpr uint32_t K1_MC_MR_DATA   = 0x0370;
static constexpr uint32_t K1_MC_REFRESH0  = 0x0380;
static constexpr uint32_t K1_MC_REFRESH1  = 0x0384;
static constexpr uint32_t K1_MC_REFRESH2  = 0x0388;
static constexpr uint32_t K1_MC_WDQS      = 0x0400;
static constexpr uint32_t K1_MC_PI_CMD    = 0x13d0;
static constexpr uint32_t K1_MC_PI_CFG    = 0x13e0;
static constexpr uint32_t K1_MC_PI_WL_CFG = 0x13e8;
static constexpr uint32_t K1_MC_PI_RD_CFG = 0x13ec;
static constexpr uint32_t K1_MC_PI_WR_CFG = 0x13f8;
static constexpr uint32_t K1_MC_PI_STATUS = 0x13fc;

/* PHY: phy = ddr + K1_PHY_OFF, slice = phy + (boot_pp << K1_PHY_SLICE_SHIFT) */
static constexpr uint32_t K1_PHY_OFF         = 0x40000;
static constexpr uint32_t K1_PHY_SLICE_SHIFT = 14;
static constexpr uint32_t K1_PHY_CS_SHIFT    = 8;
static constexpr uint32_t K1_PHY_OTHER       = 0x10000;
static constexpr uint32_t K1_PHY_OTHER_24    = 0x10024;
static constexpr uint32_t K1_PHY_INIT        = 0x10028;
static constexpr uint32_t K1_PHY_MARK_CMD    = 0x10100;	/* DFC markers */
static constexpr uint32_t K1_PHY_MARK_DATA   = 0x10104;
static constexpr uint32_t K1_PHY_MARK_ADDR   = 0x10108;
static constexpr uint32_t K1_PHY_TRAIN_STAT  = 0x18000;
static constexpr uint32_t K1_PHY_HARVEST_RX  = 0x18200;
static constexpr uint32_t K1_PHY_HARVEST_TX  = 0x18300;
static constexpr uint32_t K1_PHY_HARVEST_DM  = 0x20;	/* after the 8 DQ words */

/* per-slice: COMMON of subPHY A at +0x3000, of subPHY B at +0x3200 */
static constexpr uint32_t K1_SLICE_COMMON0 = 0x3000;
static constexpr uint32_t K1_SLICE_COMMON1 = 0x3200;
static constexpr uint32_t K1_SLICE_VREF0   = 0x3004;
static constexpr uint32_t K1_SLICE_VREF1   = 0x3204;
static constexpr uint32_t K1_PHY_COMMON_A0_30 = 0x3030;	/* slice 0, subPHY A only */
static constexpr uint32_t K1_SLICE_DQ_CS0B0   = 0x0064;	/* byte 0 of CS0 only */

/* per byte block (32 words) */
static constexpr uint32_t K1_BYTE_TX_DQ = 0x00;
static constexpr uint32_t K1_BYTE_TX_DM = 0x20;
static constexpr uint32_t K1_BYTE_WL    = 0x24;
static constexpr uint32_t K1_BYTE_RX_DQ = 0x30;
static constexpr uint32_t K1_BYTE_GATE  = 0x70;

static constexpr uint32_t K1_PHY_INIT_VAL = 512;

static constexpr uint32_t K1_N_BYTES = 4;
static constexpr uint32_t K1_N_LANES = 8;
static constexpr uint32_t K1_N_DQ    = 32;
static constexpr uint32_t K1_N_VREF  = 16;
static constexpr uint32_t K1_MAX_CS  = 2;
static constexpr uint32_t K1_MAX_PP  = 3;

static constexpr uint32_t K1_RX_DLY_MASK = 31;
static constexpr uint32_t K1_TX_DLY_MASK = 63;

/* PHY_STATUS (phy + 0x18000) error bits */
static constexpr uint32_t K1_ST_WL    = 1u << 1;
static constexpr uint32_t K1_ST_RL    = 1u << 2;
static constexpr uint32_t K1_ST_WDQ   = 1u << 3;
static constexpr uint32_t K1_ST_RGATE = 1u << 4;

/* COMMON Vref word */
static constexpr uint32_t K1_VREF_BIT4    = 1u << 4;
static constexpr uint32_t K1_VREF_LO_MASK = 0xffff;

/* PI operations; the command is op | (cs << 24) with a 1-based cs */
static constexpr uint32_t K1_PI_OP_WL    = 0x1040'0000;
static constexpr uint32_t K1_PI_OP_RGATE = 0x1010'0000;
static constexpr uint32_t K1_PI_OP_RD    = 0x1020'0000;
static constexpr uint32_t K1_PI_OP_WR    = 0x1008'0000;
static constexpr uint32_t K1_PI_CS_SHIFT = 24;

static constexpr uint32_t K1_PI_DONE_WL    = 0x01;
static constexpr uint32_t K1_PI_DONE_RGATE = 0x02;
static constexpr uint32_t K1_PI_DONE_RD    = 0x06;
static constexpr uint32_t K1_PI_DONE_WR    = 0x60;
static constexpr uint32_t K1_PI_GATE_MASK  = 0x06;
static constexpr uint32_t K1_PI_GATE_PASS  = 0x06;
static constexpr uint32_t K1_PI_GATE_BIT2  = 0x04;	/* RGATE does not clear it */
static constexpr uint32_t K1_PI_INIT_DONE  = 1u << 31;

/* DFI PHY init_start through PI_CMD (doc 26) */
static constexpr uint32_t K1_DFI_INIT_START = 0x1000'0001;
static constexpr uint32_t K1_DFI_INIT_CLEAR = 0x1000'0100;

/* sequencer parameters (opaque magics, doc 04 / 20) */
static constexpr uint32_t K1_WL_P0 = 0;
static constexpr uint32_t K1_WL_P1 = 10;
static constexpr uint32_t K1_WL_P2 = 100;
static constexpr uint32_t K1_WL_P3 = 30;
static constexpr uint32_t K1_WL_P4 = 68;
static constexpr uint32_t K1_RD_P0 = 80;
static constexpr uint32_t K1_RD_P1 = 112;
static constexpr uint32_t K1_RD_P2 = 0;
static constexpr uint32_t K1_WR_P0 = 16;
static constexpr uint32_t K1_WR_P1 = 96;

static constexpr uint32_t K1_PI_WL_CFG_VAL =
	K1_WL_P4 | (K1_WL_P3 << 8) | (K1_WL_P2 << 16) | (K1_WL_P1 << 24) |
	(K1_WL_P0 << 31);
static constexpr uint32_t K1_PI_RD_CFG_VAL =
	(K1_RD_P0 << 8) | (K1_RD_P2 << 16) | K1_RD_P1;
static constexpr uint32_t K1_PI_WR_CFG_VAL = (K1_WR_P1 << 6) | K1_WR_P0;

static_assert(K1_PI_WL_CFG_VAL == 0x0a64'1e44);
static_assert(K1_PI_RD_CFG_VAL == 0x0000'5070);
static_assert(K1_PI_WR_CFG_VAL == 0x0000'1810);

/* self-refresh (doc 07) */
static constexpr uint32_t K1_SR_CMD        = 0x1000'0000;
static constexpr uint32_t K1_SR_ENTER      = 1u << 6;
static constexpr uint32_t K1_SR_EXIT       = 2u << 6;
static constexpr uint32_t K1_SR_MODE_SHIFT = 24;
static constexpr uint32_t K1_SR_STAT_2CS   = 0x44;	/* bits 2 and 6 */
static constexpr uint32_t K1_SR_STAT_1CS   = 0x04;

/* USER_0 commands; the CS byte is 0x10 | cs_sel (doc 26) */
static constexpr uint32_t K1_USER0_INIT     = 0x1000'0001;
static constexpr uint32_t K1_USER0_ZQ_START = 0x1000'2000;
static constexpr uint32_t K1_USER0_ZQ_LATCH = 0x1000'1000;
static constexpr uint32_t K1_CS_SEL_SHIFT   = 24;
static constexpr uint32_t K1_INIT_DONE_1CS  = 0x01;	/* STATUS bits 0 / 4 */
static constexpr uint32_t K1_INIT_DONE_2CS  = 0x11;

/* mode registers (doc 08, 28) */
static constexpr uint32_t K1_CA_MR_WRITE    = 0x1002'0000;
static constexpr uint32_t K1_MR21_16G       = 0x95;	/* MR21, bit 7 RFU */
static constexpr uint32_t K1_CA_MR_READ     = 0x1001'0000;
static constexpr uint32_t K1_CA_MR_CS_SHIFT = 24;
static constexpr uint32_t K1_CA_MR_CH_SHIFT = 18;
static constexpr uint32_t K1_MR_DATA_VALID  = 1u << 31;
static constexpr uint32_t K1_CA_OP_TXVREF   = 0x1302'000e;
static constexpr uint32_t K1_TXVREF_MASK    = 0xffc0'ffff;
static constexpr uint32_t K1_TXVREF_SHIFT   = 16;
static constexpr uint32_t K1_TXVREF_FIELD   = 0x3f;
static constexpr uint32_t K1_TX_VREF_SEED0  = 21;
static constexpr uint32_t K1_TX_FALLBACK_IDX = 4;

/* CTL_FREQ / PI_CFG fields (doc 03, 26) */
static constexpr uint32_t K1_FREQ_PP_SHIFT    = 30;
static constexpr uint32_t K1_FREQ_SEL_SHIFT   = 28;
static constexpr uint32_t K1_FREQ_FP_MASK     = 0xf000'0000;
static constexpr uint32_t K1_FREQ_SEL_MASK    = 3u << 28;
static constexpr uint32_t K1_FREQ_REST        = 0x0080'0400;
static constexpr uint32_t K1_PI_CFG_SEL_SHIFT = 2;
static constexpr uint32_t K1_PI_CFG_SEL_MASK  = 3u << 2;

/* CTL_FREQ selecting the timing bank of fp: both fields equal */
static inline uint32_t k1_freq_bank(uint32_t fp)
{
	return (fp << K1_FREQ_PP_SHIFT) | (fp << K1_FREQ_SEL_SHIFT) |
	       K1_FREQ_REST;
}

/* CS map word: [20:16] size code, bit 0 present; CS1 start in 8 MB units */
static constexpr uint32_t K1_MAP_KEEP        = 0x0060'ffff;
static constexpr uint32_t K1_MAP_CODE_SHIFT  = 16;
static constexpr uint32_t K1_MAP_PRESENT     = 1u << 0;
static constexpr uint32_t K1_MAP_START_SHIFT = 23;
static constexpr uint32_t K1_MAP_START_LO    = 0x1ff;
static constexpr uint32_t K1_GEOM_KEEP       = 0xffff'f00c;
static constexpr uint32_t K1_GEOM_BANK8      = 2;
static constexpr uint32_t K1_GEOM_COL10      = 3u << 4;
static constexpr uint32_t K1_GEOM_ROW_SHIFT  = 8;

/* DFC table: ADDR = MC offset [12:0] | flags; snapshot 0x200..0x28a (doc 30) */
static constexpr uint32_t K1_TBL_POLL       = 1u << 13;
static constexpr uint32_t K1_TBL_REQ_PMU    = 1u << 16;
static constexpr uint32_t K1_TBL_EOP        = 1u << 17;
static constexpr uint32_t K1_TBL_SNAPSHOT   = 0x200;
static constexpr uint32_t K1_TBL_SNAP_TAIL  = 0x27b;
static constexpr uint32_t K1_TBL_SNAP_END   = 0x28a;
static constexpr uint32_t K1_TBL_CTL0_BITS  = (1u << 2) | (1u << 12);
static constexpr uint32_t K1_DFC_PRE_VAL    = 0x80ac'0000;

/* DDR PMU (APMU) at 0xd4282800 (doc 25) */
static constexpr uint32_t K1_PMU_CC_AP     = 0x004;
static constexpr uint32_t K1_PMU_AP_IMR    = 0x098;
static constexpr uint32_t K1_PMU_AP_ISR    = 0x0a0;
static constexpr uint32_t K1_PMU_HW_SLP    = 0x0b0;
static constexpr uint32_t K1_PMU_MCK_CTRL  = 0x0e8;
static constexpr uint32_t K1_PMU_PHY_LDO   = 0x398;
static constexpr uint32_t K1_PMU_PLL1_CTL1 = 0x39c;
static constexpr uint32_t K1_PMU_PLL1_CTL2 = 0x3a0;
static constexpr uint32_t K1_PMU_PHY_BIAS  = 0x3a4;
static constexpr uint32_t K1_PMU_PLL2_CTL1 = 0x3a8;
static constexpr uint32_t K1_PMU_PLL2_CTL2 = 0x3ac;
static constexpr uint32_t K1_PMU_CLKSEL    = 0x3b4;

static constexpr uint32_t K1_CC_SWFC_KICK  = 1u << 26;
static constexpr uint32_t K1_CC_DFC_KICK   = 1u << 22;
static constexpr uint32_t K1_IMR_DCLK_FC   = 1u << 4;
static constexpr uint32_t K1_HW_SLP_RESET  = 1u << 21;	/* 1 = out of reset */
static constexpr uint32_t K1_HW_SLP_SWFC   = 0x4060'0400;
static constexpr uint32_t K1_HW_SLP_DFC    = 0x4020'0200;
static constexpr uint32_t K1_HW_SLP_FP_SHIFT = 3;
static constexpr uint32_t K1_MCK_ON        = 3;

static constexpr uint32_t K1_PLL1_2666     = 0x203c'5555;
static constexpr uint32_t K1_PLL1_ANALOG   = 0x50dd'6538;
static constexpr uint32_t K1_PLL1_2400_KEEP = 0xffff'00ff;
static constexpr uint32_t K1_PLL1_2400_BYTE = 0x3b;
/* leftover at ddr_init entry on this SoC, then 2400 RMW of [15:8] */
static constexpr uint32_t K1_PLL1_BOOTROM  = 0x2a20'0000;
static constexpr uint32_t K1_PLL1_BOOTROM_ANALOG = 0x50dd'6432;
static constexpr uint32_t K1_PLL1_2400     = 0x2a20'3b00;
static constexpr uint32_t K1_PLL2_VCO      = 0x2023'8000;
static constexpr uint32_t K1_PLL2_ANALOG   = 0x50dd'632c;

static constexpr uint32_t K1_CLKSEL_PLL2_EN  = 1u << 8;
static constexpr uint32_t K1_CLKSEL_PLL1_EN  = 1u << 9;
static constexpr uint32_t K1_CLKSEL_EXTRA_EN = 1u << 11;
static constexpr uint32_t K1_CLKSEL_LOCK     = 0x3'0000;	/* PLL2 | PLL1 */

static constexpr uint32_t K1_PHY_BIAS_KEEP = 0xffff'00ff;
static constexpr uint32_t K1_PHY_BIAS_VAL  = 0x0fu << 8;
static constexpr uint32_t K1_LDO_4X        = 3u << 10;
static constexpr uint32_t K1_LDO_4         = 2u << 10;
static constexpr uint32_t K1_LDO_4_KEEP    = 0xffff'cfff;
static constexpr uint32_t K1_LDO_4_088V    = 3u << 12;

/* byte b (DQ[8b+7:8b]) inside a slice: DATAPHY0 A, DATAPHY1 A, DATAPHY0 B, DATAPHY1 B */
static constexpr uint32_t K1_BYTE_OFF[K1_N_BYTES] = {
	0x0000, 0x1000, 0x0200, 0x1200,
};

static inline uint32_t k1_cs_sel(uint32_t cs_num)
{
	return cs_num == 1 ? 1 : 3;
}

static inline uint32_t k1_pack_vref(uint32_t step)
{
	return (step << 28) | (step << 24) | (step << 20) | (step << 16);
}

#endif /* K1_DDRT_REGS_H */
