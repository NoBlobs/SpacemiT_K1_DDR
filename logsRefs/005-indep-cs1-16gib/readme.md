<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# 005 — Independent WL+Vref+delay on both ranks (16 GiB)

- **Date:** 2026-11-19
- **Goal:** Fase 5: train CS1 at the CS1 CPU buffer (`0x288000000` = 10 GiB +
  128 MiB) and PHY slice `+(cs<<8)`, after CS0. Vref still swept on the CS0
  buffer; WL and RX/TX delay per rank.
- **Firmware:** working tree, `K1_INDEP_EYE`. Hashes in `ddr_fw.sha256` /
  `fsbl.sha256`.
- **Configuration:** FDL `usb:1-1`, UART `/dev/ttyUSB1`, `MR8 0x58`, 16384 MB,
  2400 MT/s, `cs_num 2`.

## Procedure

```
python3 tools/vehicle/fdl_run.py build/vehicle/FSBL-eye-cs1.bin \
  --log /tmp/k1-16g-eye-cs1.log --tty /dev/ttyUSB1 \
  --timeout 240 --reset h --done 'k1-ddr: rc'
```

## Result

Both ranks, all three fps, **verify OK** (0/3 patterns, 0 rank-fails), MR14
check rc 0. CS1 buffer `0x288000000`.

| fp | CS0 WL (width) | CS1 WL (width) | RX Vref A/B | TX MR14 | time |
|---|---|---|---|---|---|
| 0 | 19 21 18 21 (24 21 23 21) | 19 21 19 21 (24 21 23 21) | 5 / 5 | 21 (window ~24) | 30.1 s |
| 1 | 19 20 19 20 (26 23 26 23) | 19 20 19 20 (26 23 26 23) | 3 / 4 | 21 (window ~27) | 33.3 s |
| 2 | 20 24 19 23 (10 10 10 10) | 20 24 19 22 (10 10 10 10) | 6 / 5 | 27 (window 11) | 24.0 s |

`k1-ddr: rc 0` in **64.0 s** total. Pass 0 (pre-map) is not independently
trained. eMMC U-Boot still reports 8 GiB.

Samsung doc-21 still flags Micron CS0/CS1 b1 WL 24 and TX ~38–39 at fp2; pattern
verify is 0 errors on both ranks.

## Conclusion

- **Confirmed — CS1 independent refine works** on 16 GiB at the mapped CS1
  CPU address, all fps.
- Next: F6 (cache/flush, two-stage, production path).

## Documentation updated

- `src/lib/k1_ddr_eye.c` — per-rank PHY offset + CS1 buffer.
- `logsRefs/README.md` — index row 005.
