#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# What this is: proves, without a compiler, that sco-core's network library still is what the server's
# boundary (server/net_boundary.cpp) was written against. The server is built on sco-core's internal
# class Core, which its author never promised to keep stable. This script reads the library's
# files in the extracted sco-core source kit (the core-source archive that cmake/ScoCoreSource.cmake downloads,
# checks and unpacks) and checks three things, in this order:
#   1. every declaration the boundary uses is still there, word for word (blanks count as one);
#   2. every constant and every line of behaviour the boundary relies on and no compiler can see is
#      still there: what Core relays and to whom, who checks the seat limit, the private ranges, which
#      interface a socket binds;
#   3. each file the boundary reads has the same content as when it was checked (git blob id, line
#      endings ignored), so a change anywhere else in those files is noticed too.
# The first two name the exact thing that moved. The third names the file. Any failure says to read
# server/README.md, '## The sco-core boundary', and exits 1. Exit 0 means all three held.
# The compile-time checks in server/net_boundary.cpp are the same promise, made again at build time.
#
# When you move the kit on purpose (a new SCO_SDK_VERSION in cmake/ScoSdk.cmake and the matching
# SCO_CORE_SOURCE_SHA256 in cmake/ScoCoreSource.cmake): run this script on the new kit, read what moved,
# change server/net_boundary.cpp if the change needs it, then update the lines and blob ids below in the same
# commit that moves the pin.
#
# Usage: sh server/tools/check-net-boundary.sh <kit-dir>   the folder CMake extracted the kit to
#        sh server/tools/check-net-boundary.sh --list-files  prints the files the checks read, one per line
# CMake runs it through server/tests/test_net_boundary_checks.sh, which also runs it on altered copies to prove
# it fails when it should.
set -u

root=$(cd "$(dirname "$0")/../.." && pwd)
mode=check
core=""
if [ "${1:-}" = "--list-files" ]; then
    mode=list
elif [ -n "${1:-}" ]; then
    core=$1
fi
if [ "$mode" = check ] && [ -z "$core" ]; then
    echo "usage: sh server/tools/check-net-boundary.sh <extracted sco-core source kit> | --list-files" >&2
    exit 2
fi

fails=0
pins=0
files=0
note="Read server/README.md, '## The sco-core boundary', then change server/net_boundary.cpp and this script together."

fail() { echo "FAIL: $*" >&2; fails=$((fails + 1)); }

# A file without carriage returns and with each run of blanks made one blank.
normalised() { tr -d '\r' < "$core/$1" | sed -E 's/[[:space:]]+/ /g; s/^ //; s/ $//'; }

# true when a line of the file holds the text; a text that starts with ^ must start the line.
has() {   # <file> <text>
    normalised "$1" | awk -v t="$2" '
        BEGIN { anchored = (substr(t, 1, 1) == "^"); if (anchored) t = substr(t, 2) }
        { at = index($0, t); if (anchored ? at == 1 : at > 0) found = 1 }
        END { exit found ? 0 : 1 }'
}

pin() {   # <kind> <file> <what the server uses it for> <the text, blanks as one>
    [ "$mode" = check ] || return 0
    pins=$((pins + 1))
    if [ ! -f "$core/$2" ]; then
        fail "sco-core's $2 is gone. The boundary relies on it for: $3"
        return 0
    fi
    has "$2" "$4" && return 0
    fail "sco-core changed $1 the boundary relies on: $3
      file:     $2
      expected: $4"
}
declaration() { pin "a declaration" "$@"; }
constant() { pin "a constant" "$@"; }
behaviour() { pin "a line of behaviour" "$@"; }

file_pin() {   # <file> <git blob id of the file when it was checked>
    if [ "$mode" = list ]; then
        echo "$1"
        return 0
    fi
    files=$((files + 1))
    if [ ! -f "$core/$1" ]; then
        fail "sco-core's $1 is gone. The boundary reads it."
        return 0
    fi
    now=$(tr -d '\r' < "$core/$1" | git hash-object --stdin)
    [ "$now" = "$2" ] && return 0
    fail "a file of sco-core that the boundary relies on has changed: $1
      its blob id is now ${now:-unknown (is git installed?)}, the boundary was checked against $2."
}

