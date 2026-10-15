#!/usr/bin/env bash
# Toolchain-specific half of the firmware-build contract (ca65/ld65) -- see
# scripts/build-firmware.sh, the toolchain-agnostic half that calls this.
# By default builds the vendored msbasic `eater` target (Ben Eater's Microsoft
# BASIC + WozMon port) unmodified. $EATER_IMAGE=machinetest builds the small
# wiring-regression image (machinetest.s) instead.
#
# Usage: build.sh <firmware-dir> <rom-out-dir> [obj-out-dir]
#   <firmware-dir>  a private copy of vendor/msbasic, with this directory's
#                   machinetest.s/.cfg copied alongside
#   <rom-out-dir>   receives eater.bin, the 32 KiB image for $8000-$FFFF
#   [obj-out-dir]   receives the object file and eater.lbl (a temp dir if omitted)
set -euo pipefail

FW_DIR="$1"
ROM_DIR="$2"
OBJ_DIR="${3:-$(mktemp -d -t beater-obj.XXXXXX)}"
EATER_IMAGE="${EATER_IMAGE:-basic}"

mkdir -p "$ROM_DIR" "$OBJ_DIR"

cd "$FW_DIR"
case "$EATER_IMAGE" in
	basic)
		# msbasic selects its target with a lowercase define: defines.s tests .def(eater).
		ca65 -D eater msbasic.s -o "$OBJ_DIR/eater.o" >&2
		ld65 -C eater.cfg "$OBJ_DIR/eater.o" -o "$ROM_DIR/eater.bin" -Ln "$OBJ_DIR/eater.lbl" >&2
		;;
	machinetest)
		ca65 machinetest.s -l "$OBJ_DIR/machinetest.lst" -o "$OBJ_DIR/eater.o" >&2
		ld65 -C machinetest.cfg "$OBJ_DIR/eater.o" -o "$ROM_DIR/eater.bin" -Ln "$OBJ_DIR/eater.lbl" >&2
		;;
	*) echo "Unknown EATER_IMAGE: $EATER_IMAGE (basic|machinetest)" >&2; exit 2 ;;
esac
