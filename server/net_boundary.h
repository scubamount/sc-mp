// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The boundary with sco-core's network library. Everything the server needs from that library comes
// through this header, in types of our own. The one file that includes the library's headers is
// net_boundary.cpp, and server/tools/check-no-game-code.sh fails if any other file in server/ does.
//
// Why a boundary: the library's session class (Core) is internal to sco-core, and its author never
// promised to keep it stable. If it changes, one file of ours changes with it. Two checks say so
// before anyone has to read a compiler error: server/tools/check-net-boundary.sh (no compiler needed) and
// the static_asserts in net_boundary.cpp. server/README.md, '## The sco-core boundary', says what to do.
//
// This header includes standard headers only, so it can be read, and compiled, without sco-core.
//
// What is here, in order:
//   the library's limits and defaults   kSessionSeats, kPlayerNameMaxBytes, GetLibraryDefaults()
//   Address, AddressRange               where a datagram comes from, and a range of such places
//   DatagramTransport                   the pipe the session runs on: a real socket, or a memory wire in tests
//   UdpSocket                           the real socket
//   AddressFilter                       the address rule, as a pipe in front of the session
//   HostedSession                       the session around the library's Core, with channels and events of our own
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sco::relay {

using PeerId = uint64_t;
inline constexpr PeerId kServerPeer = 1;   // the server is always peer 1; players are 2, 3, ... and a number is never reused

// ---- the library's limits and defaults -------------------------------------------------------------

// The seats of one session, the server's own included. This is the number the build expects of the
// library, and net_boundary.cpp fails to compile when the library says another. A test build that
// runs on a private copy of the library with more seats (never shipped) sets this one definition:
// -DSCO_RELAY_EXPECTED_SESSION_LIMIT=50.
#ifndef SCO_RELAY_EXPECTED_SESSION_LIMIT
#define SCO_RELAY_EXPECTED_SESSION_LIMIT 16
#endif
inline constexpr uint32_t kSessionSeats = SCO_RELAY_EXPECTED_SESSION_LIMIT;

inline constexpr uint32_t kPlayerNameMaxBytes = 64;   // also the longest server_name

// What the library uses when nobody says otherwise. Read from the library at run time, so there is
// no second copy of these numbers here.
struct LibraryDefaults {
    uint32_t pbkdf2Iters = 0;       // password stretching rounds: part of the protocol, players use the same
    uint32_t joinTimeoutMs = 0;
    uint32_t resendMs = 0;
    uint32_t maxResendMs = 0;
    uint32_t keepaliveMs = 0;
    uint32_t peerTimeoutMs = 0;     // how long a player's game waits for a server it does not hear from
};
LibraryDefaults GetLibraryDefaults();

// A player or server name: 1 to kPlayerNameMaxBytes bytes, no control characters.
bool IsValidPlayerName(std::string_view name);

// ---- addresses -------------------------------------------------------------------------------------

struct Address {
    static constexpr uint8_t kNone = 0, kIpv4 = 4, kIpv6 = 6, kMemory = 0xFF;   // kMemory: the tests' in-memory wire
    uint8_t  family = kNone;
    uint16_t port = 0;
    uint8_t  bytes[16] = {};   // an IPv4 address in the first 4 bytes
    bool operator==(const Address&) const = default;
};

struct AddressRange {
    uint8_t family = Address::kNone;
    uint8_t bits = 0;            // prefix length
    uint8_t bytes[16] = {};      // network order; bits past the prefix are zero
};

// "100.64.0.0/10", or one address such as "203.0.113.7". false for anything else.
bool ParseAddressRange(std::string_view text, AddressRange* out);
// Loopback and the private ranges: the built-in part of the address rule.
bool IsPrivateAddress(const Address& address);
bool InAddressRange(const AddressRange& range, const Address& address);

// ---- the pipe --------------------------------------------------------------------------------------

// Datagram semantics, as UDP: a send may be lost, duplicated or reordered, nothing is split or merged.
class DatagramTransport {
public:
    virtual ~DatagramTransport() = default;
    // Queues one datagram. false when it could not be queued (the session treats that as a loss). Never blocks.
    virtual bool Send(const Address& to, const uint8_t* data, size_t len) = 0;
    // Takes the next datagram: copies up to cap bytes to buf, sets *len to its full size (more than cap
    // means it was cut) and *from to where it came from. false when nothing is waiting. Never blocks.
    virtual bool Receive(Address* from, uint8_t* buf, size_t cap, size_t* len) = 0;
};

// The real socket. IPv4 only.
class UdpSocket final : public DatagramTransport {
public:
    // Binds the port on every IPv4 interface, or on 127.0.0.1 only with loopbackOnly; port 0 lets the
    // system pick one (Port() tells which). nullptr, with *why set, when the system refuses.
    static std::unique_ptr<UdpSocket> Open(uint16_t port, bool loopbackOnly, std::string* why);
    ~UdpSocket() override;
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    bool Send(const Address& to, const uint8_t* data, size_t len) override;
    bool Receive(Address* from, uint8_t* buf, size_t cap, size_t* len) override;

    // true when a datagram is waiting (now or within ms), false on timeout or error.
    bool Wait(uint32_t ms);
    uint16_t Port() const;

private:
    struct Impl;
    explicit UdpSocket(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

// The address rule as a pipe: datagrams from outside it never reach the session, and none is sent to
// an address outside it. The rule is the library's own (loopback and the private ranges) plus the
// extra ranges given here; there is no way to switch it off.
class AddressFilter final : public DatagramTransport {
public:
    AddressFilter(DatagramTransport& inner, const std::vector<AddressRange>& extraRanges);
    ~AddressFilter() override;
    AddressFilter(const AddressFilter&) = delete;
    AddressFilter& operator=(const AddressFilter&) = delete;

    bool Send(const Address& to, const uint8_t* data, size_t len) override;
    bool Receive(Address* from, uint8_t* buf, size_t cap, size_t* len) override;

    uint64_t Dropped() const;                    // datagrams received from outside the rule
    bool LastRejected(Address* out) const;       // where the latest one came from; false if none did

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ---- the hosted session ----------------------------------------------------------------------------

// Who may send on a channel, and whether a message is resent until it arrives.
enum class ChannelMode : uint8_t {
    ReliableFromServer,     // only the server sends; arrives once, in order
    UnreliableFromServer,   // only the server sends; may be lost
    UnreliableToServer,     // players send to the server only, which does not pass it on; may be lost
};

enum class SendOutcome : uint8_t {
    Sent,              // queued for every player that registered the channel
    NoSession,         // not hosting
    UnknownChannel,    // the server never added this channel
    TooLarge,          // longer than the channel's maximum
    WrongDirection,
    NobodyListening,   // no player has registered the channel (yet); nothing was queued
    QueueFull,         // a player's queue is full; the message was dropped for that player
};
const char* SendOutcomeName(SendOutcome outcome);

struct HostRequest {
    std::string passphrase;
    std::string serverName;        // what players see for peer 1
    uint32_t seats = 0;            // the session's size, the server included
    uint32_t pbkdf2Iters = 0;
    uint32_t joinTimeoutMs = 0;
    uint32_t resendMs = 0;
    uint32_t maxResendMs = 0;
    uint32_t keepaliveMs = 0;
    uint32_t peerTimeoutMs = 0;
};

// What the session tells the server. All of them run inside Pump, on the calling thread, and may call Send.
struct SessionEvents {
    std::function<void(PeerId peer, std::string_view name)> playerJoined;
    // `how` reads as the end of a sentence: "left: bye", "timed out", "rejoined".
    std::function<void(PeerId peer, std::string_view how)> playerLeft;
    std::function<void(bool active, std::string_view reason)> stateChanged;
    // A message on a channel the server added; `data` is valid for the call only.
    std::function<void(std::string_view channel, PeerId from, const uint8_t* data, uint32_t len)> message;
};

// The library's counters, for the stats line and for tests. Every datagram it drops lands in one of them.
struct SessionStats {
    uint64_t datagramsIn = 0, datagramsOut = 0;
    uint64_t malformed = 0, badVersion = 0, badTag = 0, replayed = 0, spoofed = 0;
    uint64_t unknownChannel = 0, refused = 0, handshakeDropped = 0;
    uint64_t joinsRefused = 0, retransmits = 0;
};

// One session with the server as host, over a pipe it is given. Driven by Pump(nowMs) with the
// caller's clock; nothing here sleeps or reads a clock.
class HostedSession {
public:
    // `transport` must outlive the session.
    HostedSession(DatagramTransport& transport, SessionEvents events);
    ~HostedSession();
    HostedSession(const HostedSession&) = delete;
    HostedSession& operator=(const HostedSession&) = delete;

    // Adds a channel. false for a bad or repeated name, or a bad maximum. Add channels before Host.
    bool AddChannel(std::string_view name, ChannelMode mode, uint32_t maxBytes);
    // Starts hosting. false, and nothing changes, unless idle with usable settings.
    bool Host(const HostRequest& request, uint64_t nowMs);
    // Receives what is waiting, then runs the timers: resends, keepalives, timeouts.
    void Pump(uint64_t nowMs);
    // Tells every player the session is over, with a reason they will read, and ends it.
    void End(std::string_view reason);
    SendOutcome Send(std::string_view channel, const void* data, uint32_t len);

    bool Hosting() const;
    SessionStats Stats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace sco::relay
