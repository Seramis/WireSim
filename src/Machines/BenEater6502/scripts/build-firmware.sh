#!/usr/bin/env bash
# Builds a BenEater6502 ROM directory from the vendored msbasic submodule --
# the toolchain-agnostic half of the firmware-build contract (see
# .claude/skills/machine-engineer): isolate a private copy of the sources,
# then hand off to the toolchain-specific build (Firmware/build.sh, ca65/ld65).
# Never touches rom/ or the submodule checkout. This machine runs the vendored
# firmware unmodified, so unlike My6502 there is no swappable scratch entry file;
# -t instead selects the tracked wiring-regression image used by the .machinetest.
#
# Usage: build-firmware.sh [-o DIR] [-r DIR] [-t|--machinetest]
#   -t, --machinetest  Build Firmware/machinetest.s (BenEater6502.machinetest's
#                      image) instead of Microsoft BASIC + WozMon.
#   -o, --out DIR      Work directory (default: mktemp). Holds the private copy
#                      of msbasic/ (replaced on every call), rom/BenEater6502/
#                      (the ROM output, unless -r is given) and obj/ (eater.o,
#                      eater.lbl).
#   -r, --rom-out DIR  Write the ROM image here instead of DIR/rom/BenEater6502.
#   -h, --help         Show this help.
#
# Output: prints the ROM directory (DIR/rom/BenEater6502, or the -r DIR) to stdout on success --
# this is what a MachineHarness .machinetest script's `rom=<DIR>` wants.
# Exit status: Firmware/build.sh's own (0 on success).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
MACHINE="$ROOT/src/Machines/BenEater6502"
OUT=""
ROM_OUT=""
IMAGE=basic

usage() { sed -n '2,23p' "$0" | sed 's/^# \{0,1\}//'; }

while [[ $# -gt 0 ]]; do
	case "$1" in
		-t|--machinetest) IMAGE=machinetest; shift ;;
		-o|--out) OUT="$2"; shift 2 ;;
		-r|--rom-out) ROM_OUT="$2"; shift 2 ;;
		-h|--help) usage; exit 0 ;;
		*) echo "Unknown argument: $1" >&2; usage >&2; exit 2 ;;
	esac
done

if [[ ! -f "$MACHINE/vendor/msbasic/msbasic.s" ]]; then
	echo "vendor/msbasic is empty; run: git submodule update --init" >&2
	exit 1
fi

OUT="$(realpath -m "${OUT:-$(mktemp -d -t wiresim-eater.XXXXXX)}")" # absolute: Firmware/build.sh changes directory
ROM_DIR="$(realpath -m "${ROM_OUT:-$OUT/rom/BenEater6502}")" # absolute: Firmware/build.sh changes directory
OBJ_DIR="$OUT/obj"
mkdir -p "$ROM_DIR" "$OBJ_DIR"

rm -rf "$OUT/msbasic"
cp -r "$MACHINE/vendor/msbasic" "$OUT/msbasic"
cp "$MACHINE"/Firmware/machinetest.* "$OUT/msbasic/"
export EATER_IMAGE="$IMAGE"
"$MACHINE/Firmware/build.sh" "$OUT/msbasic" "$ROM_DIR" "$OBJ_DIR"

echo "[build-firmware] work dir: $OUT (obj/eater.lbl)" >&2
echo "$ROM_DIR"
