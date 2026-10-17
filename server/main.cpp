// SPDX-License-Identifier: GPL-3.0-only
// sco-relay: the program. It reads one config file, hosts one sco.net session on one UDP port
// until it gets SIGTERM or SIGINT, then tells the players the session is over, prints a last
// stats line and exits 0. Everything it does besides wiring is in the other files of server/.
//
//     sco-relay <config-file>
//
// Exit status: 0 after a clean stop, 1 when it cannot start (the message says why and what to
// do), 2 when it is started the wrong way.
#include "server/address_rule.h"
#include "server/config.h"
#include "server/log.h"
#include "server/net_boundary.h"
#include "server/run_loop.h"
#include "server/session.h"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <signal.h>
#include <string>

namespace {

using sco::relay::LogLevel;

volatile sig_atomic_t g_stopSignal = 0;   // set by the signal handler, read by the loop

void OnStopSignal(int signalNumber) { g_stopSignal = signalNumber; }

// No SA_RESTART: a signal must cut the wait short so the loop sees it at once.
void InstallStopHandlers() {
    struct sigaction action{};
    action.sa_handler = OnStopSignal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, nullptr);
    sigaction(SIGINT, &action, nullptr);
}

// The caller's clock for the session: milliseconds that only go forward.
uint64_t NowMs() {
    using namespace std::chrono;
    return static_cast<uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

void PrintUsage(std::FILE* out) {
    std::fprintf(out,
                 "usage: sco-relay <config-file>\n"
                 "\n"
                 "Hosts one sco.net session on one UDP port. Copy server/sco-relay.example.conf, set the\n"
                 "passphrase and max_players in it, and give the copy as the argument. Stop the server\n"
                 "with SIGTERM or Ctrl+C: it tells the players and exits 0.\n");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 2 && (std::strcmp(argv[1], "-h") == 0 || std::strcmp(argv[1], "--help") == 0)) {
        PrintUsage(stdout);
        return 0;
    }
    if (argc != 2) {
        PrintUsage(stderr);
        return 2;
    }

    sco::relay::Settings settings;
    std::string why;
    if (!sco::relay::LoadConfig(argv[1], &settings, &why)) {
        std::fprintf(stderr, "sco-relay: %s\n", why.c_str());
        return 1;
    }
    sco::relay::Logger log(settings.logLevel, sco::relay::StderrSink());
    log.Log(LogLevel::Info, "starting with the settings in %s", argv[1]);

    // Bind first, so a port that is taken is reported before anything else is set up.
    std::unique_ptr<sco::relay::UdpSocket> socket = sco::relay::UdpSocket::Open(settings.port, false, &why);
    if (!socket) {
        log.Log(LogLevel::Error,
                "cannot listen on UDP port %u: %s. Another program may be using the port. Change 'port' in the "
                "config file, or stop the other program.",
                settings.port, why.c_str());
        return 1;
    }
    log.Log(LogLevel::Info, "listening on UDP port %u", socket->Port());
    log.Log(LogLevel::Info,
            "players may connect from: %s. Anything from another address is dropped; add ranges to "
            "allowed_addresses to let more players in.",
            sco::relay::DescribeAddressRule(settings.allowedAddresses).c_str());
    log.Log(LogLevel::Info,
            "traffic is signed with the passphrase but not encrypted: players who need privacy should connect "
            "through a VPN");

    InstallStopHandlers();
    sco::relay::RelaySession session(*socket, settings, log);
    if (!session.Start(NowMs(), &why)) {
        log.Log(LogLevel::Error, "cannot start the session: %s", why.c_str());
        return 1;
    }

    sco::relay::RunLoop loop(session, settings, log);
    while (g_stopSignal == 0) {
        loop.Pass(NowMs());
        socket->Wait(loop.WaitMs());
    }

    log.Log(LogLevel::Info, "received %s: shutting down", g_stopSignal == SIGINT ? "SIGINT" : "SIGTERM");
    session.Stop("the relay server is shutting down");
    loop.ReportStats(NowMs());
    log.Log(LogLevel::Info, "stopped");
    return 0;
}
