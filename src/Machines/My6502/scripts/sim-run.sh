#!/usr/bin/env bash
# Headless build + assemble + run helper for agents and CI-style use.
#
# Builds WireSim (CMake preset debug/release), assembles a ROM into an isolated
# work directory and runs the simulator there with a cycle limit. It never touches the repo's rom/ folder,
# the My6502 Firmware/debug.asm, or the generated listings under Firmware/.
#
# Usage: src/Machines/My6502/scripts/sim-run.sh [options] [TEST.asm]
#   TEST.asm         Test program to use instead of Firmware/debug.asm (debug mode only).
#   -c, --cycles N   CPU cycle limit (default 200). Always bounded; no interactive mode.
#   -k, --kernel     Assemble the real kernel + WozMon (DEBUG_MODE=0, quiet trace).
#   -r, --release    Use the Release build (build/release) instead of Debug (build/debug).
#   -o, --out DIR    Work directory (default: mktemp). Holds rom/, obj/ and trace.log.
#   -n, --no-build   Skip the CMake configure/build step.
#   -h, --help       Show this help.
#
# Output: the simulator trace is printed to stdout and saved to DIR/trace.log.
# Exit status: the simulator's exit status (0 when the cycle limit is reached).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
CYCLES=200
KERNEL=0
BUILD_TYPE=Debug
OUT=""
BUILD=1
TEST_ASM=""

usage() { sed -n '2,18p' "$0" | sed 's/^# \{0,1\}//'; }

while [[ $# -gt 0 ]]; do
	case "$1" in
		-c|--cycles) CYCLES="$2"; shift 2 ;;
		-k|--kernel) KERNEL=1; shift ;;
		-r|--release) BUILD_TYPE=Release; shift ;;
		-o|--out) OUT="$2"; shift 2 ;;
		-n|--no-build) BUILD=0; shift ;;
		-h|--help) usage; exit 0 ;;
		-*) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
		*) TEST_ASM="$(realpath -e "$1")" || { echo "Test program not found: $1" >&2; exit 2; }; shift ;;
	esac
done

if [[ $KERNEL -eq 1 && -n "$TEST_ASM" ]]; then
	echo "--kernel and TEST.asm are mutually exclusive" >&2
	exit 2
fi

BUILD_DIR="$ROOT/build/${BUILD_TYPE,,}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="${OUT:-$(mktemp -d -t wiresim.XXXXXX)}"

# 1. Build the simulator (CMakePresets.json presets "debug"/"release").
if [[ $BUILD -eq 1 ]]; then
	if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
		cmake --preset "${BUILD_TYPE,,}" -S "$ROOT" >&2
	fi
	cmake --build --preset "${BUILD_TYPE,,}" --target WireSim >&2
fi

# 2. Assemble, via the toolchain-agnostic/toolchain-specific split
#    (build-firmware.sh + Firmware/build.sh -- see .claude/skills/machine-engineer).
#    Reuses this same $OUT, so its private Firmware/ copy and obj/ listings
#    land exactly where this script already documents them.
if [[ $KERNEL -eq 1 ]]; then
	"$SCRIPT_DIR/build-firmware.sh" -o "$OUT" -k >/dev/null
elif [[ -n "$TEST_ASM" ]]; then
	"$SCRIPT_DIR/build-firmware.sh" -o "$OUT" "$TEST_ASM" >/dev/null
else
	"$SCRIPT_DIR/build-firmware.sh" -o "$OUT" >/dev/null
fi

# 3. Run. WireSim quits on stdin EOF, so hold stdin open with a FIFO.
#    The cd keeps debug_writer.log and friends inside the work directory.
mkfifo "$OUT/stdin"
exec 3<>"$OUT/stdin"
set +e
(cd "$OUT" && "$BUILD_DIR/WireSim" --rom "$OUT/rom/My6502" --cycles "$CYCLES" <&3) 2>&1 | tee "$OUT/trace.log"
STATUS=${PIPESTATUS[0]}
set -e
exec 3>&-
rm -f "$OUT/stdin"

echo "[sim-run] work dir: $OUT (trace.log, obj/main.lst, obj/labels.lst)" >&2
exit "$STATUS"
