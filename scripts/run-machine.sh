#!/usr/bin/env bash
# Runs a machine executable: run-machine.sh <exe> [args...]
# The machines quit when stdin reaches EOF. On a terminal that never happens, otherwise
# (a build output pane, CI) stdin is replaced by a pipe that stays open until the machine
# exits, so the machine runs until it is stopped.
set -u
if [[ -t 0 ]]; then exec "$@"; fi
dir="$(mktemp -d)"
mkfifo "$dir/stdin"
sleep 2147483647 > "$dir/stdin" &
holder=$!
"$@" < "$dir/stdin"
status=$?
kill "$holder" 2>/dev/null
rm -rf "$dir"
exit $status
