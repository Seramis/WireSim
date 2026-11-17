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

    # TcpTerminal sends a greeting on accept. A port forwarder can accept a TCP
    # connection even when no simulator is listening, so also bound the wait for
    # the first output. Once output arrives, an idle session stays connected.
    # raw,echo=0 passes Ctrl+C through; icrnl translates Enter; Ctrl+D disconnects.
    coproc SESSION {
        exec socat 'FD:0,raw,echo=0,icrnl,escape=0x04!!FD:1' TCP:localhost:"$PORT",connect-timeout=1 </dev/tty 2>/dev/null
    }
    session_pid=$SESSION_PID
    exec {session_output}<&"${SESSION[0]}"
    session_input=${SESSION[1]}
    exec {session_input}>&-

    if IFS= read -r -N 1 -t 3 -u "$session_output" first_byte; then
        printf '%s' "$first_byte"
        cat <&"$session_output"
    else
        kill "$session_pid" 2>/dev/null || true
    fi
    exec {session_output}<&-
    wait "$session_pid"
    exit_code=$?

    # socat returns exit code 0 when connected and closed normally.
    # If a connection was established, add a clean line before new output.
    if [ $exit_code -eq 0 ]; then
        echo ""
        dots_printed=0
    fi

    sleep 1 || break
done