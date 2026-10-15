#!/usr/bin/env bash
# Toolchain-specific half of the firmware-build contract (ca65/ld65) -- see
# scripts/build-firmware.sh, the toolchain-agnostic half that calls this.
# Takes exactly two required positional arguments (a firmware source
# directory and a ROM output directory) plus one optional third (a
# directory for assembler listings, for callers that want them -- a fresh
# temp directory is used and discarded if omitted). Produces the flat bank
# binaries ParallelSram loads; nothing else is this script's concern.
#
# Usage: build.sh <firmware-dir> <rom-out-dir> [obj-out-dir]
#
# $DEBUG_MODE (0 or 1, default 1) selects real kernel+WozMon vs debug.asm --
# set by scripts/build-firmware.sh, defaulted here for a direct manual
# invocation.
set -euo pipefail

FW_DIR="$1"
ROM_DIR="$2"
OBJ_DIR="${3:-$(mktemp -d -t my6502-obj.XXXXXX)}"
DEBUG_MODE="${DEBUG_MODE:-1}"

mkdir -p "$ROM_DIR" "$OBJ_DIR"

ca65 -D DEBUG_MODE="$DEBUG_MODE" "$FW_DIR/main.asm" -l "$OBJ_DIR/main.lst" -o "$OBJ_DIR/main.o" >&2
ld65 -C "$FW_DIR/linker.cfg" -Ln "$OBJ_DIR/labels.lst" -m "$OBJ_DIR/main.map" \
	"$OBJ_DIR/main.o" -o "$OBJ_DIR/main" 2> >(grep -v "does not exist" >&2)
cp "$OBJ_DIR"/main.bank*.bin "$ROM_DIR/"
