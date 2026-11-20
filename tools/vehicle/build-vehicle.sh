#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Thalles Campagnani
#
# Validation vehicle for "fastboot stage" in BootROM download mode, a signed
# FSBL with the mainline SPL and:
#
#   MODE=retrain  proprietary ddr_fw.bin (controller init only), then the
#                 open firmware re-trains        -> FSBL-gpl-test.bin
#   MODE=full     the open firmware as ddr_fw.bin: PLL, Device_Init, every
#                 pass and DFC from power-on     -> FSBL-gpl-full.bin
#   MODE=emmc     full, then an eMMC monitor on the UART (emmc_tool.py)
#                                                -> FSBL-gpl-emmc.bin
#   MODE=uboot    full, then U-Boot proper (OpenSBI + U-Boot, u-boot.itb)
#                 over ymodem on the UART        -> FSBL-gpl-uboot.bin
#                 needs OPENSBI=/path/to/fw_dynamic.bin (FW_TEXT_START=0)
#
#   EXP=NAME      build the open firmware with -DNAME (one-off experiment)
#                                                -> FSBL-gpl-<mode>-exp.bin
#   RATE=MT/s     the SPL asks ddr_init for this data_rate (default: the
#                 SPL's, 2400 without EEPROM)    -> FSBL-gpl-<mode>...-<rate>.bin
#   CS=1|2        the SPL asks ddr_init for this cs_num (default: EEPROM,
#                 2 without EEPROM)              -> FSBL-gpl-<mode>...-csN.bin
#   LAT=1         DRAM load latency (pointer chase) before K1DDRT-DONE
#                                                -> FSBL-gpl-<mode>...-lat.bin
#   RESET=        no reset after the run (default: on, off for emmc and
#                 uboot); with
#                 it the board goes back to the BootROM (doc 32)
#
# Required:
#   UBOOT=        mainline U-Boot checkout (validated at 8d534cec0cc); it is
#                 copied to $WORK and u-boot/*.patch is applied to the copy
#   VENDOR_UBOOT= vendor U-Boot tree that holds the FSBL signing tool and key
#                 config (tools/build_binary_file.py, board/spacemit/k1-x/configs)
#   DDR_FW=       MODE=retrain only: the proprietary ddr_fw.bin
#                 (default: ../../build/vehicle/ddr_fw.bin)
#
# The vendor signing tool and its key config are only executed, on a copy.
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
IMPL=$(cd "$HERE/../.." && pwd)
WORK=${WORK:-$HOME/k1-cleanroom-build}
: "${UBOOT:?set UBOOT=/path/to/mainline/u-boot}"
: "${VENDOR_UBOOT:?set VENDOR_UBOOT=/path/to/vendor/u-boot (signing tool)}"
MODE=${MODE:-retrain}
OUT=$IMPL/build/vehicle
# relative to src/: make targets cannot hold the space in the checkout path
BLOB_BUILD=../build
# empty: src/Makefile production default (-DK1_INDEP_EYE). Passing BLOB_DEFS=
# to make would override that and drop the independent eye.
BLOB_DEFS=
SUFFIX=
# one-off silicon experiment: a define for the open firmware, own outputs
if [ -n "${EXP:-}" ]; then
	exp_tag=$(printf '%s' "${EXP#K1_EXP_}" | tr 'A-Z_' 'a-z-')
	BLOB_BUILD=$BLOB_BUILD/exp-$exp_tag
	BLOB_DEFS="${BLOB_DEFS:--DK1_INDEP_EYE} -D$EXP"
	SUFFIX=$SUFFIX-exp-$exp_tag
fi
RATE=${RATE:-}
if [ -n "$RATE" ]; then
	SUFFIX=$SUFFIX-$RATE
fi
CS=${CS:-}
if [ -n "$CS" ]; then
	SUFFIX=$SUFFIX-cs$CS
fi
LAT=${LAT:-}
if [ -n "$LAT" ]; then
	SUFFIX=$SUFFIX-lat
fi
GPL_FW=$IMPL/src/$BLOB_BUILD/blob/k1_ddr_fw.bin
EMMC=
UART=
case "$MODE" in
retrain)
	DDR_FW=${DDR_FW:-$IMPL/build/vehicle/ddr_fw.bin}
	FULL=
	FSBL=$OUT/FSBL-gpl-test$SUFFIX.bin ;;
full)
	DDR_FW=$GPL_FW
	FULL=1
	FSBL=$OUT/FSBL-gpl-full$SUFFIX.bin ;;
emmc)
	DDR_FW=$GPL_FW
	FULL=1
	EMMC=1
	FSBL=$OUT/FSBL-gpl-emmc$SUFFIX.bin ;;
uboot)
	DDR_FW=$GPL_FW
	FULL=1
	UART=1
	: "${OPENSBI:?MODE=uboot needs OPENSBI=/path/to/fw_dynamic.bin}"
	export OPENSBI
	FSBL=$OUT/FSBL-gpl-uboot$SUFFIX.bin ;;
