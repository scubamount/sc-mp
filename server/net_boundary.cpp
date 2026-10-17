// SPDX-License-Identifier: GPL-3.0-only
// The boundary with sco-core's network library (server/net_boundary.h). This is the only file of the
// server that includes the library's headers or names its types. Everything else in server/ speaks
// the types of net_boundary.h, and this file translates in both directions.
//
// What the server relies on, and how a change is caught:
//   - Names and shapes the code below uses (Core and its calls, Transport, Endpoint, Scope and
//     ScopedTransport, the UDP socket, the limits). static_asserts in this file fail to compile with
//     a plain message when one moves; server/tools/check-net-boundary.sh checks the same declarations in
//     the library's headers first, without a compiler, and names the one that moved.
//   - Behaviour no compiler can see (what Core relays and to whom, who checks the seat limit, what
//     the private ranges are, which interface a socket binds). server/tools/check-net-boundary.sh pins the
//     exact lines of the library's sources that do it, and the blob id of every file this one relies on.
// The list lives in that script. When it fails, server/README.md, '## The sco-core boundary', says what to do.
#include "server/net_boundary.h"
#include "sco/net/core.h"
#include "sco/net/scope.h"
#include "sco/net/transport.h"
#include "sco/net/udp.h"
#include "sco/net/wire.h"
#include "plugins/multiplayer/relay_wire.h"
#include <cstring>
#include <memory>
#include <type_traits>
#include <utility>

namespace lib = sco::net;

#define BOUNDARY_CHECK(condition, what) \
    static_assert(condition, what " Read server/README.md, '## The sco-core boundary', then change server/net_boundary.cpp and server/tools/check-net-boundary.sh together.")

