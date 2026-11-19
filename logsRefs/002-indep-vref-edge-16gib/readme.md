<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# 002 — Independent RX/TX Vref + delay edge-search on 16 GiB Micron

- **Date:** 2026-11-19
- **Goal:** Fase 1 (Vref by delay-window width, then re-centre delays) plus
  Fase 6.1 (binary edge search from the PI seed) on the 16 GiB board.
- **Hypothesis / docsClean claim tested:**
  - `eye-training.md`: Rx Vref shifts the delay window; pick the index of
    largest window; VREF0 = sub-PHY A (bytes 0/1), VREF1 = B (bytes 2/3).
  - Tx Vref = MR14 21..36 via `CTL_TXVREF` + `CA_CMD` MRW, then MR14 equal on
    every (rank, ch).
  - Edge search from the PI tap finds the same window as the linear sweep of
    `001`, in much less time.
- **Firmware:** working tree `k1_ddr_eye.c` (`K1_INDEP_EYE`). Hashes in
  `ddr_fw.sha256` / `fsbl.sha256`.
- **Configuration:** FDL, SPL defaults, `MR8 0x58`, 16384 MB, fp2/2400, CS0.

## Procedure

```
python3 tools/vehicle/fdl_run.py build/vehicle/FSBL-eye-vref.bin \
  --log /tmp/k1-16g-eye-vref.log --tty /dev/ttyUSB1 \
  --timeout 300 --reset h --done 'indep: eye training done'
```

UART `/dev/ttyUSB1`. Image falls through to eMMC U-Boot after the eye.

## Result

PI fp2: rx idx 5 (min 11), MR14 30 (min 10). Independent:

```
indep: fp2 rx vref A best 6 (min window 11) widths 8 8 9 9 10 10 11 10 9 9 9 8 7 0 0 0
indep: fp2 rx vref B best 4 (min window 10) widths 8 8 8 9 10 9 10 10 10 9 8 6 3 0 0 0
indep: fp2 tx mr14 best 27 (min window 11) widths 10 10 10 10 10 10 11 11 11 11 11 11 11 11 11 10
indep: fp2 rx b0 centre 18 18 18 17 18 19 18 19
indep: fp2 tx b0 centre 35 35 35 35 36 36 36 36
indep: fp2 tx b1 centre 38 38 38 38 38 37 38 38
indep: eye training done in 14536276 us, verify OK (0/3 patterns bad), MR14 check rc 0
```

RX Vref A=6 / B=4 vs PI's common 5. TX MR14 27 vs PI 30 (flat min-window-11
plateau from 27 through 35). Delay centres match `001`. **14.5 s** vs **36.3 s**
linear (`001`).

## Conclusion

- **Confirmed** on 16 GiB: independent RX/TX Vref + delay, 0 DRAM errors,
  MR14 consistent on all four (cs, ch).
- Sub-PHY Vref split is real (A and B disagree by 2 indices).
- Edge search cuts boot of this stage by ~2.5× without changing the trained
  delay centres.

## Documentation updated

- `src/lib/k1_ddr_eye.c` — Vref sweeps + binary edges.
- `logsRefs/README.md` — index row 002.
