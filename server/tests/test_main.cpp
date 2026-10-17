// SPDX-License-Identifier: GPL-3.0-only
// Entry point of the server unit tests: runs every group and prints the summary line that ends in
// "N passed, 0 failed" when all is well, as sco-core's test binaries do. Exit status 1 on any failure.
#include "server/tests/check.h"
#include <cstdio>

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    relaytest::RunAddressRuleTests();
    relaytest::RunConfigTests();
    relaytest::RunRelayWireTests();
    relaytest::RunRosterTests();
    relaytest::RunNearbyTests();
    relaytest::RunSessionTests();
    relaytest::RunNearbySessionTests();
    relaytest::RunLoopTests();
    relaytest::RunUdpTests();
    std::printf("sco-relay server tests: %d passed, %d failed\n", relaytest::g_pass, relaytest::g_fail);
    return relaytest::g_fail ? 1 : 0;
}
