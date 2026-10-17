// SPDX-License-Identifier: GPL-3.0-only
// One test over real UDP sockets on the loopback address, in one process: a player joins the server
// through sco-core's own socket class, and the server listens on the UdpSocket of server/net_boundary.h,
// the one the program uses. The program itself is tested by test_program.sh.
#include "sco/net/core.h"
#include "sco/net/scope.h"
#include "sco/net/udp.h"
#include "server/config.h"
#include "server/log.h"
#include "server/session.h"
#include "server/tests/check.h"
#include "server/tests/session_kit.h"
#include <chrono>
#include <memory>
#include <string>
#include <string_view>

namespace {

uint64_t NowMs() {
    using namespace std::chrono;
    return static_cast<uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

void TestRealUdpJoin() {
    std::string why;
    std::unique_ptr<sco::relay::UdpSocket> serverSocket = sco::relay::UdpSocket::Open(0, true, &why);
    std::unique_ptr<sco::net::udp::Socket> clientSocket = sco::net::udp::Socket::Open(0, true, &why);
    CHECK(serverSocket != nullptr);
    CHECK(clientSocket != nullptr);
    if (!serverSocket || !clientSocket) return;

    sco::relay::Logger quiet(sco::relay::LogLevel::Error, [](sco::relay::LogLevel, const std::string&) {});
    sco::relay::RelaySession session(*serverSocket, relaytest::TestSettings(2), quiet, relaytest::kTestIters);
    CHECK(session.Start(NowMs(), &why));

    bool up = false;
    sco::net::Callbacks callbacks;
    callbacks.state = [&up](bool active, std::string_view) {
        if (active) up = true;
    };
    sco::net::Core player(*clientSocket, callbacks);
    sco::net::Endpoint host;
    CHECK(sco::net::ParseAddress("127.0.0.1", serverSocket->Port(), &host));
    sco::net::Options options;
    options.playerName = "Ann";
    options.passphrase = relaytest::kPassphrase;
    options.pbkdf2Iters = relaytest::kTestIters;
    CHECK(player.Join(options, host, NowMs()));

    const uint64_t deadline = NowMs() + 5000;
    while (!up && NowMs() < deadline) {
        const uint64_t now = NowMs();
        session.Pump(now);
        player.Pump(now);
        serverSocket->Wait(1);
    }
    CHECK(up);
    CHECK(session.PlayerCount() == 1);
    session.Stop("test over");
}

}  // namespace

namespace relaytest {

void RunUdpTests() { Run("TestRealUdpJoin", TestRealUdpJoin); }

}  // namespace relaytest
