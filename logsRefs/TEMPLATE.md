<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# NNN — <experiment name>

- **Date:**
- **Goal:**
- **Hypothesis / docsClean claim tested:** (file and section)
- **Firmware:** commit `…` (or "working tree"), blob SHA-256 `…` (`ddr_fw.sha256`)
- **FSBL:** U-Boot rev `…`, build fragment, SHA-256 `…` (`fsbl.sha256`, `uboot.rev`)
- **Configuration:** memory type, ranks, width, data rate / final fp, other inputs

## Procedure

Commands run, power cycles, anything done by hand. The firmware is loaded over
FDL (`fastboot stage` / `fastboot continue`) and the UART captured on
`/dev/ttyUSB0`.

## Result

Key lines of `serial*.log`.

## Conclusion

Verdict (confirmed / refuted / partial / inconclusive) and what it means.

## Documentation updated

- `docs/...rst` — section
