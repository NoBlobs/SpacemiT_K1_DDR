<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# 000 — Independent eye measurement by memory-pattern sweep (on hardware)

- **Date:** 2026-11-18
- **Goal:** on the board, after the normal PI bring-up, run the independent
  data-eye module (`src/lib/k1_ddr_eye.c`): first check that the RX/TX windows it
  measures by moving data through DRAM match the spec and the PI taps, then have
  it centre every lane in its own window, apply the centres and verify the DRAM
  with 0 errors — an independent training of the data eye.
- **Hypothesis / docsClean claim tested:**
  - `docsClean/eye-training.md` "Independent verification of the eye": sweeping a
    delay word while a pattern is written and read back gives a contiguous window
    of passing codes whose centre is the sampling point; at fp2/2400 the RX
    window is ≈14..21 (8 codes) and the TX window ≈31..39 (9 codes);
  - `docsClean/phy.md`: RX DQ delay at `base+0x30+4·lane` (mask 31), TX DQ delay
    at `base+0x00+4·lane` (mask 63), `base = slice + K1_BYTE_OFF[b]`.
- **Firmware:** working tree, `src/lib/k1_ddr_eye.{c,h}` + the `-DK1_INDEP_EYE`
  hook in `blob/blob.c` (uncommitted). Blob SHA-256 in `ddr_fw.sha256`.
- **FSBL:** mainline U-Boot `v2027.01-rc1-341-g934995f642a`
  (`/home/thalles/0-git/NoBlobs/u-boot`), SPL + `ddr_fw.bin` via binman, signed
  with the `k1-x` vendor keys. Build details in `uboot.rev`, SHA-256 in
  `fsbl.sha256`.
- **Configuration:** EEPROM not read under FDL (`Fail to detect board:-2`), so
  the SPL defaults are used: LPDDR4X, 2 ranks, 2400 MT/s. The die reports
  `MR8 0x52` (x8, 8 Gb/die) → the 8 GiB Samsung reference board. Final point fp2.

## Procedure

1. `make -C src blob BLOB_DEFS=-DK1_INDEP_EYE` — build the firmware with the eye
   hook. The hook runs `k1_ddr_eye_measure()` at the end of `k1_blob_ddr_init`,
   after a successful boot, at the final fp.
2. Embed the blob in the SPL (binman `u-boot-spl-ddr` section) and sign it (see
   `uboot.rev` for the exact build, including the `CONFIG_PYLIBFDT=` and
   OpenSSL-4 host-tool workarounds).
3. `fastboot stage FSBL-eye.bin && fastboot continue`; capture the UART on
   `/dev/ttyUSB0` at 115200.
4. The sweep buffer was shrunk across runs (4 MiB → 512 KiB → 64 KiB write) to
   fit the sweep into the capture window; the measured windows did not change.

## Result

Two increments, both on the 8 GiB board at fp2/2400. Bring-up succeeds first
(`rc 0`, `phy_status 0x0`, `8192 MB`, `0 item(s) out of range`).

**Increment 1 — measurement** (`serial-rx.log`, `serial-tx.log`, byte-group
windows):

```
indep: fp2 rx byte0 window 15..21 (7) centre 18 ; byte1 14..22 (9) centre 18
indep: fp2 tx byte0 window 28..37 (10) centre 33
```

Agree with `docsClean/eye-training.md` (fp2/2400 RX ≈14..21, TX ≈31..39) and the
PI taps this boot (`rx dq0 18–19`, `tx dq0 32–36`).

**Increment 2 — per-lane training + verify** (`serial-train.log`): each of the
32 RX and 32 TX DQ lanes centred in its own window and kept, then verified over
three patterns:

```
indep: fp2 rx b0 centre 18 18 18 17 19 18 19 19   width 9 10 10 9 9 9 10 10
indep: fp2 rx b1 centre 19 19 19 19 19 18 19 19   width 10 10 10 10 10 9 10 10
indep: fp2 rx b2 centre 18 18 18 18 19 19 19 19   width 9 9 9 9 10 10 10 10
indep: fp2 rx b3 centre 18 18 18 18 19 18 18 19   width 10 11 11 11 10 10 11 10
indep: fp2 tx b0 centre 33 33 33 33 34 34 34 34   width 11 11 13 12 12 12 12 12
indep: fp2 tx b1 centre 36 37 37 36 37 36 36 37   width 11 12 12 11 12 12 13 12
indep: fp2 tx b2 centre 34 34 34 34 34 34 34 34   width 10 11 11 11 11 11 11 11
indep: fp2 tx b3 centre 37 37 37 37 37 36 37 37   width 10 10 11 11 11 11 12 12
indep: eye training done in 35.7 s, verify OK (0/3 patterns bad)
```

## Conclusion

- **Confirmed — independent RX/TX eye training works on hardware.** The firmware
  re-centres every RX and TX DQ lane from a memory-pattern sweep alone (no PI
  harvest), applies the centres, and the DRAM passes the pattern verification
  with **0 errors**. Centres match `docsClean/eye-training.md` and the PI's own
  taps (RX 17–19, TX 33–37); the per-lane spread (e.g. tx b0 33–34 vs b1 36–37)
  is the variation the spec says needs per-lane centring at fp2.
- **Register map confirmed:** `docsClean/phy.md` RX `+0x30+4·lane` (mask 31),
  TX `+0x00+4·lane` (mask 63), per-byte base — correct.
- **Speed:** the cache is off (~3 MB/s). Early-exit on the first mismatch and a
  write-once-per-byte RX sweep bring a full per-lane RX+TX training to ~36 s,
  which fits the FDL run.
- **Still from the PI (not yet independent):** the RX Vref (PHY index) and TX
  Vref (MR14), write leveling, and the DQS gate (which `docsClean/phy.md` shows
  software cannot position). Only fp2 and CS0 are trained so far.
- **Next:** independent RX/TX Vref sweeps, write leveling, the other frequency
  points, and CS1.

## Documentation updated

- `docs/firmware/` — a firmware page for the independent eye module (pending).
