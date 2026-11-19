<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# 004 — Independent WL+Vref+delay at every frequency point (16 GiB)

- **Date:** 2026-11-19
- **Goal:** Fase 4: after the address map, run the independent eye on each
  active fp (slice `phy+fp<<14`). fp0 and fp1 from `boot_on_pass`; fp2 after
  the final DFC (which re-sends MR14 from the bank).
- **Firmware:** working tree, `K1_INDEP_EYE`. Hashes in `ddr_fw.sha256` /
  `fsbl.sha256`.
- **Configuration:** FDL `usb:1-1`, UART `/dev/ttyUSB1`, `MR8 0x58`, 16384 MB,
  2400 MT/s, CS0.

## Procedure

```
python3 tools/vehicle/fdl_run.py build/vehicle/FSBL-eye-fps.bin \
  --log /tmp/k1-16g-eye-fps.log --tty /dev/ttyUSB1 \
  --timeout 180 --reset h --done 'k1-ddr: rc'
```

## Result

All three independent trains **verify OK**, MR14 check rc 0:

| fp | WL centre (width) | RX Vref A/B | TX MR14 | time |
|---|---|---|---|---|
| 0 | 19 21 18 21 (24 21 23 21) | 5 / 4 | 21 (window 24 on every step) | 20.5 s |
| 1 | 19 20 19 20 (25 23 26 23) | 3 / 4 | 21 (window 27 on every step) | 23.8 s |
| 2 | 20 23 19 23 (10 9 10 10) | 6 / 4 | 26 | 16.6 s |

`k1-ddr: rc 0` in 45.0 s total (PI + three independent trains). Pass 0 (pre-map)
is not trained, as expected.

At fp0/fp1 the coarse TX-Vref metric saturates (every MR14 21..36 has the same
min window), so the first index (21) is kept; delay re-centre still verifies.

## Conclusion

- **Confirmed — independent refine works on fp0, fp1 and fp2** on 16 GiB CS0.
- Next: CS1 (F5).

## Documentation updated

- `src/blob/blob.c` — per-fp hook after the map; final fp after last DFC.
- `logsRefs/README.md` — index row 004.
