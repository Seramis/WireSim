#!/usr/bin/env bash
# Builds a My6502 ROM directory from Firmware/ -- the toolchain-agnostic half
# of the firmware-build contract (see .claude/skills/machine-engineer):
# isolate a private copy of Firmware/, optionally swap in a scratch entry
# file, then hand off to the toolchain-specific build (Firmware/build.sh,
# ca65/ld65 for this machine). It never touches the repo's rom/ folder or
# the tracked Firmware/debug.asm. A different machine's own
# scripts/build-firmware.sh follows this same shape; only its own
# Firmware/build.sh differs.
#
# Usage: build-firmware.sh [-o DIR] [-r DIR] [-k|--kernel] [TEST.asm]
#   TEST.asm       Swaps in for Firmware/debug.asm (debug mode only). Never
#                  touches the tracked copy -- a private copy is built fresh
#                  every call.
#   -k, --kernel   Build the real kernel + WozMon (DEBUG_MODE=0) instead of
#                  debug.asm (DEBUG_MODE=1, the default). Mutually exclusive
#                  with TEST.asm.
#   -o, --out DIR  Work directory (default: mktemp). Holds the private
#                  Firmware/ copy (replaced on every call), rom/My6502/ (the
#                  ROM output, unless -r is given), and obj/ (assembler
#                  listings -- main.lst, labels.lst, main.map).
#   -r, --rom-out DIR  Write the ROM images here instead of DIR/rom/My6502.
#   -h, --help     Show this help.
#
# Output: prints the ROM directory (DIR/rom/My6502, or the -r DIR) to stdout on success --
# this is what a MachineHarness .machinetest script's `rom=<DIR>` wants.
# Exit status: Firmware/build.sh's own (0 on success).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
FIRMWARE_SRC="$ROOT/src/Machines/My6502/Firmware"
KERNEL=0
OUT=""
ROM_OUT=""
TEST_ASM=""

usage() { sed -n '2,27p' "$0" | sed 's/^# \{0,1\}//'; }

while [[ $# -gt 0 ]]; do
	case "$1" in
		-k|--kernel) KERNEL=1; shift ;;
		-o|--out) OUT="$2"; shift 2 ;;
		-r|--rom-out) ROM_OUT="$2"; shift 2 ;;
		-h|--help) usage; exit 0 ;;
		-*) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
		*) TEST_ASM="$(realpath -e "$1")" || { echo "Test program not found: $1" >&2; exit 2; }; shift ;;
	esac
done

if [[ $KERNEL -eq 1 && -n "$TEST_ASM" ]]; then
	echo "--kernel and TEST.asm are mutually exclusive" >&2
	exit 2
fi

OUT="${OUT:-$(mktemp -d -t wiresim.XXXXXX)}"
ROM_DIR="${ROM_OUT:-$OUT/rom/My6502}"
OBJ_DIR="$OUT/obj"
mkdir -p "$ROM_DIR" "$OBJ_DIR"

# Private copy of Firmware/ so a scratch TEST.asm (or anything the inner
# build does) never touches the tracked sources.
rm -rf "$OUT/Firmware"
cp -r "$FIRMWARE_SRC" "$OUT/Firmware"
[[ -n "$TEST_ASM" ]] && cp "$TEST_ASM" "$OUT/Firmware/debug.asm"

# DEBUG_MODE is a ca65-specific preprocessor define (main.asm selects real
# firmware vs debug.asm by it) -- toolchain detail, so it's passed to the
# inner script via the environment rather than as a positional argument,
# keeping Firmware/build.sh's own two required arguments generic.
export DEBUG_MODE=$(( KERNEL ? 0 : 1 ))
"$FIRMWARE_SRC/build.sh" "$OUT/Firmware" "$ROM_DIR" "$OBJ_DIR"

echo "[build-firmware] work dir: $OUT (obj/main.lst, obj/labels.lst)" >&2
echo "$ROM_DIR"
