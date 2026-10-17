#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# What this is: the proof that the sco-core boundary checks fire when they should. Each case alters a
# private copy of the sco-core source kit's files (never the extracted kit itself) and expects a failure that names what was
# altered and points to server/README.md, '## The sco-core boundary'. The first part runs
# server/tools/check-net-boundary.sh (no compiler) on the copies: a declaration, a constant, a line of
# behaviour, a file changed anywhere else, and a file gone. The second part compiles
# server/net_boundary.cpp (syntax only) against altered headers, to prove the static_asserts fire, and
# compiles net_boundary.h with no sco-core include path at all. CTest runs it as net_boundary_checks.
# Usage: sh test_net_boundary_checks.sh <repository root> <extracted sco-core source kit> [C++ compiler]
#        Without a compiler only the first part runs.
set -u

root=${1:-}
core=${2:-}
cxx=${3:-}
if [ -z "$root" ] || [ -z "$core" ] || [ ! -f "$root/server/tools/check-net-boundary.sh" ] || [ ! -f "$core/include/sco/net/core.h" ]; then
    echo "usage: sh test_net_boundary_checks.sh <repository root> <extracted sco-core source kit> [C++ compiler]" >&2
    exit 2
fi
checker="$root/server/tools/check-net-boundary.sh"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
pass=0
fail=0

ok() { pass=$((pass + 1)); echo "ok   $1"; }
bad() { fail=$((fail + 1)); echo "FAIL $1"; }

# Says ok when `condition` (shell text) holds.
check() {   # <description> <condition>
    if eval "$2"; then ok "$1"; else bad "$1"; fi
}

# A private copy of every file the checker reads, laid out as in sco-core. Prints its path.
copy_of_the_files() {   # <name>
    dir="$work/$1"
    mkdir -p "$dir"
    sh "$checker" --list-files > "$work/list.txt"
    while IFS= read -r file; do
        mkdir -p "$dir/$(dirname "$file")"
        cp "$core/$file" "$dir/$file"
    done < "$work/list.txt"
    echo "$dir"
}

run_checker() {   # <sco-core dir>: leaves the output in $out and the exit status in $status
    out=$(sh "$checker" "$1" 2>&1)
    status=$?
}

says() {   # <text>: true when the checker's output holds it
    case "$out" in *"$1"*) return 0 ;; esac
    return 1
}

# ---- part 1: the script, on altered copies ------------------------------------------------------------

run_checker "$core"
check "the real sco-core source kit passes" '[ "$status" -eq 0 ]'

dir=$(copy_of_the_files unaltered)
run_checker "$dir"
check "an unaltered copy passes" '[ "$status" -eq 0 ]'

dir=$(copy_of_the_files declaration)
sed 's/bool Host(const Options& opts, uint64_t nowMs);/bool Host(const Options\& opts);/' "$dir/include/sco/net/core.h" > "$work/edit" && cp "$work/edit" "$dir/include/sco/net/core.h"
run_checker "$dir"
check "a changed declaration fails" '[ "$status" -eq 1 ]'
check "  and names Core::Host and the file" 'says "Core::Host, hosting a session" && says "include/sco/net/core.h"'
check "  and points to server/README.md, The sco-core boundary" 'says "server/README.md" && says "The sco-core boundary"'

dir=$(copy_of_the_files constant)
sed 's/\(#define SC_NET_MAX_PEERS[ ]*\)16u/\117u/' "$dir/include/sc_net.h" > "$work/edit" && cp "$work/edit" "$dir/include/sc_net.h"
run_checker "$dir"
check "a changed constant fails" '[ "$status" -eq 1 ]'
check "  and names SC_NET_MAX_PEERS and the limit the build expects" 'says "SC_NET_MAX_PEERS" && says "this build expects 16"'
check "  and points to server/README.md, The sco-core boundary" 'says "server/README.md" && says "The sco-core boundary"'

dir=$(copy_of_the_files behaviour)
sed 's/\(b.id == linkId || \)!b\.registered/\1b.registered/' "$dir/src/net/core.cpp" > "$work/edit" && cp "$work/edit" "$dir/src/net/core.cpp"
run_checker "$dir"
check "a changed line of behaviour fails" '[ "$status" -eq 1 ]'
check "  and says what Core no longer does" 'says "Core relays to every other player that registered the channel" && says "src/net/core.cpp"'
check "  and points to server/README.md, The sco-core boundary" 'says "server/README.md" && says "The sco-core boundary"'

dir=$(copy_of_the_files validname)
sed 's/return sc_net_valid_name(name.data(), name.size()) != 0;/return true;/' "$dir/src/net/wire.cpp" > "$work/edit" && cp "$work/edit" "$dir/src/net/wire.cpp"
run_checker "$dir"
check "ValidName changed to accept any name fails" '[ "$status" -eq 1 ]'
check "  and names ValidName and src/net/wire.cpp" 'says "ValidName, a legal player name" && says "src/net/wire.cpp"'
check "  and points to server/README.md, The sco-core boundary" 'says "server/README.md" && says "The sco-core boundary"'