if [ "$mode" = check ] && [ ! -f "$core/include/sco/net/core.h" ]; then
    echo "check-net-boundary: no sco-core source kit in $core. Configure the build (cmake -S . -B build): it downloads and unpacks the kit." >&2
    exit 2
fi

# The seats a session has, as the build expects them (server/net_boundary.h): the one number a
# 50-player test build changes, and then only on a private copy of the library.
seats=$(sed -n 's/^#define SCO_RELAY_EXPECTED_SESSION_LIMIT \([0-9][0-9]*\)$/\1/p' "$root/server/net_boundary.h")
if [ "$mode" = check ] && [ -z "$seats" ]; then
    echo "check-net-boundary: cannot read SCO_RELAY_EXPECTED_SESSION_LIMIT from server/net_boundary.h" >&2
    exit 2
fi

# ---- 1. declarations the boundary uses ----------------------------------------------------------------

core_h=include/sco/net/core.h
declaration $core_h 'PeerId, a 64-bit number' 'using PeerId = uint64_t;'
declaration $core_h 'kHostPeer, the host is peer 1' 'inline constexpr PeerId kHostPeer = 1;'
declaration $core_h 'State, the states of a session' 'enum class State : uint8_t { Idle, Hosting, Joining, Joined };'
declaration $core_h 'PeerEvent, a player joins or leaves' 'enum class PeerEvent : uint8_t { Joined, Left };'
for result in Ok NoSession UnknownChannel TooLarge WrongDirection NotRemote QueueFull; do
    declaration $core_h "SendResult::$result, a way a send ends" "^$result,"
done
declaration $core_h 'Message::channel' '^std::string_view channel;'
declaration $core_h 'Message::from, the sending player' '^PeerId from;'
declaration $core_h 'Message::data' '^const uint8_t* data;'
declaration $core_h 'Message::len' '^uint32_t len;'
declaration $core_h 'Callbacks::message' 'std::function<void(const Message&)> message;'
declaration $core_h 'Callbacks::peer' 'std::function<void(PeerEvent, PeerId, std::string_view text)> peer;'
declaration $core_h 'Callbacks::state' 'std::function<void(bool active, std::string_view reason)> state;'
declaration $core_h 'Options::passphrase' '^std::string passphrase;'
declaration $core_h 'Options::playerName, the server name' '^std::string playerName;'
declaration $core_h 'Options::maxPeers, the seats of the session' '^uint32_t maxPeers'
declaration $core_h 'Options::pbkdf2Iters, the protocol default' '^uint32_t pbkdf2Iters = kPbkdf2Iters;'
for timer in joinTimeoutMs resendMs maxResendMs keepaliveMs peerTimeoutMs; do
    declaration $core_h "Options::$timer, a timer with the library's default" "^uint32_t $timer"
done
for counter in malformed badVersion badTag replayed spoofed unknownChannel refused handshakeDropped joinsRefused retransmits; do
    declaration $core_h "Stats::$counter" "^uint64_t $counter = 0;"
done
declaration $core_h 'Stats::datagramsIn and datagramsOut' '^uint64_t datagramsIn = 0, datagramsOut = 0;'
declaration $core_h 'Core, built on a Transport' 'explicit Core(Transport& transport, Callbacks callbacks = Callbacks());'
declaration $core_h 'Core::RegisterChannel, adding a channel' 'bool RegisterChannel(std::string_view fqn, uint32_t flags, uint32_t maxLen);'
declaration $core_h 'Core::Host, hosting a session' 'bool Host(const Options& opts, uint64_t nowMs);'
declaration $core_h 'Core::Leave, ending a session' 'void Leave(std::string_view reason);'
declaration $core_h 'Core::Send, sending a message' 'SendResult Send(std::string_view fqn, const void* data, uint32_t len);'
declaration $core_h 'Core::Pump, running the timers' 'void Pump(uint64_t nowMs);'
declaration $core_h 'Core::GetState' 'State GetState() const;'
declaration $core_h 'Core::GetStats' 'const Stats& GetStats() const;'

transport_h=include/sco/net/transport.h
declaration $transport_h 'Endpoint, the address families' 'static constexpr uint8_t kNone = 0, kIpv4 = 4, kIpv6 = 6, kMemory = 0xFF;'
declaration $transport_h 'Endpoint::family' '^uint8_t family = kNone;'
declaration $transport_h 'Endpoint::port' '^uint16_t port = 0;'
declaration $transport_h 'Endpoint::addr, an IPv4 address in the first 4 bytes' '^uint8_t addr[16] = {};'
declaration $transport_h 'Transport::Send' 'virtual bool Send(const Endpoint& to, const uint8_t* data, size_t len) = 0;'
declaration $transport_h 'Transport::Receive' 'virtual bool Receive(Endpoint* from, uint8_t* buf, size_t cap, size_t* len) = 0;'

