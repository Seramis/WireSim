#!/usr/bin/env bash
# Measures CPU cycle counts between opcode-fetch addresses in a sim-run.sh trace.
#
# Reading instruction timing off the trace by hand means: find the address in
# obj/main.lst, grep "CPU PHI2=false R <addr>" for its opcode-fetch line, note
# the c= value, do the same for the next instruction's opcode fetch, subtract.
# This script does that lookup+subtraction for a whole sequence of addresses
# in one shot, so the only manual step left is picking the right addresses.
#
# Usage: src/Machines/My6502/scripts/cycle-delta.sh TRACE.log ADDR [ADDR...]
#   TRACE.log   trace.log from a sim-run.sh -o DIR run (or any redirected
#               sim-run.sh stdout containing "CPU PHI2=false" lines).
#   ADDR        Lowercase hex addresses, e.g. c10f, exactly as they appear in
#               the trace. Pass the opcode-fetch address of every instruction
#               boundary you care about, in execution order; the delta before
#               address N is the cycle count of the instruction *starting* at
#               address N-1 (i.e. everything executed between the two fetches).
#
# Addresses are matched by FIRST occurrence in the trace. For straight-line
# test code (the asm-test skill's recommended shape) that's exactly right;
# it is NOT correct if the same address is fetched twice before the region
# you're measuring ends (loops, re-entrant code) -- pre-filter the trace with
# `sed -n 'START,STOPp'` by line number in that case.
#
# Example:
#   src/Machines/My6502/scripts/sim-run.sh -c 80 -o /tmp/t test.asm > /tmp/t/out.txt
#   src/Machines/My6502/scripts/cycle-delta.sh /tmp/t/out.txt c10f c112 c114
#   # c10f: h=54 c=27
#   # c10f -> c112: 4 cycles  (h=62 c=31)
#   # c112 -> c114: ...
set -euo pipefail

if [[ $# -lt 2 ]]; then
	echo "Usage: $0 TRACE.log ADDR [ADDR...]" >&2
	exit 2
fi

TRACE="$1"; shift
[[ -f "$TRACE" ]] || { echo "No such trace file: $TRACE" >&2; exit 2; }

prev_c=""
prev_addr=""
for addr in "$@"; do
	line="$(grep -m1 -E "CPU PHI2=false [RW] ${addr} " "$TRACE" || true)"
	if [[ -z "$line" ]]; then
		echo "No 'CPU PHI2=false R/W $addr' line found in $TRACE" >&2
		exit 1
	fi
	h="$(sed -E 's/.*h=([0-9]+).*/\1/' <<<"$line")"
	c="$(sed -E 's/.*c=([0-9]+).*/\1/' <<<"$line")"
	if [[ -n "$prev_c" ]]; then
		echo "$prev_addr -> $addr: $((c - prev_c)) cycles  (h=$h c=$c)"
	else
		echo "$addr: h=$h c=$c"
	fi
	prev_c="$c"
	prev_addr="$addr"
done
