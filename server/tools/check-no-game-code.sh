#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# What this is: proves the relay server holds no game code. From the build: the sco-core source kit was configured
# without its game pack, the program links only our server library, the kit's network library and the kit's two
# socket files (as CMake declares it, with the libraries each target passes on, and as the program's real link line
# says where the generator writes one), and those libraries hold only the objects they should. From the sources: only
# server/net_boundary.cpp names the kit's network library (includes a sco/net header or sc_net.h, or writes
# sco::net::, even in a comment), and every #include in server/ is one of our own headers, the shared relay wire
# header, or a standard or POSIX header (net_boundary.cpp also the library's). An include that climbs out of its
# folder with ".." is never admitted, whatever it points at. Exit 0 means every check passed.
#
# Only the production set is read: server/*.cpp, server/*.h and cmake/ScoCoreSource.cmake. Not server/tests,
# server/tools or any document.
#
# Usage: sh server/tools/check-no-game-code.sh <repository root> <build facts file>
# The facts file is written by CMake (server/tests/CMakeLists.txt) when it generates the build: where the program
# and the libraries are, what each links and passes on to what links it, the program's link line file, and the
# extracted kit. CTest runs this script as relay_no_game_code.
set -u

root=${1:-}
facts=${2:-}
if [ -z "$root" ] || [ -z "$facts" ] || [ ! -d "$root/server" ]; then
    echo "usage: sh server/tools/check-no-game-code.sh <repository root> <build facts file>" >&2
    exit 2
fi
if [ ! -f "$facts" ]; then
    echo "check-no-game-code: no build facts file $facts. Configure and generate the build first (cmake -S . -B build)." >&2
    exit 2
fi
root=$(cd "$root" && pwd)

fails=0
ok() { echo "ok:   $*"; }
fail() { echo "FAIL: $*" >&2; fails=$((fails + 1)); }

fact() {   # <key>: the value after "key=" in the facts file, empty when the key has none
    sed -n "s/^$1=//p" "$facts" | head -n 1
}

# ---- from the build ---------------------------------------------------------------------------------

cache=$(fact cache)
if [ -f "$cache" ] && grep -q '^SCO_GAME_SC:BOOL=OFF$' "$cache"; then
    ok "the kit was configured with SCO_GAME_SC=OFF"
else
    fail "SCO_GAME_SC is not OFF in the build's CMakeCache.txt ($cache)"
fi
if [ -f "$cache" ] && grep -q '^SCO_BUILD_TESTS:BOOL=OFF$' "$cache"; then
    ok "the kit's own tests are off (SCO_BUILD_TESTS=OFF)"
else
    fail "SCO_BUILD_TESTS is not OFF in the build's CMakeCache.txt ($cache)"
fi

# What each target links, as CMake knows it, and what each passes on to whatever links it (the INTERFACE libraries,
# which reach the program's link line through the target that lists them). Nothing else may end up on that line.
if [ "$(fact program_sources)" = "main.cpp" ] && [ "$(fact program_links)" = "sco_relay_server" ] && [ -z "$(fact program_interface_links)" ] &&
   [ "$(fact server_links)" = "sco_net;sco_relay_sockets" ] && [ "$(fact server_interface_links)" = "sco_net;sco_relay_sockets" ] &&
   [ -z "$(fact net_links)" ] && [ -z "$(fact net_interface_links)" ] &&
   [ -z "$(fact sockets_links)" ] && [ -z "$(fact sockets_interface_links)" ]; then
    ok "CMake declares that the program links only libsco_relay_server, libsco_net and libsco_relay_sockets, and its own object is main.cpp.o"
else
    fail "unexpected program, sources or links: program sources '$(fact program_sources)', program links '$(fact program_links)' (passes on '$(fact program_interface_links)'), server library links '$(fact server_links)' (passes on '$(fact server_interface_links)'), network library links '$(fact net_links)' (passes on '$(fact net_interface_links)'), socket library links '$(fact sockets_links)' (passes on '$(fact sockets_interface_links)')"
fi

# The program's real link line, where the generator writes one (Makefile generators: link.txt). CMake's own words
# for a library end in .a or .so, or start with -l; the objects end in .o. CMake quotes some words of the line.
link_file=$(fact link_file)
if [ -n "$link_file" ] && [ -f "$link_file" ]; then
    words=$(tr -d '"' < "$link_file" | tr ' ' '\n')
    libraries=$(echo "$words" | grep -E '\.(a|so)$|^-l' | sed 's|.*/||' | sort -u | tr '\n' ' ')
    objects=$(echo "$words" | grep -E '\.o$' | tr '\n' ' ')
    if [ "$libraries" = "libsco_net.a libsco_relay_server.a libsco_relay_sockets.a " ]; then
        ok "the program's link line names only libsco_relay_server, libsco_net and libsco_relay_sockets"
    else
        fail "unexpected libraries on the link line: $libraries"
    fi
    if [ "$objects" = "CMakeFiles/sco-relay.dir/main.cpp.o " ]; then
        ok "the program's own object on the link line is main.cpp.o and nothing else"
    else
        fail "unexpected objects on the link line: '$objects'. The whole link line is: $(cat "$link_file")"
    fi