scope_h=include/sco/net/scope.h
declaration $scope_h 'Cidr::family' '^uint8_t family = Endpoint::kNone;'
declaration $scope_h 'Cidr::bits' '^uint8_t bits = 0;'
declaration $scope_h 'Cidr::addr' '^uint8_t addr[16] = {};'
declaration $scope_h 'Scope::allow, the extra ranges' '^std::vector<Cidr> allow;'
declaration $scope_h 'Scope::any, off unless set: the address rule never lets every address in' '^bool any = false;'
declaration $scope_h 'Scope::allowOnly, off by default: private ranges stay inside the rule' '^bool allowOnly = false;'
declaration $scope_h 'Scope::maxDatagramsPerSecond, 0 means no limit on what a player may send' '^uint32_t maxDatagramsPerSecond = 0;'
declaration $scope_h 'ParseCidr' 'bool ParseCidr(std::string_view text, Cidr* out);'
declaration $scope_h 'IsLan, the private ranges' 'bool IsLan(const Endpoint& e);'
declaration $scope_h 'InCidr' 'bool InCidr(const Cidr& c, const Endpoint& e);'
declaration $scope_h 'InScope' 'bool InScope(const Scope& s, const Endpoint& e);'
declaration $scope_h 'ScopedTransport, the address rule as a Transport' 'ScopedTransport(Transport& inner, Scope scope)'
declaration $scope_h 'ScopedTransport::Dropped' 'uint64_t Dropped() const'

udp_h=include/sco/net/udp.h
declaration $udp_h 'udp::Socket, a Transport' 'class Socket final : public Transport {'
declaration $udp_h 'udp::Socket::Open, binding the port' 'static std::unique_ptr<Socket> Open(uint16_t port, bool loopbackOnly, std::string* why);'
declaration $udp_h 'udp::Socket::Wait' 'bool Wait(uint32_t ms);'
declaration $udp_h 'udp::Socket::Port' 'uint16_t Port() const'

wire_h=include/sco/net/wire.h
declaration $wire_h 'ValidName, a legal player name' 'bool ValidName(std::string_view name);'
declaration $wire_h 'kMaxPeers, the seats of a session' 'inline constexpr uint32_t kMaxPeers = SC_NET_MAX_PEERS;'
declaration $wire_h 'kMaxName' 'inline constexpr uint32_t kMaxName = SC_NET_MAX_NAME;'
declaration $wire_h 'kMaxUnreliable' 'inline constexpr uint32_t kMaxUnreliable = SC_NET_MAX_UNREL;'
declaration $wire_h 'kMaxReliable' 'inline constexpr uint32_t kMaxReliable = SC_NET_MAX_RELIABLE;'
declaration $wire_h 'kMaxChannels, the channel table' 'inline constexpr uint32_t kMaxChannels = SC_NET_MAX_CHANNELS;'
declaration $wire_h 'kPbkdf2Iters, the protocol default' 'inline constexpr uint32_t kPbkdf2Iters = SC_NET_PBKDF2_ITERS;'
declaration $wire_h 'kReliable' 'inline constexpr uint32_t kReliable = SC_NET_RELIABLE;'
declaration $wire_h 'kFromHost' 'inline constexpr uint32_t kFromHost = SC_NET_FROM_HOST;'
declaration $wire_h 'kToHost' 'inline constexpr uint32_t kToHost = SC_NET_TO_HOST;'

# ---- 2. constants and behaviour ----------------------------------------------------------------------

