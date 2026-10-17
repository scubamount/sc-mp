// SPDX-License-Identifier: GPL-3.0-only
// RelaySession (server/session.h): the hosted session, the roster of players, the messages the
// server sends of its own (relay.info, multiplayer.relay) and the hand-off of nearby poses. The
// relaying itself is done by the network library; this file hosts, watches and announces.
#include "server/session.h"
#include "server/relay_info.h"
#include "plugins/multiplayer/relay_wire.h"

namespace sco::relay {

static_assert(kSessionSeats <= 0xFFFF, "relay.info and multiplayer.relay hold the player limit in 16 bits");

RelaySession::RelaySession(DatagramTransport& transport, const Settings& settings, Logger& log, uint32_t pbkdf2Iters)
    : settings_(settings),
      log_(log),
      pbkdf2Iters_(pbkdf2Iters),
      filter_(transport, settings.allowedAddresses),
      counted_(filter_),
      // A setting above the session's seats is refused by Start; the roster is not sized by it.
      roster_(settings.maxPlayers < kSessionSeats ? settings.maxPlayers : kSessionSeats),
      nearby_(settings.nearbyDistanceM, settings.nearbyStaleMs),
      hosted_(counted_, MakeEvents()) {
    sendCopy_ = [this](uint32_t slot, const uint8_t* data, size_t len) {
        return slot < nearChannels_.size() &&
               hosted_.Send(nearChannels_[slot], data, static_cast<uint32_t>(len)) == SendOutcome::Sent;
    };
}

SessionEvents RelaySession::MakeEvents() {
    SessionEvents events;
    events.playerJoined = [this](PeerId id, std::string_view name) { OnPlayerJoined(id, name); };
    events.playerLeft = [this](PeerId id, std::string_view how) { OnPlayerLeft(id, how); };
    events.stateChanged = [this](bool active, std::string_view reason) { OnStateChanged(active, reason); };
    events.message = [this](std::string_view channel, PeerId from, const uint8_t* data, uint32_t len) {
        OnMessage(channel, from, data, len);
    };
    return events;
}

bool RelaySession::AddChannel(std::string_view name, ChannelMode mode, uint32_t maxBytes, std::string* why) {
    if (hosted_.AddChannel(name, mode, maxBytes)) return true;
    *why = Format("the network library refused the channel '%s'. This is a fault in the server: report it.",
                  std::string(name).c_str());
    return false;
}

bool RelaySession::AddChannels(std::string* why) {
    // relay.info goes first: it is the one that is refused when the session was already started.
    if (!hosted_.AddChannel(kRelayInfoChannel, ChannelMode::ReliableFromServer, kRelayInfoBytes)) {
        *why = "this session was already started. Make a new RelaySession to start again.";
        return false;
    }
    if (!AddChannel(wire::kRelayChannel, ChannelMode::ReliableFromServer, wire::kRelayMaxBytes, why)) return false;
    if (!settings_.nearbyOnly) return true;

    if (!AddChannel(wire::kNearUpChannel, ChannelMode::UnreliableToServer, wire::kNearUpMaxBytes, why)) return false;
    for (uint32_t slot = 0; slot < settings_.maxPlayers; ++slot) {
        nearChannels_.push_back(wire::NearDownChannel(slot));
        if (!AddChannel(nearChannels_.back(), ChannelMode::UnreliableFromServer, wire::kNearDownMaxBytes, why))
            return false;
    }
    return true;
}

bool RelaySession::Start(uint64_t nowMs, std::string* why) {
    if (!CheckSettings(settings_, why)) return false;
    if (!AddChannels(why)) return false;

    HostRequest request;
    request.passphrase = settings_.passphrase;
    request.serverName = settings_.serverName;
    request.seats = settings_.maxPlayers + 1;   // the server takes one place
    request.pbkdf2Iters = pbkdf2Iters_;
    request.joinTimeoutMs = settings_.joinTimeoutMs;
    request.resendMs = settings_.resendMs;
    request.maxResendMs = settings_.maxResendMs;
    request.keepaliveMs = settings_.keepaliveMs;
    request.peerTimeoutMs = settings_.peerTimeoutMs;
    if (!hosted_.Host(request, nowMs)) {
        *why = "the network library refused to host a session with these settings. Check passphrase, server_name "
               "and the timers in the config file.";
        return false;
    }
    nowMs_ = nowMs;
    nextAnnounceMs_ = nowMs + settings_.announceIntervalMs;
    log_.Log(LogLevel::Info, "session is up: the server shows as '%s' (peer 1) and takes up to %u players",
             settings_.serverName.c_str(), settings_.maxPlayers);
    LogNearbyMode();
    return true;
}

void RelaySession::LogNearbyMode() {
    if (!settings_.nearbyOnly) return;
    log_.Log(LogLevel::Info,
             "nearby-only is on: a player's pose goes only to players in the same area within %u m, and to a player "
             "whose own position is unknown for %u ms. Players need a plugin that speaks multiplayer.near_up.",
             settings_.nearbyDistanceM, settings_.nearbyStaleMs);
    if (settings_.nearbyDistanceM < wire::kPluginShowDistanceM)
        log_.Log(LogLevel::Warn,
                 "nearby_distance_m is %u, below the %u m at which the plugin shows players: they would appear and "
                 "vanish at the edge. Raise nearby_distance_m in the config file.",
                 settings_.nearbyDistanceM, wire::kPluginShowDistanceM);
}

void RelaySession::Pump(uint64_t nowMs) {
    nowMs_ = nowMs;
    hosted_.Pump(nowMs);
    if (Running() && nowMs >= nextAnnounceMs_) {
        nextAnnounceMs_ = nowMs + settings_.announceIntervalMs;
        if (PlayerCount() > 0) Announce();
    }
}

void RelaySession::Stop(std::string_view reason) { hosted_.End(reason); }

void RelaySession::OnPlayerJoined(PeerId id, std::string_view name) {
    const int slot = roster_.Add(id, name);
    if (slot < 0) {
        log_.Log(LogLevel::Error,
                 "player '%s' joined as peer %llu, but every slot is taken. The library admits at most max_players, "
                 "so this should not happen: restart the server and report it.",
                 std::string(name).c_str(), Ull(id));
        return;
    }
    nearby_.PlayerJoined(id, nowMs_);
    log_.Log(LogLevel::Info, "player '%s' joined as peer %llu. %u of %u players now.", std::string(name).c_str(),
             Ull(id), PlayerCount(), settings_.maxPlayers);
    log_.Log(LogLevel::Debug, "peer %llu sits in slot %d, generation %u", Ull(id), slot,
             static_cast<unsigned>(roster_.Seats()[static_cast<size_t>(slot)].generation));
    Announce();
}

void RelaySession::OnPlayerLeft(PeerId id, std::string_view how) {
    std::string name = "?";
    roster_.Remove(id, &name);
    nearby_.PlayerLeft(id);
    // The library's reason reads as a sentence end: "left: bye", "timed out", "rejoined".
    log_.Log(LogLevel::Info, "player '%s' (peer %llu) %s. %u of %u players now.", name.c_str(), Ull(id),
             std::string(how).c_str(), PlayerCount(), settings_.maxPlayers);
}

void RelaySession::OnStateChanged(bool active, std::string_view reason) {
    if (active) return;   // Start logs the session coming up
    roster_.Clear();
    nearby_.Clear();
    log_.Log(LogLevel::Info, "the session has ended: %s", std::string(reason).c_str());
}

void RelaySession::OnMessage(std::string_view channel, PeerId from, const uint8_t* data, uint32_t len) {
    if (channel == wire::kNearUpChannel) nearby_.OnUp(roster_, from, data, len, nowMs_, sendCopy_);
}

void RelaySession::Announce() {
    SendRelayInfo();
    SendRoster();
}

void RelaySession::SendRelayInfo() {
    uint8_t message[kRelayInfoBytes];
    EncodeRelayInfo(RelayInfo{ static_cast<uint16_t>(settings_.maxPlayers) }, message);
    // Sent means queued. NobodyListening only means no player registered relay.info, which is fine.
    const SendOutcome outcome = hosted_.Send(kRelayInfoChannel, message, kRelayInfoBytes);
    log_.Log(LogLevel::Debug, "relay.info: %s", SendOutcomeName(outcome));
}

void RelaySession::SendRoster() {
    wire::RelayRoster roster;
    roster.nearbyOnly = settings_.nearbyOnly;
    roster.maxPlayers = static_cast<uint16_t>(settings_.maxPlayers);
    const std::vector<Seat>& seats = roster_.Seats();
    for (uint32_t slot = 0; slot < seats.size(); ++slot) {
        if (seats[slot].peer == 0) continue;
        roster.players.push_back(wire::RelayEntry{ seats[slot].peer, static_cast<uint16_t>(slot), seats[slot].generation });
    }
    const std::vector<uint8_t> message = wire::EncodeRelay(roster);
    const SendOutcome outcome = hosted_.Send(wire::kRelayChannel, message.data(), static_cast<uint32_t>(message.size()));
    log_.Log(LogLevel::Debug, "multiplayer.relay: %s", SendOutcomeName(outcome));
}

}  // namespace sco::relay
