#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Thalles Campagnani
"""Kill mutants of the open firmware against `make test`.

Each mutant is (name, path-under-src, old, new). The runner copies src/ and
tools/ (the suite lives in tools/test), applies one substitution under src/,
and runs the QEMU suite. A mutant is killed if the tests fail.

  python3 tools/test/mutate.py            # all mutants (or: make -C src mutate)
  python3 tools/test/mutate.py --check    # anchors only (no compile)
  python3 tools/test/mutate.py pll        # names matching a substring
"""

from __future__ import annotations

import argparse
import concurrent.futures as cf
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]   # implementation/
SRC = ROOT / "src"
T, I, B, TB = (
    "lib/k1_ddrt.c",
    "lib/k1_ddr_init.c",
    "lib/k1_ddr_boot.c",
    "lib/k1_ddr_tables.c",
)

DUMMY = "\t(void)k1_ddrt_mr_read(&mc, b->env, 0, 0, 14, &discard);\n"
CHECK = (
    "\tif (k1_ddrt_check_mr14(&mc, b->env))\n"
    "\t\tk1_log(b->env, \"k1-ddr: warning: MR14 differs from the fp%u bank\\n\",\n"
    "\t\t       fp);\n"
)
RMW2400 = (
    "\t\tk1_rmw32(k1_pmu(hw, K1_PMU_PLL1_CTL1), K1_PLL1_2400_KEEP,\n"
    "\t\t\t K1_PLL1_2400_BYTE << 8);"
)
TBL_PUT = (
    "\tk1_w32(hw->win + K1_MC_TBL_DATA, data);\n"
    "\tk1_w32(hw->win + K1_MC_TBL_ADDR, addr);\n"
    "\tk1_w32(hw->win + K1_MC_TBL_INDEX, idx);"
)

