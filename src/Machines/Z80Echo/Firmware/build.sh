#!/usr/bin/env bash
# Toolchain-specific half of the firmware-build contract (sjasmplus) -- see
# scripts/build-firmware.sh, the toolchain-agnostic half that calls this.
# $Z80ECHO_IMAGE picks the image: echo (echo.asm, default), machinetest
# (machinetest.asm) or scratch (scratch.asm).
#
# Usage: build.sh <firmware-dir> <rom-out-dir> [obj-out-dir]
#   <firmware-dir>  a private copy of this directory's .asm / .inc files
#   <rom-out-dir>   receives z80echo.bin, the 32 KiB image for $0000-$7FFF
#   [obj-out-dir]   receives the assembler listing (<image>.lst) and symbols
#                   (<image>.sym); a temp dir if omitted
set -euo pipefail

FW_DIR="$1"
ROM_DIR="$2"
OBJ_DIR="${3:-$(mktemp -d -t z80echo-obj.XXXXXX)}"
IMAGE="${Z80ECHO_IMAGE:-echo}"

case "$IMAGE" in
	echo|machinetest|scratch) ;;
	*) echo "Unknown Z80ECHO_IMAGE: $IMAGE (echo|machinetest|scratch)" >&2; exit 2 ;;
esac

if ! command -v sjasmplus >/dev/null; then
	echo "sjasmplus not found; see src/Machines/Z80Echo/AGENTS.md (Build & run)" >&2
	exit 1
fi

mkdir -p "$ROM_DIR" "$OBJ_DIR"

# Run from the firmware directory so `include` resolves next to the sources.
cd "$FW_DIR"
sjasmplus --nologo --msg=war \
	-DROM_BIN="\"$ROM_DIR/z80echo.bin\"" \
	--lst="$OBJ_DIR/$IMAGE.lst" --sym="$OBJ_DIR/$IMAGE.sym" \
	"$IMAGE.asm" >&2
