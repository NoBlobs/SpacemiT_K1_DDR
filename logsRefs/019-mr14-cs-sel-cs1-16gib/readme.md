<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# 019 — PI MR14 CA command follows cs_sel (`cs_num 1`, 16 GiB Micron)

- **Date:** 2026-11-20
- **Goal:** silicon check that `k1_set_mr14()` now sends
  `K1_CA_MR_WRITE | (cs_sel << 24) | 14` (`0x1102000e` for one rank) instead of
  the two-rank constant `0x1302000e`. Same vehicle as [018](../018-indep-cs-num-1-16gib/readme.md).
- **Firmware:** working tree, `FSBL-gpl-full-cs1.bin`. Hashes in `ddr_fw.sha256`
  / `fsbl.sha256`.
- **Configuration:** FDL `usb:1-1`, UART `/dev/ttyUSB1`, 2400 MT/s, `MR8 0x58`,
  SPL `CS=1` on the two-rank Micron package.

## Procedure

```
MODE=full CS=1 tools/vehicle/build-vehicle.sh
ANDROID_SERIAL=usb:1-1 python3 tools/vehicle/fdl_run.py \
  build/vehicle/FSBL-gpl-full-cs1.bin --tty /dev/ttyUSB1 --reset h \
  --done K1DDRT-DONE --log logsRefs/019-mr14-cs-sel-cs1-16gib/serial-train.log
```

## Result

PI TX sweeps at every fp completed (`pi tx ops 17`, `tx mr14` 21/27/29),
`MR14 check rc 0`, independent CS0 verify **OK** at fp0/fp1/fp2.
`k1-ddr: rc 0` in **2.14 s**, 8192 MB, DRAM probes after `ddr_init`: 0 errors.
SR enter status `0x37` (one rank). After the vehicle PI retrain: the same two
mid-map probes as 018 lose kept content (`0x40000000` and `0x1c0000000`,
16384 kept-errors each); fresh writes still 0 errors; retrain `rc 0`.

## Conclusion

- **Confirmed — PI MR14 write with `cs_num 1` trains and verifies on this
  package.** The one-rank CA command does not break the PI TX sweep.
- The retrain kept-errors of 018 are **not** caused by the old two-rank MR14
  constant: they remain after this change.