else
    echo "note: this generator writes no link line file (${link_file:-none named}); the link line was checked from CMake's declared libraries only"
fi

members() {   # <archive>: the objects inside it, sorted, on one line
    if [ ! -f "$1" ]; then
        echo "missing"
    else
        ar t "$1" | sort | tr '\n' ' '
    fi
}

check_members() {   # <archive> <expected objects>
    got=$(members "$1")
    if [ "$got" = "$2 " ]; then
        ok "$(basename "$1") holds only: $2"
    else
        fail "$(basename "$1") holds: $got (expected: $2)"
    fi
}

server_objects=$(for source in "$root"/server/*.cpp; do
    name=$(basename "$source")
    [ "$name" = main.cpp ] || echo "$name.o"
done | sort | tr '\n' ' ')
check_members "$(fact server_library)" "${server_objects% }"
check_members "$(fact net_library)" "core.cpp.o reliable.cpp.o sha2.c.o wire.cpp.o"
check_members "$(fact sockets_library)" "scope.cpp.o udp_posix.cpp.o"

program=$(fact program)
if [ -x "$program" ] && command -v ldd > /dev/null 2>&1; then
    extra=$(ldd "$program" | awk '{print $1}' | grep -Ev '^(linux-vdso\.so\.1|libstdc\+\+\.so\.6|libm\.so\.6|libgcc_s\.so\.1|libc\.so\.6|/lib64/ld-linux-x86-64\.so\.2)$' | tr '\n' ' ')
    if [ -z "$extra" ]; then
        ok "the program needs only the C++ runtime and libc"
    else
        fail "the program needs other libraries: $extra"
    fi
fi

# ---- from the sources -------------------------------------------------------------------------------

is_standard_or_posix() {   # <header name>
    case "$1" in
        algorithm|array|atomic|cerrno|chrono|cmath|csignal|cstdarg|cstddef|cstdint|cstdio|cstdlib|cstring|ctime) return 0 ;;
        functional|initializer_list|iterator|limits|map|memory|optional|set|string|string_view|type_traits|utility|vector) return 0 ;;
        arpa/inet.h|fcntl.h|netinet/in.h|poll.h|signal.h|sys/socket.h|sys/types.h|time.h|unistd.h) return 0 ;;
    esac
    return 1
}

# The production set: the server's sources and the one CMake module that fetches the kit.
production="server/*.cpp server/*.h cmake/ScoCoreSource.cmake"

# Only the boundary file names the network library: not an include, not a type, not a comment.
leaks=$(cd "$root" && grep -lE 'sco/net/|sc_net\.h|sco_net\.h|sco::net::' $production | grep -vxF 'server/net_boundary.cpp')
if [ -z "$leaks" ]; then
    ok "only server/net_boundary.cpp includes a sco-core header or names sco::net::"
else
    fail "these files name sco-core's network library, which only server/net_boundary.cpp may: $(echo $leaks). Move the use into server/net_boundary.cpp (see server/README.md, '## The sco-core boundary')"
fi

# Every include, file by file, so the message can name the file. The library's own headers are allowed in the
# boundary file alone. Outside server/ only the relay wire header, shared with the plugin, is allowed. A name with
# ".." in it is refused first: "server/../plugins/multiplayer/motion.h" starts like one of ours and leaves server/.
foreign=$(for file in "$root"/server/*.h "$root"/server/*.cpp; do
    grep -hE '^[[:space:]]*#[[:space:]]*include' "$file" |
    sed -E 's/^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"]([^>"]*)[>"].*/\1/' |
    sort -u |
    while read -r name; do
        case "$name" in
            *..*) echo "$(basename "$file"): $name"; continue ;;
        esac
        case "$name" in
            sco/net/*|sc_net.h) [ "$(basename "$file")" = net_boundary.cpp ] && continue ;;
            server/*) [ -f "$root/$name" ] && continue ;;
            plugins/multiplayer/relay_wire.h) [ -f "$root/$name" ] && continue ;;
        esac
        is_standard_or_posix "$name" && continue
        echo "$(basename "$file"): $name"
    done
done)
if [ -z "$foreign" ]; then
    ok "every #include in server/ is our own, plugins/multiplayer/relay_wire.h, standard or POSIX (and the library's, in net_boundary.cpp only)"
else
    fail "server/ includes headers it may not: $(echo $foreign)"
fi

if (cd "$root" && grep -Eq 'external/sc-offline|multiplayer_wire' $production); then
    fail "the server's sources or its kit module refer to the plugin's own wire header (multiplayer_wire) or to an sc-offline checkout"
else
    ok "the server's sources and its kit module do not refer to the plugin's own wire header or to an sc-offline checkout"
fi

if (cd "$root" && grep -Eq '^set\(SCO_GAME_SC OFF CACHE BOOL' cmake/ScoCoreSource.cmake); then
    ok "cmake/ScoCoreSource.cmake sets SCO_GAME_SC to OFF"
else
    fail "cmake/ScoCoreSource.cmake does not set SCO_GAME_SC to OFF"
fi

if [ "$fails" -ne 0 ]; then
    echo "check-no-game-code: $fails check(s) failed" >&2
    exit 1
fi
echo "check-no-game-code: all checks passed"