namespace sco::relay {

// ---- what the library must still be: checked every time this file is compiled ----------------------

BOUNDARY_CHECK(lib::kMaxPeers == kSessionSeats,
               "sco-core's session limit (kMaxPeers) is not the one this build expects (SCO_RELAY_EXPECTED_SESSION_LIMIT, 16 unless the build says otherwise).");
BOUNDARY_CHECK(lib::kHostPeer == kServerPeer, "sco-core no longer numbers the host peer 1.");
BOUNDARY_CHECK(lib::kMaxName == kPlayerNameMaxBytes, "sco-core's longest player name (kMaxName) changed.");
BOUNDARY_CHECK(kSessionSeats - 1 <= wire::kRelayMaxPlayers,
               "multiplayer.relay cannot list as many players as the session holds.");
BOUNDARY_CHECK(lib::kMaxUnreliable >= wire::kNearUpMaxBytes,
               "sco-core's unreliable message limit (kMaxUnreliable) is below the longest nearby message.");
BOUNDARY_CHECK(lib::kMaxReliable >= wire::kRelayMaxBytes,
               "sco-core's reliable message limit (kMaxReliable) is below the longest multiplayer.relay.");
BOUNDARY_CHECK(1 + wire::kRelayFixedChannelNames + (kSessionSeats - 1) + wire::kPlayerChannelNamesReserved <=
                   lib::kMaxChannels,
               "the relay's channel names plus the players' no longer fit sco-core's channel table (kMaxChannels).");

BOUNDARY_CHECK(lib::Endpoint::kNone == Address::kNone && lib::Endpoint::kIpv4 == Address::kIpv4 &&
                   lib::Endpoint::kIpv6 == Address::kIpv6 && lib::Endpoint::kMemory == Address::kMemory,
               "the address families of sco-core's Endpoint changed.");
BOUNDARY_CHECK((std::is_same_v<decltype(lib::Endpoint::family), uint8_t> &&
                std::is_same_v<decltype(lib::Endpoint::port), uint16_t> &&
                std::is_same_v<decltype(lib::Endpoint::addr), uint8_t[16]>),
               "the fields of sco-core's Endpoint changed.");
BOUNDARY_CHECK((std::is_same_v<decltype(lib::Cidr::family), uint8_t> && std::is_same_v<decltype(lib::Cidr::bits), uint8_t> &&
                std::is_same_v<decltype(lib::Cidr::addr), uint8_t[16]>),
               "the fields of sco-core's Cidr changed.");
BOUNDARY_CHECK((std::is_same_v<decltype(lib::Scope::allow), std::vector<lib::Cidr>>),
               "sco-core's Scope no longer holds its extra ranges as a vector of Cidr.");

BOUNDARY_CHECK((std::is_same_v<decltype(&lib::Transport::Send), bool (lib::Transport::*)(const lib::Endpoint&, const uint8_t*, size_t)>),
               "Transport::Send changed its parameters.");
BOUNDARY_CHECK((std::is_same_v<decltype(&lib::Transport::Receive), bool (lib::Transport::*)(lib::Endpoint*, uint8_t*, size_t, size_t*)>),
               "Transport::Receive changed its parameters.");
BOUNDARY_CHECK((std::is_constructible_v<lib::ScopedTransport, lib::Transport&, lib::Scope>),
               "ScopedTransport is no longer built from a Transport and a Scope.");
BOUNDARY_CHECK((std::is_same_v<decltype(&lib::ScopedTransport::Dropped), uint64_t (lib::ScopedTransport::*)() const>),
               "ScopedTransport::Dropped changed.");
BOUNDARY_CHECK((std::is_same_v<decltype(&lib::ParseCidr), bool (*)(std::string_view, lib::Cidr*)>), "ParseCidr changed.");
BOUNDARY_CHECK((std::is_same_v<decltype(&lib::IsLan), bool (*)(const lib::Endpoint&)>), "IsLan changed.");
BOUNDARY_CHECK((std::is_same_v<decltype(&lib::InCidr), bool (*)(const lib::Cidr&, const lib::Endpoint&)>), "InCidr changed.");
BOUNDARY_CHECK((std::is_same_v<decltype(&lib::InScope), bool (*)(const lib::Scope&, const lib::Endpoint&)>), "InScope changed.");
BOUNDARY_CHECK((std::is_same_v<decltype(&lib::ValidName), bool (*)(std::string_view)>), "ValidName changed.");

BOUNDARY_CHECK((std::is_base_of_v<lib::Transport, lib::udp::Socket>), "the UDP Socket is no longer a Transport.");
BOUNDARY_CHECK((std::is_same_v<decltype(&lib::udp::Socket::Wait), bool (lib::udp::Socket::*)(uint32_t)>),
               "udp::Socket::Wait changed.");
BOUNDARY_CHECK((std::is_same_v<decltype(&lib::udp::Socket::Port), uint16_t (lib::udp::Socket::*)() const>),
               "udp::Socket::Port changed.");
// Open is overloaded, so its address cannot be named; ask for the one overload this file calls.
template <class Socket>
concept OpensOnInterfaces = requires {
    static_cast<std::unique_ptr<Socket> (*)(uint16_t, bool, std::string*)>(&Socket::Open);
};
BOUNDARY_CHECK(OpensOnInterfaces<lib::udp::Socket>, "udp::Socket::Open(port, loopbackOnly, why) changed.");

BOUNDARY_CHECK((std::is_same_v<decltype(&lib::Core::RegisterChannel), bool (lib::Core::*)(std::string_view, uint32_t, uint32_t)>),
               "Core::RegisterChannel changed its parameters.");
BOUNDARY_CHECK((std::is_same_v<decltype(&lib::Core::Host), bool (lib::Core::*)(const lib::Options&, uint64_t)>),
               "Core::Host changed its parameters.");
BOUNDARY_CHECK((std::is_same_v<decltype(&lib::Core::Leave), void (lib::Core::*)(std::string_view)>),
               "Core::Leave changed its parameters.");
BOUNDARY_CHECK((std::is_same_v<decltype(&lib::Core::Pump), void (lib::Core::*)(uint64_t)>), "Core::Pump changed its parameters.");
BOUNDARY_CHECK((std::is_same_v<decltype(&lib::Core::Send), lib::SendResult (lib::Core::*)(std::string_view, const void*, uint32_t)>),
               "Core::Send changed its parameters.");
BOUNDARY_CHECK((std::is_same_v<decltype(&lib::Core::GetState), lib::State (lib::Core::*)() const>), "Core::GetState changed.");
BOUNDARY_CHECK((std::is_same_v<decltype(&lib::Core::GetStats), const lib::Stats& (lib::Core::*)() const>),
               "Core::GetStats changed.");

BOUNDARY_CHECK((std::is_same_v<lib::PeerId, PeerId>), "sco-core's PeerId is no longer a 64-bit number.");
BOUNDARY_CHECK((std::is_same_v<decltype(lib::Callbacks::message), std::function<void(const lib::Message&)>>),
               "Callbacks::message changed.");
BOUNDARY_CHECK((std::is_same_v<decltype(lib::Callbacks::peer), std::function<void(lib::PeerEvent, lib::PeerId, std::string_view)>>),
               "Callbacks::peer changed.");
BOUNDARY_CHECK((std::is_same_v<decltype(lib::Callbacks::state), std::function<void(bool, std::string_view)>>),
               "Callbacks::state changed.");
BOUNDARY_CHECK((std::is_same_v<decltype(lib::Message::channel), std::string_view> &&
                std::is_same_v<decltype(lib::Message::from), lib::PeerId> &&
                std::is_same_v<decltype(lib::Message::data), const uint8_t*> &&
                std::is_same_v<decltype(lib::Message::len), uint32_t>),
               "the fields of sco-core's Message changed.");
BOUNDARY_CHECK((std::is_same_v<decltype(lib::Options::passphrase), std::string> &&
                std::is_same_v<decltype(lib::Options::playerName), std::string> &&
                std::is_same_v<decltype(lib::Options::maxPeers), uint32_t> &&
                std::is_same_v<decltype(lib::Options::pbkdf2Iters), uint32_t> &&
                std::is_same_v<decltype(lib::Options::joinTimeoutMs), uint32_t> &&
                std::is_same_v<decltype(lib::Options::resendMs), uint32_t> &&
                std::is_same_v<decltype(lib::Options::maxResendMs), uint32_t> &&
                std::is_same_v<decltype(lib::Options::keepaliveMs), uint32_t> &&
                std::is_same_v<decltype(lib::Options::peerTimeoutMs), uint32_t>),
               "the fields of sco-core's Options changed.");
BOUNDARY_CHECK((std::is_same_v<decltype(lib::Stats::datagramsIn), uint64_t> && std::is_same_v<decltype(lib::Stats::datagramsOut), uint64_t> &&
                std::is_same_v<decltype(lib::Stats::malformed), uint64_t> && std::is_same_v<decltype(lib::Stats::badVersion), uint64_t> &&
                std::is_same_v<decltype(lib::Stats::badTag), uint64_t> && std::is_same_v<decltype(lib::Stats::replayed), uint64_t> &&
                std::is_same_v<decltype(lib::Stats::spoofed), uint64_t> && std::is_same_v<decltype(lib::Stats::unknownChannel), uint64_t> &&
                std::is_same_v<decltype(lib::Stats::refused), uint64_t> && std::is_same_v<decltype(lib::Stats::handshakeDropped), uint64_t> &&
                std::is_same_v<decltype(lib::Stats::joinsRefused), uint64_t> && std::is_same_v<decltype(lib::Stats::retransmits), uint64_t>),
               "the counters of sco-core's Stats changed.");

namespace {

// ---- addresses, in both directions -----------------------------------------------------------------

lib::Endpoint ToEndpoint(const Address& address) {
    lib::Endpoint endpoint;
    endpoint.family = address.family;
    endpoint.port = address.port;
    std::memcpy(endpoint.addr, address.bytes, sizeof endpoint.addr);
    return endpoint;
}

Address ToAddress(const lib::Endpoint& endpoint) {
    Address address;
    address.family = endpoint.family;
    address.port = endpoint.port;
    std::memcpy(address.bytes, endpoint.addr, sizeof address.bytes);
    return address;
}

lib::Cidr ToCidr(const AddressRange& range) {
    lib::Cidr cidr;
    cidr.family = range.family;
    cidr.bits = range.bits;
    std::memcpy(cidr.addr, range.bytes, sizeof cidr.addr);
    return cidr;
}

AddressRange ToRange(const lib::Cidr& cidr) {
    AddressRange range;
    range.family = cidr.family;
    range.bits = cidr.bits;
    std::memcpy(range.bytes, cidr.addr, sizeof range.bytes);
    return range;
}

lib::Scope MakeScope(const std::vector<AddressRange>& extraRanges) {
    lib::Scope scope;
    for (const AddressRange& range : extraRanges) scope.allow.push_back(ToCidr(range));
    return scope;
}

// ---- our pipe seen from the library ------------------------------------------------------------------

class LibraryTransport final : public lib::Transport {
public:
    explicit LibraryTransport(DatagramTransport& inner) : inner_(inner) {}

