#!/usr/bin/env bash
# Builds the Z80Retro ROM directory -- the toolchain-agnostic half of the
# firmware-build contract (see .claude/skills/machine-engineer): isolate a
# private copy of the sources, then hand off to the toolchain-specific build
# (Firmware/build.sh, z80asm and cpmtools). Never touches the tracked Firmware/
# directory.
#
# Builds the vendored 2063-Z80-cpm boot flash (z80retro.bin) and a bootable CP/M
# 2.2 SD card image (sdcard.img). Needs z80asm, cpmtools and the
# vendor/2063-Z80-cpm submodule (git submodule update --init --recursive).
#
# Usage: build-firmware.sh [-o DIR] [-r DIR]
#   -o, --out DIR      Work directory (default: mktemp). Holds the private copy
#                      of the sources and obj/ (firmware.lst, retro.lst and
#                      symbols).
#   -r, --rom-out DIR  Write the ROM image here instead of DIR/rom/Z80Retro.
#   -h, --help         Show this help.
#
# Output: prints the ROM directory (DIR/rom/Z80Retro, or the -r DIR) to stdout on
# success -- this is what a MachineHarness .machinetest script's `rom=<DIR>`
# wants. Exit status: Firmware/build.sh's own (0 on success).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
MACHINE="$ROOT/src/Machines/Z80Retro"
OUT=""
ROM_OUT=""

usage() { sed -n '2,21p' "$0" | sed 's/^# \{0,1\}//'; }

while [[ $# -gt 0 ]]; do
	case "$1" in
		-o|--out) OUT="$2"; shift 2 ;;
		-r|--rom-out) ROM_OUT="$2"; shift 2 ;;
		-h|--help) usage; exit 0 ;;
		*) echo "Unknown argument: $1" >&2; usage >&2; exit 2 ;;
	esac
done

OUT="$(realpath -m "${OUT:-$(mktemp -d -t wiresim-z80retro.XXXXXX)}")" # absolute: Firmware/build.sh changes directory
ROM_DIR="$(realpath -m "${ROM_OUT:-$OUT/rom/Z80Retro}")"
OBJ_DIR="$OUT/obj"
mkdir -p "$ROM_DIR" "$OBJ_DIR"

VENDOR="$MACHINE/vendor/2063-Z80-cpm"
if [[ ! -f "$VENDOR/cpm-2.2/src/cpm22.asm" ]]; then
	echo "CP/M sources missing; run: git submodule update --init --recursive" >&2
	exit 1
fi

rm -rf "$OUT/Firmware"
mkdir -p "$OUT/Firmware/vendor/cpm-2.2"
# Only what the boot flash, the BIOS and the disk image need, minus the submodules' .git files.
cp -r "$VENDOR"/{boot,retro,lib,libretro} "$OUT/Firmware/vendor/"
cp -r "$VENDOR"/cpm-2.2/{src,filesystem} "$OUT/Firmware/vendor/cpm-2.2/"
cp "$VENDOR/filesystem/diskdefs" "$OUT/Firmware/vendor/"
# Stamped into the boot banner; the commit's own date keeps the image reproducible.
export Z80RETRO_CPM_VERSION="$(git -C "$VENDOR" describe --tags --always 2>/dev/null || echo unknown)"
export Z80RETRO_CPM_DATE="$(git -C "$VENDOR" show -s --format=%ci 2>/dev/null || echo unknown)"
"$MACHINE/Firmware/build.sh" "$OUT/Firmware" "$ROM_DIR" "$OBJ_DIR"

echo "[build-firmware] work dir: $OUT (obj/firmware.lst, obj/retro.lst)" >&2
echo "$ROM_DIR"
