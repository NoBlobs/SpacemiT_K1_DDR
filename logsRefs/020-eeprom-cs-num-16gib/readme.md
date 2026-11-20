<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# 020 — EEPROM `cs_num` (no vehicle override) on the 16 GiB Micron

- **Date:** 2026-11-20
- **Goal:** with `k1_set_mr14()` following `cs_sel`, boot without `CS=` so the
  SPL's EEPROM `TLV_CODE_DDR_CSNUM` (or default 2) chooses the rank count.
- **Firmware:** working tree, `FSBL-gpl-full.bin`. Hashes in `ddr_fw.sha256` /
  `fsbl.sha256`.
- **Configuration:** FDL `usb:1-1`, UART `/dev/ttyUSB1`, 2400 MT/s. Product-name
  map failed (`Fail to detect board:-2`); DDR TLV still supplied `cs_num`.

## Procedure

```
MODE=full tools/vehicle/build-vehicle.sh
ANDROID_SERIAL=usb:1-1 python3 tools/vehicle/fdl_run.py \
  build/vehicle/FSBL-gpl-full.bin --tty /dev/ttyUSB1 --reset h \
  --done K1DDRT-DONE --log logsRefs/020-eeprom-cs-num-16gib/serial-train.log
```

## Result

`cs_num 2`, map 16384 MB, CS1 present (`cs1 0x00110001`), SR enter `0x77`.
PI TX both ranks, independent verify **OK** CS0 and CS1 at fp0/fp1/fp2,
`MR14 check rc 0`. `k1-ddr: rc 0` in **3.26 s**. DRAM probes 0 errors after
`ddr_init` and after the PI retrain (including `0x40000000`).

## Conclusion

- **Confirmed — EEPROM rank count on this 16 GiB package is 2**, and
  `k1_ca_mr14(2)` (`0x1302000e`) trains both ranks.
- This is not a silicon probe of CS1; the SPL still trusts the EEPROM.