sc_net_h=include/sc_net.h
constant $sc_net_h "SC_NET_MAX_PEERS, the seats of a session (this build expects ${seats:-?}; see SCO_RELAY_EXPECTED_SESSION_LIMIT in server/net_boundary.h)" "#define SC_NET_MAX_PEERS ${seats:-?}u"
constant $sc_net_h 'SC_NET_MAX_UNREL, the longest unreliable message' '#define SC_NET_MAX_UNREL 1200u'
constant $sc_net_h 'SC_NET_MAX_RELIABLE, the longest reliable message' '#define SC_NET_MAX_RELIABLE (256u * 1024u)'
constant $sc_net_h 'SC_NET_MAX_NAME, the longest player name' '#define SC_NET_MAX_NAME 64u'
constant $sc_net_h 'SC_NET_MAX_FQN, the longest channel name' '#define SC_NET_MAX_FQN 64u'
behaviour $sc_net_h 'a player name is 1 to 64 bytes (sc_net_valid_name, behind ValidName)' 'if (!s || n == 0 || n > SC_NET_MAX_NAME) return 0;'
behaviour $sc_net_h 'a player name holds no control character (sc_net_valid_name, behind ValidName)' 'if (u < 0x20u || u == 0x7Fu) return 0;'
behaviour $sc_net_h 'a channel name is 3 to 64 bytes (sc_net_valid_fqn, behind ValidFqn)' 'if (!s || n < 3 || n > SC_NET_MAX_FQN) return 0;'
constant $sc_net_h 'SC_NET_MAX_CHANNELS, names in the channel table' '#define SC_NET_MAX_CHANNELS 256u'
constant $sc_net_h 'SC_NET_PBKDF2_ITERS, the password stretching a player uses too' '#define SC_NET_PBKDF2_ITERS 200000u'
constant $sc_net_h 'SC_NET_RELIABLE' '#define SC_NET_RELIABLE 0x1u'
constant $sc_net_h 'SC_NET_FROM_HOST' '#define SC_NET_FROM_HOST 0x2u'
constant $sc_net_h 'SC_NET_TO_HOST' '#define SC_NET_TO_HOST 0x4u'

core_cpp=src/net/core.cpp
behaviour $core_cpp 'Core relays every channel that is not TO_HOST, but only when it hosts' 'if (state == State::Hosting && !toHost) {'
behaviour $core_cpp 'Core relays to every other player that registered the channel, and to no one else' 'if (b.id == linkId || !b.registered.count(ch)) continue;'
behaviour $core_cpp 'a message on a channel the server registered is handed to Callbacks::message' 'cb.message(Message{ fqn, origin, data, len });'
behaviour $core_cpp 'Core::Send from the host queues the message for each player that registered the channel' 'if (!kv.second->registered.count(ch)) continue;'
behaviour $core_cpp 'Core::Send says NotRemote when no player registered the channel' 'return SendResult::NotRemote;'
behaviour $core_cpp 'Core::Send says QueueFull when any one registered player has a full queue, even if the others got it' 'return full ? SendResult::QueueFull : SendResult::Ok;'
behaviour $core_cpp 'a player can only speak for itself: the host checks the sender of every datagram, so Message::from is true' 'if (state == State::Hosting && origin != l.id) {'
behaviour $core_cpp 'TO_HOST is a flag the sender sets on its datagram, not a mode of the channel at the host' 'const bool toHost = (flags & kDataToHost) != 0;'
behaviour $core_cpp 'a player that registered a FROM_HOST channel drops data on it whose origin is not the host, which stops a player forging multiplayer.relay or multiplayer.near.<slot>' 'if ((c.flags & kFromHost) && origin != kHostPeer) {'
behaviour $core_cpp 'only the host checks the seat limit, in the options it is given' 'if (host && (o.maxPeers < 2 || o.maxPeers > kMaxPeers)) return false;'
behaviour $core_cpp 'a join is refused when the session is full' 'if (others + 1 >= opts.maxPeers) {'
behaviour $core_cpp 'the refusal text players read when the session is full' 'SendRefuse(from, pd.cn, Refusal::Full, "session full");'
behaviour $core_cpp 'the channel table only grows, and holds kMaxChannels names' 'if (table.size() >= kMaxChannels) return -1;'
behaviour $core_cpp 'peer numbers are handed out in order and never reused in a session' 'l.id = nextPeer++;'
behaviour $core_cpp 'peer numbers start again only with a new session' 'nextPeer = kHostPeer + 1;'
behaviour $core_cpp 'a player who joins again from the same address is told to have rejoined' 'if (old) RemoveLink(old->id, "rejoined");'
behaviour $core_cpp 'a player who left on purpose is told with its reason' 'RemoveLink(l.id, "left: " + text);'
behaviour $core_cpp 'a silent player is dropped as timed out' 'RemoveLink(id, "timed out");'