MUTANTS: list[tuple[str, str, str, str]] = [
    # training (doc 13–18)
    ("train: max_min >= (keeps last of plateau)", T,
     "if (min_per_step[s] > min_per_step[best])",
     "if (min_per_step[s] >= min_per_step[best])"),
    ("train: no MR14 command", T,
     "\tk1_w32(p->ddr + K1_MC_CA_CMD, K1_CA_OP_TXVREF);", ""),
    ("train: no Rx re-apply of winner", T, "\tk1_set_rx_vref(p, best);\n", ""),
    ("train: 0-based cs in PI cmd", T,
     "op | (cs << K1_PI_CS_SHIFT)", "op | ((cs - 1) << K1_PI_CS_SHIFT)"),
    ("train: RL bit not cleared", T,
     "k1_rmw32(p->phy + K1_PHY_TRAIN_STAT, ~K1_ST_RL, 0);", ""),
    ("train: gate TIMEOUT ignored", T,
     "if ((st & K1_PI_GATE_MASK) != K1_PI_GATE_PASS) {", "if (0) {"),
    ("train: only VREF0 written", T,
     "\tk1_w32(p->slice + K1_SLICE_VREF1, packed);", ""),
    ("train: Tx zero fallback missing", T,
     "\tif (p->res->tx_min[best] == 0)\n\t\tbest = K1_TX_FALLBACK_IDX;\n", ""),
    ("train: SR mode 1 always", T, "return cs_num == 2 ? 3 : 1;", "return 1;"),
    ("train: wait for equality", T,
     "if ((v & mask) == want) {", "if (v == want + (mask & 0)) {"),
    ("train: harvest once per step (last CS only)", T,
     "\t\t\tif (m < worst)\n\t\t\t\tworst = m;\n\t\t\tif (p->env && p->env->verbose)\n"
     "\t\t\t\tk1_log(p->env,\n\t\t\t\t       \"k1-ddrt:   rx",
     "\t\t\tworst = m;\n\t\t\tif (p->env && p->env->verbose)\n"
     "\t\t\t\tk1_log(p->env,\n\t\t\t\t       \"k1-ddrt:   rx"),
    ("train: Tx harvest once per step (last CS only)", T,
     "\t\t\tif (m < worst)\n\t\t\t\tworst = m;\n\t\t\tif (p->env && p->env->verbose)\n"
     "\t\t\t\tk1_log(p->env,\n\t\t\t\t       \"k1-ddrt:   tx",
     "\t\t\tworst = m;\n\t\t\tif (p->env && p->env->verbose)\n"
     "\t\t\t\tk1_log(p->env,\n\t\t\t\t       \"k1-ddrt:   tx"),
    ("train: WL CFG not written", T,
     "\tk1_w32(p->ddr + K1_MC_PI_WL_CFG, K1_PI_WL_CFG_VAL);", ""),
    ("train: SR exit skipped on failure", T, "\t\t(void)k1_sr_exit(&p);\n", ""),
    ("P-09: PHY_STATUS bit 4 ignored", T, "if (ps & K1_ST_RGATE) {", "if (0) {"),
    ("P-09: classic test behind leftover bit 2", T,
     "if (!(snap & K1_PI_GATE_BIT2)) {", "if (1) {"),

    # PLL / SW-FC (docs 25, 06; P-21)
    ("pll: 2666 uses the 2400 RMW", I, "if (data_rate == 2666) {", "if (0) {"),
    ("pll: rates above 2666 take the 2666 pair", I,
     "if (data_rate == 2666) {", "if (data_rate >= 2666) {"),
    ("pll: 2400 writes 0x203b5555", I, RMW2400,
     "\t\tk1_w32(k1_pmu(hw, K1_PMU_PLL1_CTL1), 0x203b5555);\n"
     "\t\tk1_w32(k1_pmu(hw, K1_PMU_PLL1_CTL2), K1_PLL1_ANALOG);"),
    ("pll: no lock wait", I,
     "if (k1_wait(env, k1_pmu(hw, K1_PMU_CLKSEL), K1_CLKSEL_LOCK,",
     "if (0 && k1_wait(env, k1_pmu(hw, K1_PMU_CLKSEL), K1_CLKSEL_LOCK,"),
    ("pll: PLL2 VCO not written", I,
     "\tk1_w32(k1_pmu(hw, K1_PMU_PLL2_CTL1), K1_PLL2_VCO);\n", ""),
    ("pll: PLL1 analog not written", I,
     "\t\tk1_w32(k1_pmu(hw, K1_PMU_PLL1_CTL2), K1_PLL1_ANALOG);\n", ""),
    ("pll: bit 11 missing", I,
     "K1_CLKSEL_PLL2_EN | K1_CLKSEL_PLL1_EN | K1_CLKSEL_EXTRA_EN);",
     "K1_CLKSEL_PLL2_EN | K1_CLKSEL_PLL1_EN);"),
    ("swfc: no clksel", I, "\tk1_w32(k1_pmu(hw, K1_PMU_CLKSEL), k1_clksel[0]);\n", ""),
    ("swfc: no HW_SLP", I, "\tk1_w32(k1_pmu(hw, K1_PMU_HW_SLP), K1_HW_SLP_SWFC);\n", ""),
    ("swfc: wait skipped", I,
     "if (k1_wait(env, k1_pmu(hw, K1_PMU_CC_AP), K1_CC_SWFC_KICK, 0,",
     "if (0 && k1_wait(env, k1_pmu(hw, K1_PMU_CC_AP), K1_CC_SWFC_KICK, 0,"),
    ("swfc: DFC kick bit", I,
     "k1_rmw32(k1_pmu(hw, K1_PMU_CC_AP), ~0u, K1_CC_SWFC_KICK);",
     "k1_rmw32(k1_pmu(hw, K1_PMU_CC_AP), ~0u, K1_CC_DFC_KICK);"),
    ("swfc: MCK not enabled", I,
     "\tk1_rmw32(k1_pmu(hw, K1_PMU_MCK_CTRL), ~K1_MCK_ON, K1_MCK_ON);\n", ""),

    # Device_Init / PHY / DRAM
    ("mc: CTL0 readback ignored", I, "if (ctl0 != k1_mc_globals[0].val) {", "if (0) {"),
    ("mc: x8 overlays skipped", I, "\tif (x8) {", "\tif (0) {"),
    ("mc: 0x308 RMW missing", I,
     "\tk1_rmw32(k1_mc(hw, K1_MC_FREQ_AUX), K1_FREQ_AUX_KEEP, K1_FREQ_AUX_VAL);\n", ""),
    ("mc: refresh missing", I,
     "\tfor (size_t i = 0; i < 3; i++)\n"
     "\t\tk1_w32(k1_mc(hw, k1_mc_refresh[i].off), k1_mc_refresh[i].val);\n", ""),
    ("mc: TXVREF seed missing", I, "\t\tk1_w32(a, K1_IO_TXVREF_SEED);\n", ""),
    ("mc: DS seed missing", I, "\t\tk1_w32(a, K1_IO_DS_SEED);\n", ""),
    ("mc: WDQS missing (LPDDR4)", I,
     "\t\tif (hw->type == K1_DDR_LPDDR4)\n"
     "\t\t\tk1_w32(k1_mc(hw, K1_MC_WDQS), k1_mc_wdqs[n]);\n", ""),
    ("phy: no PMU bias", I,
     "\tk1_rmw32(k1_pmu(hw, K1_PMU_PHY_BIAS), K1_PHY_BIAS_KEEP, K1_PHY_BIAS_VAL);\n", ""),
    ("phy: LDO 4X for LPDDR4", I,
     "\tif (hw->type == K1_DDR_LPDDR4) {\n\t\tk1_rmw32(k1_pmu(hw, K1_PMU_PHY_LDO)",
     "\tif (0) {\n\t\tk1_rmw32(k1_pmu(hw, K1_PMU_PHY_LDO)"),
    ("phy: COMMON not cleared first", I,
     "\t\tk1_w32(c[0] + K1_COM_ARM, 0);\n\t\tk1_w32(c[1] + K1_COM_ARM, 0);\n", ""),
    ("phy: COMMON not armed", I,
     "\t\tk1_w32(c[0] + K1_COM_ARM, 1);\n\t\tk1_w32(c[1] + K1_COM_ARM, 1);\n", ""),
    ("phy: subPHY B skipped", I,
     "for (uint32_t i = 0; i < 2; i++) {\n\t\t\tk1_rmw32(c[i] + K1_COM_VREF, K1_COM_PRE_KEEP",
     "for (uint32_t i = 0; i < 1; i++) {\n\t\t\tk1_rmw32(c[i] + K1_COM_VREF, K1_COM_PRE_KEEP"),
    ("phy: only slice 0", I,
     "\tfor (uint32_t n = 0; n <= K1_MAX_PP; n++) {\n\t\tconst uintptr_t s = hw->phy",
     "\tfor (uint32_t n = 0; n < 1; n++) {\n\t\tconst uintptr_t s = hw->phy"),
    ("phy: Rx ODT single", I,
     "const uint32_t d3 = ((uint32_t)io->rx_odt << 3) | io->rx_odt;",
     "const uint32_t d3 = io->rx_odt;"),
    ("phy: Rx Vref one nibble", I,
     "const uint32_t rxv = ((uint32_t)io->rx_vref << 16) | ((uint32_t)io->rx_vref << 24);",
     "const uint32_t rxv = (uint32_t)io->rx_vref << 16;"),
    ("phy: +0x14 not written", I,
     "\t\t\tk1_rmw32(c[i] + K1_COM_14, K1_COM_14_KEEP, K1_COM_14_VAL);\n", ""),
    ("phy: +0x10 not written", I,
     "\t\t\tk1_rmw32(c[i] + K1_COM_10, ~0u, K1_COM_10_BIT);\n", ""),
    ("phy: 0x3030 missing", I, "\tk1_w32(hw->phy + K1_PHY_COMMON_A0_30, K1_COM_A0_30_VAL);\n", ""),
    ("phy: 0x64 only slice 0", I,
     "\tfor (uint32_t n = 0; n <= K1_MAX_PP; n++)\n"
     "\t\tk1_w32(hw->phy + (n << K1_PHY_SLICE_SHIFT) + K1_SLICE_DQ_CS0B0,",
     "\tfor (uint32_t n = 0; n < 1; n++)\n"
     "\t\tk1_w32(hw->phy + (n << K1_PHY_SLICE_SHIFT) + K1_SLICE_DQ_CS0B0,"),
    ("phy: OTHER bit 0 missing", I, "\tk1_rmw32(hw->phy + K1_PHY_OTHER, ~0u, 1);\n", ""),
    ("phy: analog skipped", I, "\tk1_phy_analog(hw);\n", ""),
    ("dram: DFI wait skipped", I,
     "if (k1_wait(env, k1_mc(hw, K1_MC_PI_STATUS), K1_PI_INIT_DONE,",
     "if (0 && k1_wait(env, k1_mc(hw, K1_MC_PI_STATUS), K1_PI_INIT_DONE,"),
    ("dram: DFI clear missing", I,
     "\tk1_w32(k1_mc(hw, K1_MC_PI_CMD), K1_DFI_INIT_CLEAR | k1_cs_field(hw));\n", ""),
    ("dram: init wait skipped", I,
     "if (k1_wait(env, k1_mc(hw, K1_MC_STATUS), chk, chk, K1_INIT_BUDGET_US,",
     "if (0 && k1_wait(env, k1_mc(hw, K1_MC_STATUS), chk, chk, K1_INIT_BUDGET_US,"),
    ("dram: 2CS check with 1 CS", I,
     "hw->cs_num == 1 ? K1_INIT_DONE_1CS : K1_INIT_DONE_2CS", "K1_INIT_DONE_2CS"),
    ("dram: ZQ START->LATCH no wait", I, "\t\tk1_udelay(env, K1_ZQ_START_US);\n", ""),
    ("dram: ZQ on both CS always", I,
     "for (uint32_t cs0 = 0; cs0 < hw->cs_num; cs0++)",
     "for (uint32_t cs0 = 0; cs0 < 2; cs0++)"),
    ("dram: ZQ before MR batch 1", I,
     "\tfor (size_t i = 0; i < K1_N_MR_INIT1; i++)\n\t\tk1_ddr_mr_write(hw, k1_mr_init1[i]);\n",
     ""),
    ("dram: MR batch 2 missing", I,
     "\tfor (size_t i = 0; i < K1_N_MR_INIT2; i++)\n\t\tk1_ddr_mr_write(hw, k1_mr_init2[i]);\n",
     ""),
    ("mr: no tMRW", I, "\tk1_udelay(hw->env, K1_MRW_US);\n", ""),
    ("mr: cs_sel 3 always", I,
     "K1_CA_MR_WRITE | k1_cs_field(hw) | (mr & 0xff)",
     "K1_CA_MR_WRITE | (3u << K1_CS_SEL_SHIFT) | (mr & 0xff)"),
    ("init: CTL_FREQ check ignored", I,
     "if (freq != k1_freq_bank(0) || (pi_cfg & K1_PI_CFG_SEL_MASK)) {", "if (0) {"),
    ("reset: low pulse without wait", I,
     "~K1_HW_SLP_RESET, 0);\n\tk1_udelay(hw->env, K1_MC_RESET_US);",
     "~K1_HW_SLP_RESET, 0);"),
    ("reset: no wait after release", I,
     "K1_HW_SLP_RESET);\n\tk1_udelay(hw->env, K1_MC_RESET_US);\n}",
     "K1_HW_SLP_RESET);\n}"),
    ("map: CS1 present with 1 CS", I,
     "(hw->cs_num == 2 ? K1_MAP_PRESENT : 0)", "K1_MAP_PRESENT"),
    ("map: CS1 HI missing", I, "\tk1_w32(k1_mc(hw, K1_MC_CS1_HI), start >> 9);\n", ""),
    ("map: start shift 22", I, "<< K1_MAP_START_SHIFT)", "<< 22)"),
    ("map: CS1 geometry missing", I,
     "\tk1_rmw32(k1_mc(hw, K1_MC_CS1_GEOM), K1_GEOM_KEEP, geom);\n", ""),
    ("geom: x8 row not +1", I, ".row = t[i].row + io,", ".row = t[i].row,"),
    ("geom: x8 size not doubled", I,
     "2u * ((uint32_t)t[i].ch_mb << io)", "2u * (uint32_t)t[i].ch_mb"),

    # DFC table (P-14)
    ("tbl: index written first", I, TBL_PUT,
     "\tk1_w32(hw->win + K1_MC_TBL_INDEX, idx);\n"
     "\tk1_w32(hw->win + K1_MC_TBL_DATA, data);\n"
     "\tk1_w32(hw->win + K1_MC_TBL_ADDR, addr);"),
    ("tbl: 1 CS byte kept", I, "if (hw->cs_sel != 3 &&", "if (0 &&"),
    ("tbl: MR21 not inserted", I, "if (mr21 && e->idx == mr21_after)", "if (0)"),
    ("tbl: run not shifted", I,
     "const uint32_t shift = mr21 && e->idx > mr21_after &&",
     "const uint32_t shift = 0 && e->idx > mr21_after &&"),
    ("tbl: CTL0 bits missing", I, "| K1_TBL_CTL0_BITS,", ","),
    ("tbl: banks not switched", I,
     "\t\tk1_w32(k1_mc(hw, K1_MC_FREQ), (freq & ~K1_FREQ_FP_MASK) |\n"
     "\t\t       (k1_freq_bank(n) & K1_FREQ_FP_MASK));\n", ""),
    ("tbl: markers missing", I,
     "\tfor (size_t i = 0; i < K1_N_DFC_MARKS; i++) {\n\t\tconst uintptr_t phy",
     "\tfor (size_t i = 0; i < 0; i++) {\n\t\tconst uintptr_t phy"),
    ("dfc: 0x148 missing", I, "\tk1_w32(k1_mc(hw, K1_MC_DFC_PRE), K1_DFC_PRE_VAL);\n", ""),
    ("dfc: IMR missing", I,
     "\tk1_rmw32(k1_pmu(hw, K1_PMU_AP_IMR), ~0u, K1_IMR_DCLK_FC);\n", ""),
    ("dfc: SW-FC kick bit", I,
     "\tk1_w32(k1_pmu(hw, K1_PMU_CC_AP), K1_CC_DFC_KICK);",
     "\tk1_w32(k1_pmu(hw, K1_PMU_CC_AP), K1_CC_SWFC_KICK);"),
    ("dfc: fp not in HW_SLP", I,
     "K1_HW_SLP_DFC | (fp << K1_HW_SLP_FP_SHIFT));", "K1_HW_SLP_DFC);"),
    ("dfc: SW-FC encoding", I,
     "K1_HW_SLP_DFC | (fp << K1_HW_SLP_FP_SHIFT));",
     "K1_HW_SLP_SWFC | (fp << K1_HW_SLP_FP_SHIFT));"),
    ("dfc: wait skipped", I,
     "if (k1_wait(env, k1_pmu(hw, K1_PMU_CC_AP), K1_CC_DFC_KICK, 0,",
     "if (0 && k1_wait(env, k1_pmu(hw, K1_PMU_CC_AP), K1_CC_DFC_KICK, 0,"),
    ("dfc: clksel of fp0", I,
     "k1_w32(k1_pmu(hw, K1_PMU_CLKSEL), k1_clksel[fp]);",
     "k1_w32(k1_pmu(hw, K1_PMU_CLKSEL), k1_clksel[0]);"),
    ("dfc: result check ignored", I,
     "\t    (clk & K1_CLKSEL_LOCK) != K1_CLKSEL_LOCK || (st & sr))\n\t\treturn K1_DDRT_EIO;",
     "\t    (clk & K1_CLKSEL_LOCK) != K1_CLKSEL_LOCK || (st & sr))\n\t\t(void)0;"),
    ("p14: port through ddr", I, TBL_PUT,
     "\tk1_w32(hw->ddr + K1_MC_TBL_DATA, data);\n"
     "\tk1_w32(hw->ddr + K1_MC_TBL_ADDR, addr);\n"
     "\tk1_w32(hw->ddr + K1_MC_TBL_INDEX, idx);"),
    ("p14: markers through phy", I,
     "const uintptr_t phy = hw->win + K1_PHY_OFF;", "const uintptr_t phy = hw->phy;"),
    ("p14: no REQ_PMU on 0x200", I, "K1_MC_CTL0 | K1_TBL_REQ_PMU);", "K1_MC_CTL0);"),
    ("p14: no EOP flag", I, "K1_MC_CTL0 | K1_TBL_EOP);", "K1_MC_CTL0);"),
    ("p14: no EOP line", I,
     "\tk1_tbl_put(hw, K1_TBL_SNAP_END, ctl0, K1_MC_CTL0 | K1_TBL_EOP);\n", ""),
    ("p14: EOP data with bits", I,
     "k1_tbl_put(hw, K1_TBL_SNAP_END, ctl0,",
     "k1_tbl_put(hw, K1_TBL_SNAP_END, ctl0 | K1_TBL_CTL0_BITS,"),
    ("p14: 0x200 without CTL0 bits", I,
     "ctl0 | K1_TBL_CTL0_BITS,\n\t\t   K1_MC_CTL0 | K1_TBL_REQ_PMU",
     "ctl0,\n\t\t   K1_MC_CTL0 | K1_TBL_REQ_PMU"),
    ("p14: [29:28] kept in RAM", I,
     "\t\t\tif (off == K1_MC_FREQ)\n\t\t\t\tv &= ~K1_FREQ_SEL_MASK;\n", ""),
    ("p14: bank selected by [31:30] only", I,
     "(k1_freq_bank(n) & K1_FREQ_FP_MASK));", "(n << K1_FREQ_PP_SHIFT));"),
    ("p14: CTL_FREQ not restored", I,
     "\tk1_w32(k1_mc(hw, K1_MC_FREQ), freq);\n\tfor (size_t i = 0; i < K1_N_SNAP_TAIL",
     "\tfor (size_t i = 0; i < K1_N_SNAP_TAIL"),
    ("p14: no tail", I,
     "\tfor (size_t i = 0; i < K1_N_SNAP_TAIL; i++)",
     "\tfor (size_t i = 0; i < 0; i++)"),
    ("p14: tail without CS byte", I,
     "k1_tbl_data(hw, &k1_dfc_snap_tail[i]),", "k1_dfc_snap_tail[i].data,"),
    ("p14: tail PI_CFG value", TB,
     "{ 0x0002'0200, 0x0000'13e0, 0x27b },", "{ 0x0002'0000, 0x0000'13e0, 0x27b },"),
    ("p14: tail last idx", TB,
     "{ 0x1302'0003, 0x0000'0024, 0x289 },", "{ 0x1302'0003, 0x0000'0024, 0x28b },"),
    ("p14: tail POLL value", TB,
     "{ 0x0001'0000, 0x0000'33fc, 0x27e },", "{ 0x0000'0000, 0x0000'33fc, 0x27e },"),
    ("p14: kick in SR", I,
     "\tif (v & sr) {\n\t\tk1_log(env, \"k1-ddr: DFC to fp%u refused: status",
     "\tif (0) {\n\t\tk1_log(env, \"k1-ddr: DFC to fp%u refused: status"),
    ("p14: kick on held bit 22", I,
     "if (k1_r32(k1_pmu(hw, K1_PMU_CC_AP)) & K1_CC_DFC_KICK) {", "if (0) {"),
    ("p14: no SR exit after timeout", I, "\t\tk1_dfc_unstick(hw, sr);\n", ""),
    ("p14: SR exit for both ranks", I,
     "(hw->cs_sel << K1_SR_MODE_SHIFT));", "(3u << K1_SR_MODE_SHIFT));"),
    ("p14: SR exit is enter", I, "K1_SR_CMD | K1_SR_EXIT |\n", "K1_SR_CMD | K1_SR_ENTER |\n"),
    ("p14: no window arg check", B, "!cfg->dfc_win || ", ""),
    ("p14: window not in hw", I, "\t\t.win = cfg->dfc_win,\n", ""),

    # boot plan / dummy MRR (P-19, P-20)
    ("boot: no 2nd pp0", B,
     "\trc = k1_boot_pass(&b, 0);\n\tn = k1_ddr_dfc_plan",
     "\trc = 0;\n\tn = k1_ddr_dfc_plan"),
    ("boot: no MC reset for x8", B, "\t\tk1_ddr_mc_reset(&b.hw);\n", ""),
    ("boot: x8 re-init without overlays", B,
     "k1_ddr_device_init(&b.hw, true, &step)", "k1_ddr_device_init(&b.hw, false, &step)"),
    ("boot: table without MR21", B,
     "k1_ddr_dfc_table_load(&b.hw, rep->size_mb == 16384);",
     "k1_ddr_dfc_table_load(&b.hw, false);"),
    ("boot: MR21 write missing", B,
     "\tif (rep->size_mb == 16384)\n\t\tk1_ddr_mr_write(&b.hw, K1_MR21_16G);\n", ""),
    ("boot: final DFC skipped", B, "\trc = k1_boot_dfc(&b, rep->final_pp);", "\trc = 0;"),
    ("boot: fp3 in LPDDR4X plan", B,
     "\tif (type == K1_DDR_LPDDR4)\n\t\tfps[n++] = 3;", "\tfps[n++] = 3;"),
    ("boot: 2666 not fp2", B, "\tcase 2666:\n", ""),
    ("boot: 2133 fp3 on LPDDR4X", B,
     "return type == K1_DDR_LPDDR4 ? 3 : 0;", "return 3;"),
    ("boot: size not x cs_num", B,
     "rep->size_mb = g.area_mb * cfg->cs_num;", "rep->size_mb = g.area_mb;"),
    ("boot: MC read before clock", B,
     "\trc = k1_ddr_pll_init(&b.hw, cfg->data_rate, &step);",
     "\tb.mc_clocked = true;\n\trc = k1_ddr_pll_init(&b.hw, cfg->data_rate, &step);"),
    ("boot: map before x8 re-init", B,
     "\tif (g.x8) {\n\t\trep->byte_mode = true;\n\t\tk1_ddr_mc_reset(&b.hw);\n"
     "\t\trc = k1_ddr_device_init(&b.hw, true, &step);\n\t\tif (rc)\n"
     "\t\t\treturn k1_boot_end(&b, rc, step);\n\t}\n\tk1_ddr_map_apply(&b.hw, &g);\n",
     "\tk1_ddr_map_apply(&b.hw, &g);\n\tif (g.x8) {\n\t\trep->byte_mode = true;\n"
     "\t\tk1_ddr_mc_reset(&b.hw);\n\t\trc = k1_ddr_device_init(&b.hw, true, &step);\n"
     "\t\tif (rc)\n\t\t\treturn k1_boot_end(&b, rc, step);\n\t}\n"),
    ("boot: LPDDR4 accepted", B,
     "\tif (cfg->type == K1_DDR_LPDDR4) {\n\t\t/*", "\tif (0) {\n\t\t/*"),
    ("final fp: >2666 runs fp2", B,
     "\tdefault:\n\t\treturn 0;",
     "\tdefault:\n\t\treturn data_rate > 2666 ? 2 : 0;"),
    ("dummy: removed", B, DUMMY, ""),
    ("check: inverted", B,
     "\tif (k1_ddrt_check_mr14(&mc, b->env))\n",
     "\tif (!k1_ddrt_check_mr14(&mc, b->env))\n"),
    ("check: removed", B, CHECK, ""),
]


