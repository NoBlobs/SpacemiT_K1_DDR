<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# 021 — EEPROM `cs_num` (no vehicle override) on the 8 GiB Samsung

- **Date:** 2026-11-20
- **Goal:** same `k1_set_mr14()` / `k1_ca_mr14(cs_num)` image as
  [020](../020-eeprom-cs-num-16gib/readme.md), no `CS=`, on the 8 GiB reference
  board. UART is `/dev/ttyUSB0` (the 16 GiB board uses `/dev/ttyUSB1`).
- **Firmware:** working tree, `FSBL-gpl-full.bin`. Hashes in `ddr_fw.sha256` /
  `fsbl.sha256`.
- **Configuration:** FDL `usb:1-1`, UART `/dev/ttyUSB0`, 2400 MT/s.

## Procedure

```
ANDROID_SERIAL=usb:1-1 python3 tools/vehicle/fdl_run.py \
  build/vehicle/FSBL-gpl-full.bin --tty /dev/ttyUSB0 --reset h \
  --done K1DDRT-DONE --log logsRefs/021-eeprom-cs-num-8gib/serial-train.log
```

## Result

`cs_num 2`, `MR5 0x01` `MR8 0x52` (x8, 8 Gb/die), 8192 MB, CS1 present
(`cs1 0x00100001`), SR enter `0x77`. Independent verify **OK** CS0 and CS1 at
fp0/fp1/fp2 (`CS1` CPU buffer `0x188000000`), `MR14 check rc 0`.
`k1-ddr: rc 0` in **3.34 s**. DRAM probes 0 errors after `ddr_init` and after
the PI retrain.

A first FDL of this image listened on `ttyUSB1` and captured nothing; Linux
then came up on `ttyUSB0`. This run used `ttyUSB0`.

## Conclusion

- **Confirmed — 8 GiB Samsung, EEPROM/default `cs_num 2`**, and
  `k1_ca_mr14(2)` trains both ranks with the independent eye.