scope_cpp=src/net/scope.cpp
wire_cpp=src/net/wire.cpp
behaviour $wire_cpp 'ValidName, a legal player name (server_name), asks sc_net_valid_name' 'bool ValidName(std::string_view name) { return sc_net_valid_name(name.data(), name.size()) != 0; }'
behaviour $wire_cpp 'ValidFqn, a legal channel name (every channel the relay adds), asks sc_net_valid_fqn' 'bool ValidFqn(std::string_view fqn) { return sc_net_valid_fqn(fqn.data(), fqn.size()) != 0; }'

behaviour $scope_cpp 'the private ranges, first four' 'return In4(e, 127, 0, 8) || In4(e, 10, 0, 8) || In4(e, 172, 16, 12) || In4(e, 192, 168, 16) ||'
behaviour $scope_cpp 'the private ranges, link-local' 'In4(e, 169, 254, 16);'
behaviour $scope_cpp 'ScopedTransport drops a datagram from outside the rule' 'if (InScope(scope_, *from)) return true;'
behaviour $scope_cpp 'ScopedTransport counts what it drops' '++dropped_;'

udp_cpp=src/net/udp_posix.cpp
behaviour $udp_cpp 'a socket opened without loopbackOnly binds every IPv4 interface' 'if (loopbackOnly) {'
behaviour $udp_cpp 'both Open calls end in the same bind' 'return OpenBound(port, ep, why);'
behaviour $udp_cpp 'a datagram longer than the buffer is reported as one byte too long' '*len = (msg.msg_flags & MSG_TRUNC) ? cap + 1 : static_cast<size_t>(r);'

# ---- 3. every file the boundary reads, unchanged since it was checked ---------------------------------
# Blob ids of the files in sco-sdk-core-source-2.3.0.zip. They are the same ids as at sco-core 686187c (SDK 2.2.0,
# the pin of the standalone relay this server comes from): none of these files changed in the move to SDK 2.3.0.
# The last five files are not read by the checks above, but the relay compiles or includes them too (they sit
# under the library's core and its wire), so a change in them is noticed the same way. The fifth is the kit's
# tests/net_mem.h, the in-memory network that test_server includes (the program never does).

file_pin include/sc_net.h 452014ac6fbbc754c6a052cb3779c3d0df5e7b47
file_pin include/sco/net/core.h 0928dfdeb502dd4086efc74bc82ebb26a82704fb
file_pin include/sco/net/scope.h 650b1df56628c1ffeb99c9a84221e28d2164cd3a
file_pin include/sco/net/transport.h 9ad67f517234f28fb99e44c8cf367a5ccb700542
file_pin include/sco/net/udp.h 15d4bd57f5f3be2246be16d3d7ddb8da62c19675
file_pin include/sco/net/wire.h 26e1ea011d6b131238a186e4fc0b33dbb69c03a0
file_pin src/net/core.cpp 540ead2f58c2e49519e8c27b9918fabcecc1eba5
file_pin src/net/scope.cpp de7f0c50ca8178135363f4f049ba751a17de3382
file_pin src/net/wire.cpp 317f2d63a8e7981f0399155918e67b0bccd9a0b6
file_pin src/net/udp_posix.cpp 62402b44d666a6e1a5d7a79adce5c76d2fa3be22
file_pin include/sco/net/reliable.h 1ed5158157831585a24c296d09519bea5697699a
file_pin include/sco/net/sha2.h 9f68ac185abaf685ef10c29faf6d0e549ccf4cbc
file_pin src/net/reliable.cpp 831e75bfc2d56e24c9051008b770a3d245a122a2
file_pin src/net/sha2.c 018f0007dd26f3f7d3fbd144aee8920431e082cf
file_pin tests/net_mem.h 7ebff672cfd3802f87b7524c4cbb279fa5f26f68

[ "$mode" = list ] && exit 0

if [ "$fails" -ne 0 ]; then
    echo "check-net-boundary: $fails thing(s) the boundary relies on changed in $core." >&2
    echo "$note" >&2
    exit 1
fi
kit_version=$(cat "$core/sdk/VERSION" 2> /dev/null || echo unknown)
echo "check-net-boundary: ok. $pins declarations, constants and lines of behaviour found, $files files unchanged since they were checked (sco-core source kit $kit_version)."