*) echo "MODE must be retrain, full, emmc or uboot"; exit 1 ;;
esac
if [ -n "$EMMC$UART" ]; then
	RESET=${RESET-}
else
	RESET=${RESET-1}
fi
CROSS=${CROSS_COMPILE:-riscv64-linux-gnu-}
case "$WORK" in
*" "* | *:*) echo "WORK must not contain spaces or colons (U-Boot kbuild)"; exit 1 ;;
esac

if [ -n "$BLOB_DEFS" ]; then
	make -C "$IMPL/src" blob BUILD="$BLOB_BUILD" BLOB_DEFS="$BLOB_DEFS"
else
	make -C "$IMPL/src" blob BUILD="$BLOB_BUILD"
fi

mkdir -p "$WORK" "$OUT"
rsync -a --delete --exclude .git "$UBOOT/" "$WORK/u-boot/"
# vehicle hooks in board/spacemit/k1; skip if UBOOT already carries them
# (the copy has no .git; the ceiling keeps git from finding a parent repo)
for p in "$HERE"/u-boot/*.patch; do
	if (cd "$WORK/u-boot" && patch -p1 --dry-run --reverse --fuzz=3 \
		< "$p" >/dev/null 2>&1); then
		echo "  already applied: $(basename "$p")"
	else
		(cd "$WORK/u-boot" && patch -p1 --fuzz=3 < "$p")
	fi
done
cp "$DDR_FW" "$WORK/ddr_fw.bin"
cp "$GPL_FW" "$WORK/ddr_gpl.bin"

# in-tree dtc, but binman uses the host python libfdt: no swig needed
# OpenSSL 4 removed ENGINE; stubs keep mkimage/dumpimage linking.
HOSTCFLAGS="${HOSTCFLAGS:-} -DOPENSSL_ENGINE_STUBS"

make -C "$WORK/u-boot" O="$WORK/out" ARCH=riscv CROSS_COMPILE="$CROSS" \
	HOSTCFLAGS="$HOSTCFLAGS" \
	spacemit_k1_defconfig
if [ -n "$EMMC" ]; then
	"$WORK/u-boot/scripts/config" --file "$WORK/out/.config" \
		-e SPL_MMC_WRITE
fi
if [ -n "$UART" ]; then
	# the ymodem state alone outgrows the 1 KiB BSS at the top of SRAM:
	# a 2 KiB BSS just under the old stack top, the early stack below it
	"$WORK/u-boot/scripts/config" --file "$WORK/out/.config" \
		-e SPL_YMODEM_SUPPORT \
		--set-val SPL_BSS_START_ADDR 0xc083f300 \
		--set-val SPL_BSS_MAX_SIZE 0x800 \
		--set-val SPL_STACK 0xc083f200
	# U-Boot proper: exercise the DRAM, and the P1 reset (doc 32) by hand
	"$WORK/u-boot/scripts/config" --file "$WORK/out/.config" \
		-e CMD_MEMTEST -e CMD_PMIC -e CMD_I2C -e CMD_SBI -e CMD_TIME -e CMD_CACHE
fi
if [ -n "$EMMC$UART" ]; then
	make -C "$WORK/u-boot" O="$WORK/out" ARCH=riscv \
		CROSS_COMPILE="$CROSS" olddefconfig
fi
make -C "$WORK/u-boot" O="$WORK/out" ARCH=riscv CROSS_COMPILE="$CROSS" \
	HOSTCFLAGS="$HOSTCFLAGS" \
	CONFIG_PYLIBFDT= \
	DDR_FW_FILE="$WORK/ddr_fw.bin" DDR_GPL_FILE="$WORK/ddr_gpl.bin" \
	DDR_GPL_FULL="$FULL" K1_EMMC_TOOL="$EMMC" K1_VEHICLE_RESET="$RESET" \
	K1_VEHICLE_UART="$UART" K1_VEHICLE_RATE="$RATE" K1_VEHICLE_CS="$CS" \
	K1_VEHICLE_LAT="$LAT" \
	BINMAN_ALLOW_MISSING=1 -j"$(nproc)"

SIGN=$WORK/sign
rm -rf "$SIGN"
mkdir -p "$SIGN/configs"
cp -r "$VENDOR_UBOOT/board/spacemit/k1-x/configs/key" "$SIGN/configs/"
sed 's/u-boot-spl\.bin/u-boot-spl-ddr.bin/g' \
	"$VENDOR_UBOOT/board/spacemit/k1-x/configs/fsbl.json" \
	> "$SIGN/configs/fsbl_ddr.json"
cp "$WORK/out/u-boot-spl-ddr.bin" "$SIGN/"
python3 "$VENDOR_UBOOT/tools/build_binary_file.py" \
	-c "$SIGN/configs/fsbl_ddr.json" -o "$FSBL"

if [ -n "$UART" ]; then
	cp "$WORK/out/u-boot.itb" "$OUT/u-boot$SUFFIX.itb"
	ls -l "$OUT/u-boot$SUFFIX.itb"
fi
ls -l "$WORK/out/u-boot-spl-ddr.bin" "$FSBL"
