<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# 001 — Independent RX/TX delay eye on the 16 GiB Micron board (baseline)

- **Date:** 2026-11-19
- **Goal:** run the same independent delay-only eye (`K1_INDEP_EYE`, no Vref
  sweep yet) that `000` verified on 8 GiB, on the BPI-F3 16 GiB Micron board.
- **Hypothesis / docsClean claim tested:**
  - `docsClean/eye-training.md` / `configurations.md`: 16 GiB needs per-lane
    centring at fp2; CS0 at 128 MiB is in the first rank.
  - Same RX/TX delay map as `000` (`phy.md`).
- **Firmware:** same signed image as `000` (`FSBL-eye.bin`).
- **FSBL:** hashes in `fsbl.sha256` / `ddr_fw.sha256`; U-Boot in `uboot.rev`.
- **Configuration:** FDL, no EEPROM (`Fail to detect board:-2`) → SPL defaults
  2 ranks, 2400 MT/s. Die: `MR5 0xff` `MR8 0x58` → 16384 MB x8 Micron.

## Procedure

Board in BootROM FDL (`361c:1001`, `usb:1-1`), UART `/dev/ttyUSB1` 115200.

```
python3 tools/vehicle/fdl_run.py build/vehicle/FSBL-eye.bin \
  --log /tmp/k1-16g-eye-baseline.log --tty /dev/ttyUSB1 --timeout 180 --reset h
```

This image does not print `K1DDRT-DONE`; after the eye it falls through to the
eMMC vendor U-Boot. Capture is the SPL UART up to `verify OK`.

## Result

Bring-up `rc 0`, `16384 MB`, fp2. PI fp2: rx vref idx 5 (min 11), tx MR14 30
(min 10). Doc-21 ranges (from the 8 GiB Samsung board) flag 11 items on this
Micron part (gate codes and TX DQ0 37 on byte 1).

Independent delay training (CS0, fp2), then 3-pattern verify:

```
indep: fp2 rx b0 centre 17 17 18 17 18 18 18 18   width 11 11 10 10 10 11 11 11
indep: fp2 rx b1 centre 17 18 18 17 18 17 18 18   width 11 10 10 11 11 11 10 10
indep: fp2 rx b2 centre 18 18 18 18 18 19 18 19   width 11 10 10 10 11 10 11 10
indep: fp2 rx b3 centre 18 17 18 18 18 17 18 18   width 10 9 10 10 11 9 10 10
indep: fp2 tx b0 centre 35 36 35 35 36 36 36 36   width 12 12 11 12 12 12 12 12
indep: fp2 tx b1 centre 38 38 38 38 38 37 38 38   width 12 12 12 11 11 11 12 12
indep: fp2 tx b2 centre 33 34 34 33 34 34 34 34   width 11 11 12 11 11 11 11 11
indep: fp2 tx b3 centre 37 37 37 37 37 36 37 37   width 12 12 12 12 12 13 12 12
indep: eye training done in 36296184 us, verify OK (0/3 patterns bad)
```

RX matches the PI taps (17–19) and is a bit wider than 8 GiB (9–11 vs 9–10).
TX spread is larger than Samsung: byte 1 centres at 37–38 (PI also reported
DQ0 37, outside the 8 GiB 30–36 band). Verify still 0 errors.

## Conclusion

- **Confirmed — delay-only independent eye works on 16 GiB Micron**, CS0/fp2,
  0 DRAM errors, ~36 s (cache off, linear sweep).
- Per-lane TX on byte 1 sits ~3–4 codes above bytes 2; this is why the spec
  wants per-lane training here, and why 8 GiB acceptance numbers are not a
  Micron fail.
- **Still from the PI:** RX/TX Vref, write leveling, DQS gate; only fp2/CS0.

## Documentation updated

- `logsRefs/README.md` — index row 001.
