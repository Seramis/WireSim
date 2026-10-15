#!/bin/bash

# Check if a port was provided
if [ -z "$1" ]; then
    echo "Usage: $0 <port>"
    exit 1
fi

PORT=$1

# 1. Set the erase character to Backspace (Ctrl+H)
# 2. Disable the 'literal next' (lnext) feature
# (socat's raw mode below turns ISIG off, so Ctrl+C reaches the machine as byte 0x03
# instead of raising SIGINT; the script only sees SIGINT between socat sessions.)
# We use a trap to ensure 'stty sane' runs even if the script exits unexpectedly
trap 'stty sane' EXIT
stty erase ^H lnext undef

echo "Connecting to localhost:$PORT..."
echo "Ctrl+C is sent to the machine (it stops a running BASIC program)."
echo "Press Ctrl+D to disconnect, then Ctrl+C during the reconnect pause to stop the loop."

dots_printed=0

# The Reconnect Loop
while true; do
    # Get current terminal column count (defaults to 80 if undetermined)
    cols=$(tput cols 2>/dev/null || echo 80)

    # Print reconnect message if we are at the start of a new dot line
    if [ "$dots_printed" -eq 0 ]; then
        echo "--- Connection lost. Reconnecting in 1 second... (Hit Ctrl+C to stop) ---"
    fi

    # Print a single period without a newline and flush stdout
    printf "."
    ((dots_printed++))

    # If the line is full of dots, reset the counter and print a newline
    if [ "$dots_printed" -ge "$cols" ]; then
        echo ""
        dots_printed=0
    fi

    # Run socat
    # raw,echo=0 passes every key through, Ctrl+C included (0x03 stops MS BASIC).
    # icrnl turns Enter (CR) into LF, the line ending the simulated machines expect.
    # escape=0x04 (Ctrl+D) exits the current socat session.
    socat STDIO,raw,echo=0,icrnl,escape=0x04 TCP:localhost:"$PORT" 2>/dev/null
    exit_code=$?

    # socat returns exit code 0 when connected and closed normally.
    # If a connection was established, add a clean line before new output.
    if [ $exit_code -eq 0 ]; then
        echo ""
        dots_printed=0
    fi

    sleep 1 || break
done