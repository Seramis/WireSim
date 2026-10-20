#!/usr/bin/env bash
# Toolchain-specific half of the firmware-build contract (z80asm, cpmtools) -- see
# scripts/build-firmware.sh, the toolchain-agnostic half that calls this. Builds the
# vendored 2063-Z80-cpm boot flash and a CP/M 2.2 SD card image.
#
# Usage: build.sh <firmware-dir> <rom-out-dir> [obj-out-dir]
#   <firmware-dir>  a private copy of the vendored sources (vendor/)
#   <rom-out-dir>   receives z80retro.bin (the image for the flash) and sdcard.img (the SD card)
#   [obj-out-dir]   receives the assembler listings and symbols; a temp dir if omitted
set -euo pipefail

FW_DIR="$1"
ROM_DIR="$2"
OBJ_DIR="${3:-$(mktemp -d -t z80retro-obj.XXXXXX)}"

for tool in z80asm mkfs.cpm cpmcp; do
	command -v "$tool" >/dev/null || { echo "$tool not found; see src/Machines/Z80Retro/AGENTS.md (Build & run)" >&2; exit 1; }
done
mkdir -p "$ROM_DIR" "$OBJ_DIR"
V="$FW_DIR/vendor"
DISKDEF=z80-retro-8k-8m
SLOT_BLOCKS=16384	# one 8 MiB drive
PART_START=2048		# the boot flash expects partition 1 at block 0x800

# The upstream Makefiles' recipe: stamp the banner, assemble with z80asm.
assemble() { # <dir> <name> [extra sed expressions...]
	local dir="$1" name="$2"; shift 2
	(cd "$V/$dir" && sed -e "s/@@DATE@@/$Z80RETRO_CPM_DATE/g" -e "s/@@GIT_VERSION@@/$Z80RETRO_CPM_VERSION/g" "$@" "$name.asm" \
		| z80asm - -o "$name.bin" --list="$OBJ_DIR/$name.lst" --label="$OBJ_DIR/$name.sym" -I../lib -I../libretro >&2)
}
assemble boot firmware -e "s/@@BOOT_MSG@@/Z80 Retro Board 2063.3 (WireSim)/g"
assemble retro retro

# Flash: the boot loader (SD card driver included); the rest of the window stays erased.
cp "$V/boot/firmware.bin" "$ROM_DIR/z80retro.bin"

# Drive A: CP/M system tracks (the BIOS) plus the distribution files.
cd "$V"
rm -f drive.img
mkfs.cpm -f "$DISKDEF" -b retro/retro.bin drive.img
cpmcp -f "$DISKDEF" drive.img cpm-2.2/filesystem/* 0:

# SD card: an MBR whose partition 1 starts at block 0x800 and holds drive A.
card="$ROM_DIR/sdcard.img"
rm -f "$card"
truncate -s $(( (PART_START + SLOT_BLOCKS) * 512 )) "$card"
byte() { printf "\\x$(printf %02x "$1")"; }
le32() { byte $(($1 & 255)); byte $((($1 >> 8) & 255)); byte $((($1 >> 16) & 255)); byte $((($1 >> 24) & 255)); }
{ printf '\0\0\0\0\x83\0\0\0'; le32 $PART_START; le32 $SLOT_BLOCKS; } | dd of="$card" bs=1 seek=446 conv=notrunc status=none
printf '\x55\xaa' | dd of="$card" bs=1 seek=510 conv=notrunc status=none
dd if=drive.img of="$card" bs=512 seek=$PART_START conv=notrunc status=none