    bool Send(const lib::Endpoint& to, const uint8_t* data, size_t len) override {
        return inner_.Send(ToAddress(to), data, len);
    }
    bool Receive(lib::Endpoint* from, uint8_t* buf, size_t cap, size_t* len) override {
        Address source;
        if (!inner_.Receive(&source, buf, cap, len)) return false;
        *from = ToEndpoint(source);
        return true;
    }

private:
    DatagramTransport& inner_;
};

// Remembers where the last datagram the address rule rejects came from, so the log can tell the
// operator which address to allow. It only notes; it never drops anything. The library's own
// ScopedTransport, right above it, drops and counts. Both use the library's InScope on the same
// Scope, so they always agree.
class WatchingTransport final : public lib::Transport {
public:
    WatchingTransport(lib::Transport& inner, lib::Scope scope) : inner_(inner), scope_(std::move(scope)) {}

    bool Send(const lib::Endpoint& to, const uint8_t* data, size_t len) override { return inner_.Send(to, data, len); }
    bool Receive(lib::Endpoint* from, uint8_t* buf, size_t cap, size_t* len) override {
        if (!inner_.Receive(from, buf, cap, len)) return false;
        if (!lib::InScope(scope_, *from)) {
            lastRejected_ = *from;
            anyRejected_ = true;
        }
        return true;
    }

