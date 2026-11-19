<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# 003 — Independent write leveling + Vref + delay on 16 GiB Micron

- **Date:** 2026-11-19
- **Goal:** Fase 2: centre CS0 WL (`byte+0x24`, 5-bit field) by memory-pattern
  edge search, then the F1 Vref + delay path. Gate stays with the PI (F3).
- **Hypothesis / docsClean claim tested:** `write-leveling.md` "Independent
  measurement of the WL window": contiguous 5-bit window, PI tap within 2 codes
  of the centre; at fp2/2400 widths ~8–12, codes ~17–24 on Micron.
- **Firmware:** working tree `k1_ddr_eye.c` (`K1_INDEP_EYE`). Hashes in
  `ddr_fw.sha256` / `fsbl.sha256`.
- **Configuration:** FDL `usb:1-1`, UART `/dev/ttyUSB1`, `MR8 0x58`, 16384 MB,
  fp2/2400, CS0.

## Procedure

```
python3 tools/vehicle/fdl_run.py build/vehicle/FSBL-eye-wl.bin \
  --log /tmp/k1-16g-eye-wl.log --tty /dev/ttyUSB1 \
  --timeout 300 --reset h --done 'indep: eye training done'
```

## Result

PI fp2 CS0 WL `20 24 19 22` (byte 1 = 24, outside the 8 GiB 17–23 band).
Independent:

```
indep: fp2 wl centre 20 23 19 22 width 10 9 10 10
indep: fp2 rx vref A best 5 (min window 11) ...
indep: fp2 rx vref B best 5 (min window 10) ...
indep: fp2 tx mr14 best 25 (min window 11) ...
indep: eye training done in 15034709 us, verify OK (0/3 patterns bad), MR14 check rc 0
```

WL centres sit on the PI taps except byte 1 (23 vs 24), all widths 9–10, all
inside 19–23. Verify 0 errors. **15.0 s**.

## Conclusion

- **Confirmed — independent WL works on 16 GiB CS0/fp2** by the same write
  pattern as TX. The memory-pattern metric does discriminate; no need to leave
  WL on the PI for this board/rate.
- Gate remains PI-only (F3). Next: all fps (F4), then CS1 (F5).

## Documentation updated

- `logsRefs/README.md` — index row 003.