def check_anchors(mutants: list[tuple[str, str, str, str]]) -> int:
    bad = 0
    cache: dict[str, str] = {}
    for name, path, old, new in mutants:
        if path not in cache:
            cache[path] = (SRC / path).read_text()
        n = cache[path].count(old)
        if n != 1:
            print(f"NOANCHOR {name}: {path} matches {n}")
            bad += 1
        if old == new:
            print(f"NOCHANGE {name}")
            bad += 1
    if not bad:
        print(f"{len(mutants)} anchors ok")
    return bad


def run_one(arg: tuple[str, str, str, str]) -> str:
    name, path, old, new = arg
    orig = (SRC / path).read_text()
    tmp = tempfile.mkdtemp(prefix="k1-mut-")
    try:
        dst = Path(tmp) / "m"
        for part in ("src", "tools"):
            shutil.copytree(ROOT / part, dst / part, ignore=shutil.ignore_patterns(
                "__pycache__", "*.pyc", ".git"))
        (dst / "src" / path).write_text(orig.replace(old, new, 1))
        r = subprocess.run(
            ["make", "-s", "-C", str(dst / "src"), "BUILD=" + str(Path(tmp) / "b"),
             "WARN=", "test"],
            capture_output=True, text=True, timeout=300)
        fails = [line.split(": ", 1)[-1]
                 for line in r.stdout.splitlines() if line.startswith("FAIL ")]
        status = "killed  " if r.returncode else "SURVIVED"
        if r.returncode and not fails:
            status = "BUILDERR"
            err = r.stderr.strip().splitlines()
            fails = [err[-1] if err else "?"]
        return f"{status} {name}: {len(fails)} test(s) {'; '.join(fails)[:80]}"
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("match", nargs="?", help="substring of the mutant name")
    ap.add_argument("--check", action="store_true",
                    help="only verify each old string occurs once")
    ap.add_argument("-j", type=int, default=os.cpu_count() or 4)
    args = ap.parse_args()
    mutants = MUTANTS
    if args.match:
        mutants = [m for m in MUTANTS if args.match.lower() in m[0].lower()]
        if not mutants:
            print(f"no mutant matches {args.match!r}")
            return 1
    if args.check:
        return 1 if check_anchors(mutants) else 0
    n_bad = check_anchors(mutants)
    if n_bad:
        return 1
    with cf.ThreadPoolExecutor(max_workers=args.j) as ex:
        results = list(ex.map(run_one, mutants))
    for line in results:
        print(line)
    killed = sum(line.startswith("killed") for line in results)
    print(f"{killed}/{len(results)} killed")
    return 0 if killed == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