    bool LastRejected(Address* out) const {
        if (!anyRejected_) return false;
        *out = ToAddress(lastRejected_);
        return true;
    }

private:
    lib::Transport& inner_;
    lib::Scope      scope_;
    lib::Endpoint   lastRejected_;
    bool            anyRejected_ = false;
};

SendOutcome ToOutcome(lib::SendResult result) {
    switch (result) {
    case lib::SendResult::Ok:             return SendOutcome::Sent;
    case lib::SendResult::NoSession:      return SendOutcome::NoSession;
    case lib::SendResult::UnknownChannel: return SendOutcome::UnknownChannel;
    case lib::SendResult::TooLarge:       return SendOutcome::TooLarge;
    case lib::SendResult::WrongDirection: return SendOutcome::WrongDirection;
    case lib::SendResult::NotRemote:      return SendOutcome::NobodyListening;
    case lib::SendResult::QueueFull:      return SendOutcome::QueueFull;
    }
    return SendOutcome::NoSession;
}

uint32_t FlagsOf(ChannelMode mode) {
    switch (mode) {
    case ChannelMode::ReliableFromServer:   return lib::kReliable | lib::kFromHost;
    case ChannelMode::UnreliableFromServer: return lib::kFromHost;
    case ChannelMode::UnreliableToServer:   return lib::kToHost;
    }
    return 0;
}

}  // namespace

// ---- limits, defaults and names --------------------------------------------------------------------

LibraryDefaults GetLibraryDefaults() {
    const lib::Options library;   // the library's own defaults, so there is no second copy here
    LibraryDefaults defaults;
    defaults.pbkdf2Iters = library.pbkdf2Iters;
    defaults.joinTimeoutMs = library.joinTimeoutMs;
    defaults.resendMs = library.resendMs;
    defaults.maxResendMs = library.maxResendMs;
    defaults.keepaliveMs = library.keepaliveMs;
    defaults.peerTimeoutMs = library.peerTimeoutMs;
    return defaults;
}

bool IsValidPlayerName(std::string_view name) { return lib::ValidName(name); }

// ---- address ranges --------------------------------------------------------------------------------

bool ParseAddressRange(std::string_view text, AddressRange* out) {
    lib::Cidr cidr;
    if (!lib::ParseCidr(text, &cidr)) return false;
    *out = ToRange(cidr);
    return true;
}

bool IsPrivateAddress(const Address& address) { return lib::IsLan(ToEndpoint(address)); }

bool InAddressRange(const AddressRange& range, const Address& address) {
    return lib::InCidr(ToCidr(range), ToEndpoint(address));
}

// ---- the real socket -------------------------------------------------------------------------------

struct UdpSocket::Impl {
    std::unique_ptr<lib::udp::Socket> socket;
};

UdpSocket::UdpSocket(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

UdpSocket::~UdpSocket() = default;

std::unique_ptr<UdpSocket> UdpSocket::Open(uint16_t port, bool loopbackOnly, std::string* why) {
    std::unique_ptr<lib::udp::Socket> socket = lib::udp::Socket::Open(port, loopbackOnly, why);
    if (!socket) return nullptr;
    std::unique_ptr<Impl> impl(new Impl{ std::move(socket) });
    return std::unique_ptr<UdpSocket>(new UdpSocket(std::move(impl)));
}

bool UdpSocket::Send(const Address& to, const uint8_t* data, size_t len) {
    return impl_->socket->Send(ToEndpoint(to), data, len);
}

bool UdpSocket::Receive(Address* from, uint8_t* buf, size_t cap, size_t* len) {
    lib::Endpoint source;
    if (!impl_->socket->Receive(&source, buf, cap, len)) return false;
    *from = ToAddress(source);
    return true;
}

bool UdpSocket::Wait(uint32_t ms) { return impl_->socket->Wait(ms); }

uint16_t UdpSocket::Port() const { return impl_->socket->Port(); }

// ---- the address rule ------------------------------------------------------------------------------

struct AddressFilter::Impl {
    Impl(DatagramTransport& inner, const std::vector<AddressRange>& extraRanges)
        : library(inner), watch(library, MakeScope(extraRanges)), scoped(watch, MakeScope(extraRanges)) {}

    LibraryTransport          library;   // the pipe we were given, seen from the library
    WatchingTransport         watch;     // notes where rejected datagrams come from
    lib::ScopedTransport      scoped;    // the rule itself, enforced by sco-core's own class
};

AddressFilter::AddressFilter(DatagramTransport& inner, const std::vector<AddressRange>& extraRanges)
    : impl_(std::make_unique<Impl>(inner, extraRanges)) {}

AddressFilter::~AddressFilter() = default;

bool AddressFilter::Send(const Address& to, const uint8_t* data, size_t len) {
    return impl_->scoped.Send(ToEndpoint(to), data, len);
}

bool AddressFilter::Receive(Address* from, uint8_t* buf, size_t cap, size_t* len) {
    lib::Endpoint source;
    if (!impl_->scoped.Receive(&source, buf, cap, len)) return false;
    *from = ToAddress(source);
    return true;
}

uint64_t AddressFilter::Dropped() const { return impl_->scoped.Dropped(); }

bool AddressFilter::LastRejected(Address* out) const { return impl_->watch.LastRejected(out); }

// ---- the hosted session ----------------------------------------------------------------------------

const char* SendOutcomeName(SendOutcome outcome) {
    switch (outcome) {
    case SendOutcome::Sent:            return "sent";
    case SendOutcome::NoSession:       return "no session";
    case SendOutcome::UnknownChannel:  return "unknown channel";
    case SendOutcome::TooLarge:        return "too large";
    case SendOutcome::WrongDirection:  return "wrong direction";
    case SendOutcome::NobodyListening: return "nobody has registered the channel";
    case SendOutcome::QueueFull:       return "queue full";
    }
    return "unknown";
}

struct HostedSession::Impl {
    Impl(DatagramTransport& transport, SessionEvents sessionEvents)
        : library(transport), events(std::move(sessionEvents)), core(library, MakeCallbacks()) {}

    lib::Callbacks MakeCallbacks() {
        lib::Callbacks callbacks;
        callbacks.message = [this](const lib::Message& message) {
            if (events.message) events.message(message.channel, message.from, message.data, message.len);
        };
        callbacks.peer = [this](lib::PeerEvent event, lib::PeerId id, std::string_view text) {
            if (event == lib::PeerEvent::Joined) {
                if (events.playerJoined) events.playerJoined(id, text);
            } else if (events.playerLeft) {
                events.playerLeft(id, text);
            }
        };
        callbacks.state = [this](bool active, std::string_view reason) {
            if (events.stateChanged) events.stateChanged(active, reason);
        };
        return callbacks;
    }

    LibraryTransport library;   // declared before core: core is built on it
    SessionEvents    events;
    lib::Core        core;
};

HostedSession::HostedSession(DatagramTransport& transport, SessionEvents events)
    : impl_(std::make_unique<Impl>(transport, std::move(events))) {}

HostedSession::~HostedSession() = default;

bool HostedSession::AddChannel(std::string_view name, ChannelMode mode, uint32_t maxBytes) {
    return impl_->core.RegisterChannel(name, FlagsOf(mode), maxBytes);
}

bool HostedSession::Host(const HostRequest& request, uint64_t nowMs) {
    lib::Options options;
    options.passphrase = request.passphrase;
    options.playerName = request.serverName;
    options.maxPeers = request.seats;
    options.pbkdf2Iters = request.pbkdf2Iters;
    options.joinTimeoutMs = request.joinTimeoutMs;
    options.resendMs = request.resendMs;
    options.maxResendMs = request.maxResendMs;
    options.keepaliveMs = request.keepaliveMs;
    options.peerTimeoutMs = request.peerTimeoutMs;
    return impl_->core.Host(options, nowMs);
}

void HostedSession::Pump(uint64_t nowMs) { impl_->core.Pump(nowMs); }

void HostedSession::End(std::string_view reason) { impl_->core.Leave(reason); }

SendOutcome HostedSession::Send(std::string_view channel, const void* data, uint32_t len) {
    return ToOutcome(impl_->core.Send(channel, data, len));
}

bool HostedSession::Hosting() const { return impl_->core.GetState() == lib::State::Hosting; }

SessionStats HostedSession::Stats() const {
    const lib::Stats& library = impl_->core.GetStats();
    SessionStats stats;
    stats.datagramsIn = library.datagramsIn;
    stats.datagramsOut = library.datagramsOut;
    stats.malformed = library.malformed;
    stats.badVersion = library.badVersion;
    stats.badTag = library.badTag;
    stats.replayed = library.replayed;
    stats.spoofed = library.spoofed;
    stats.unknownChannel = library.unknownChannel;
    stats.refused = library.refused;
    stats.handshakeDropped = library.handshakeDropped;
    stats.joinsRefused = library.joinsRefused;
    stats.retransmits = library.retransmits;
    return stats;
}

}  // namespace sco::relay
