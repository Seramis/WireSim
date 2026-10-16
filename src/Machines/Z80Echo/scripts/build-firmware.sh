#!/usr/bin/env bash
# Builds a Z80Echo ROM directory -- the toolchain-agnostic half of the
# firmware-build contract (see .claude/skills/machine-engineer): isolate a
# private copy of the sources, then hand off to the toolchain-specific build
# (Firmware/build.sh, sjasmplus). Never touches the tracked Firmware/ directory.
#
# Usage: build-firmware.sh [-o DIR] [-r DIR] [-t|--machinetest] [SCRATCH.asm]
#   -t, --machinetest  Build Firmware/machinetest.asm (Z80Echo.machinetest's
#                      image) instead of the echo firmware.
#   SCRATCH.asm        Build this program instead of the echo firmware. It is
#                      copied into the work directory only, and may
#                      `include "hw.inc"` / `include "rom.inc"` like the tracked
#                      images do.
#   -o, --out DIR      Work directory (default: mktemp). Holds the private copy
#                      of Firmware/, rom/Z80Echo/ (the ROM output, unless -r is
#                      given) and obj/ (<image>.lst, <image>.sym).
#   -r, --rom-out DIR  Write the ROM image here instead of DIR/rom/Z80Echo.
#   -h, --help         Show this help.
#
# Output: prints the ROM directory (DIR/rom/Z80Echo, or the -r DIR) to stdout on
# success -- this is what a MachineHarness .machinetest script's `rom=<DIR>`
# wants. Exit status: Firmware/build.sh's own (0 on success).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
MACHINE="$ROOT/src/Machines/Z80Echo"
OUT=""
ROM_OUT=""
IMAGE=echo
SCRATCH=""

usage() { sed -n '2,23p' "$0" | sed 's/^# \{0,1\}//'; }

while [[ $# -gt 0 ]]; do
	case "$1" in
		-t|--machinetest) IMAGE=machinetest; shift ;;
		-o|--out) OUT="$2"; shift 2 ;;
		-r|--rom-out) ROM_OUT="$2"; shift 2 ;;
		-h|--help) usage; exit 0 ;;
		-*) echo "Unknown argument: $1" >&2; usage >&2; exit 2 ;;
		*) SCRATCH="$(realpath "$1")"; IMAGE=scratch; shift ;;
	esac
done

OUT="$(realpath -m "${OUT:-$(mktemp -d -t wiresim-z80echo.XXXXXX)}")" # absolute: Firmware/build.sh changes directory
ROM_DIR="$(realpath -m "${ROM_OUT:-$OUT/rom/Z80Echo}")"
OBJ_DIR="$OUT/obj"
mkdir -p "$ROM_DIR" "$OBJ_DIR"

rm -rf "$OUT/Firmware"
mkdir -p "$OUT/Firmware"
cp "$MACHINE"/Firmware/*.asm "$MACHINE"/Firmware/*.inc "$OUT/Firmware/"
[[ -n "$SCRATCH" ]] && cp "$SCRATCH" "$OUT/Firmware/scratch.asm"
export Z80ECHO_IMAGE="$IMAGE"
"$MACHINE/Firmware/build.sh" "$OUT/Firmware" "$ROM_DIR" "$OBJ_DIR"

echo "[build-firmware] work dir: $OUT (obj/$IMAGE.lst, obj/$IMAGE.sym)" >&2
echo "$ROM_DIR"