dir=$(copy_of_the_files fromhost)
sed 's/origin != kHostPeer) {/origin != 0) {/' "$dir/src/net/core.cpp" > "$work/edit" && cp "$work/edit" "$dir/src/net/core.cpp"
run_checker "$dir"
check "a player that no longer drops forged FROM_HOST data fails" '[ "$status" -eq 1 ]'
check "  and says what Core no longer does" 'says "drops data on it whose origin is not the host" && says "src/net/core.cpp"'

dir=$(copy_of_the_files scopedefault)
sed 's/bool any = false;/bool any = true; /' "$dir/include/sco/net/scope.h" > "$work/edit" && cp "$work/edit" "$dir/include/sco/net/scope.h"
run_checker "$dir"
check "a Scope that lets every address in by default fails" '[ "$status" -eq 1 ]'
check "  and names Scope::any" 'says "Scope::any" && says "include/sco/net/scope.h"'

dir=$(copy_of_the_files elsewhere)
echo "// a change the line checks do not look at" >> "$dir/include/sco/net/udp.h"
run_checker "$dir"
check "a change anywhere else in a file the boundary reads fails" '[ "$status" -eq 1 ]'
check "  and names the file" 'says "include/sco/net/udp.h" && says "has changed"'

dir=$(copy_of_the_files gone)
rm "$dir/src/net/scope.cpp"
run_checker "$dir"
check "a file that is gone fails" '[ "$status" -eq 1 ] && says "src/net/scope.cpp is gone"'

# ---- part 2: the compiler, on altered headers ---------------------------------------------------------

if [ -z "$cxx" ]; then
    echo "note: no C++ compiler given, so the compile-time checks are not run"
else
    compile() {   # <log> <more compiler arguments...>: syntax check only, leaves the status in $status
        log=$1
        shift
        "$cxx" -std=c++20 -fsyntax-only -I"$root" "$@" > "$log" 2>&1
        status=$?
    }
    logged() {   # <log> <text>
        grep -qF -- "$2" "$1"
    }
    compiles() {   # <description> <log> <more compiler arguments...>: ok when they compile; shows the compiler's words when not
        description=$1
        log=$2
        shift 2
        compile "$log" "$@"
        if [ "$status" -eq 0 ]; then
            ok "$description"
        else
            bad "$description"
            head -n 30 "$log" | sed 's/^/    | /'
        fi
    }

    compiles "net_boundary.h and relay_wire.h compile with no sco-core include path" "$work/header.log"         "$root/server/tests/net_boundary_header_only.cpp"

    printf '#include "sco/net/core.h"\n' > "$work/needs_core.cpp"
    compile "$work/needs_core.log" "$work/needs_core.cpp"
    check "  and that include path really cannot see sco-core" '[ "$status" -ne 0 ]'

    compiles "net_boundary.cpp compiles against the real sco-core" "$work/real.log" -I"$core/include" "$root/server/net_boundary.cpp"

    rm -rf "$work/limit"
    mkdir -p "$work/limit"
    cp -R "$core/include" "$work/limit/include"
    sed 's/\(#define SC_NET_MAX_PEERS[ ]*\)16u/\117u/' "$work/limit/include/sc_net.h" > "$work/edit" && cp "$work/edit" "$work/limit/include/sc_net.h"
    compile "$work/limit.log" -I"$work/limit/include" "$root/server/net_boundary.cpp"
    check "a library with another session limit does not compile" '[ "$status" -ne 0 ]'
    check "  and the message says so and points to server/README.md, The sco-core boundary" \
        'logged "$work/limit.log" "session limit (kMaxPeers) is not the one this build expects" && logged "$work/limit.log" "The sco-core boundary"'
    compiles "  and one definition, SCO_RELAY_EXPECTED_SESSION_LIMIT, makes the build expect it" "$work/limit17.log"         -I"$work/limit/include" -DSCO_RELAY_EXPECTED_SESSION_LIMIT=17 "$root/server/net_boundary.cpp"

    rm -rf "$work/fifty"
    mkdir -p "$work/fifty"
    cp -R "$core/include" "$work/fifty/include"
    sed 's/\(#define SC_NET_MAX_PEERS[ ]*\)16u/\150u/' "$work/fifty/include/sc_net.h" > "$work/edit" && cp "$work/edit" "$work/fifty/include/sc_net.h"
    compiles "a private copy of the library with 50 seats compiles with that one definition" "$work/fifty.log"         -I"$work/fifty/include" -DSCO_RELAY_EXPECTED_SESSION_LIMIT=50 "$root/server/net_boundary.cpp"

    rm -rf "$work/signature"
    mkdir -p "$work/signature"
    cp -R "$core/include" "$work/signature/include"
    sed 's/bool Host(const Options& opts, uint64_t nowMs);/bool Host(const Options\& opts);/' "$work/signature/include/sco/net/core.h" > "$work/edit" && cp "$work/edit" "$work/signature/include/sco/net/core.h"
    compile "$work/signature.log" -I"$work/signature/include" "$root/server/net_boundary.cpp"
    check "a library whose Core::Host changed does not compile" '[ "$status" -ne 0 ]'
    check "  and the message names Core::Host and points to server/README.md, The sco-core boundary" \
        'logged "$work/signature.log" "Core::Host changed its parameters" && logged "$work/signature.log" "The sco-core boundary"'
fi

echo "net boundary checks: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
