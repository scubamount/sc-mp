#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# What this is: a test of the real sco-relay program, the part the unit tests cannot reach: how it
# starts, what it says when the config or the port is wrong, and that it exits 0 after SIGTERM or
# SIGINT with the shutdown lines in its log.
# Usage: sh test_program.sh <path to the sco-relay program>. CTest runs it as sco_relay_program.
set -u

program=${1:-}
if [ -z "$program" ] || [ ! -x "$program" ]; then
    echo "usage: sh test_program.sh <path to the sco-relay program>" >&2
    exit 2
fi

work=$(mktemp -d)
server_pid=""
status=0
pass=0
fail=0

cleanup() {
    [ -n "$server_pid" ] && kill "$server_pid" 2>/dev/null
    rm -rf "$work"
}
trap cleanup EXIT

check() {   # <description> <condition>: the condition is shell text, run here
    if eval "$2"; then
        pass=$((pass + 1))
        echo "ok   $1"
    else
        fail=$((fail + 1))
        echo "FAIL $1"
    fi
}

contains() {   # <file> <text>
    grep -qF -- "$2" "$1"
}

run() {   # <log> [argument...]: runs the program to the end, keeping its exit status in $status
    log=$1
    shift
    "$program" "$@" > "$log" 2>&1
    status=$?
}

# A port that differs between runs that happen at the same time.
port=$((20000 + $$ % 20000))

write_config() {   # <file>
    printf 'passphrase = a test passphrase\nmax_players = 3\nport = %s\nstats_interval_ms = 1000\n' "$port" > "$1"
}

start_server() {   # <config> <log>: starts the program in the background, waits for "session is up"
    "$program" "$1" > "$2" 2>&1 &
    server_pid=$!
    tries=0
    while [ "$tries" -lt 30 ]; do
        grep -qF "session is up" "$2" 2>/dev/null && return 0
        kill -0 "$server_pid" 2>/dev/null || return 1   # it stopped by itself
        sleep 1
        tries=$((tries + 1))
    done
    return 1
}

stop_server() {   # <signal>: sends it, waits, and keeps the program's exit status in $status
    kill "-$1" "$server_pid"
    status=0
    wait "$server_pid" || status=$?
    server_pid=""
}

# ---- started the wrong way ------------------------------------------------------------------------

run "$work/none.log"
check "no argument exits 2" '[ "$status" -eq 2 ]'

run "$work/help.log" --help
check "--help exits 0 and says how to start" '[ "$status" -eq 0 ] && contains "$work/help.log" "usage: sco-relay"'

run "$work/missing.log" "$work/missing.conf"
check "a missing config file exits 1 and says so" '[ "$status" -eq 1 ] && contains "$work/missing.log" "cannot open"'

printf 'passphrase = short\nmax_players = 3\n' > "$work/bad.conf"
run "$work/bad.log" "$work/bad.conf"
check "a bad config exits 1 and names the line" '[ "$status" -eq 1 ] && contains "$work/bad.log" "line 1"'

# ---- a clean stop ---------------------------------------------------------------------------------

write_config "$work/good.conf"
if start_server "$work/good.conf" "$work/term.log"; then
    check "the server comes up on the port it was given" 'contains "$work/term.log" "listening on UDP port $port"'
    stop_server TERM
    check "SIGTERM exits 0" '[ "$status" -eq 0 ]'
    check "SIGTERM ends the session and logs a last stats line" \
        'contains "$work/term.log" "shutting down" && contains "$work/term.log" "the session has ended" && contains "$work/term.log" "stats: players 0 of 3"'
else
    fail=$((fail + 1))
    echo "FAIL the server did not come up; its log:"
    cat "$work/term.log"
fi

if start_server "$work/good.conf" "$work/int.log"; then
    stop_server INT
    check "SIGINT exits 0" '[ "$status" -eq 0 ]'
else
    fail=$((fail + 1))
    echo "FAIL the server did not come up for the SIGINT run; its log:"
    cat "$work/int.log"
fi

# ---- nearby-only is read from the config file and shows in the log ---------------------------------------

printf 'passphrase = a test passphrase\nmax_players = 3\nport = %s\nnearby_only = on\n' "$port" > "$work/nearby.conf"
if start_server "$work/nearby.conf" "$work/nearby.log"; then
    check "nearby_only = on is announced in the log" 'contains "$work/nearby.log" "nearby-only is on"'
    stop_server TERM
    check "and the last stats line carries the nearby part" 'contains "$work/nearby.log" "| nearby: poses sent 0"'
else
    fail=$((fail + 1))
    echo "FAIL the server did not come up for the nearby-only run; its log:"
    cat "$work/nearby.log"
fi

printf 'passphrase = a test passphrase\nmax_players = 3\nnearby_only = maybe\n' > "$work/nearby-bad.conf"
run "$work/nearby-bad.log" "$work/nearby-bad.conf"
check "nearby_only = maybe exits 1 and names the line" \
    '[ "$status" -eq 1 ] && contains "$work/nearby-bad.log" "line 3" && contains "$work/nearby-bad.log" "nearby_only must be on or off"'

# ---- the port is already taken --------------------------------------------------------------------

if start_server "$work/good.conf" "$work/first.log"; then
    run "$work/second.log" "$work/good.conf"
    check "a taken port exits 1 and says what to do" \
        '[ "$status" -eq 1 ] && contains "$work/second.log" "cannot listen on UDP port" && contains "$work/second.log" "stop the other program"'
    stop_server TERM
else
    fail=$((fail + 1))
    echo "FAIL the server did not come up for the taken-port run; its log:"
    cat "$work/first.log"
fi

echo "sco-relay program tests: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
