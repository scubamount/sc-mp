// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The small test kit of the server tests, in sco-core's manner (tests/test_net.cpp): CHECK counts a
// pass or prints the failing line, Run prints one line per test function, and test_main.cpp prints
// the summary and returns 1 on any failure. The test groups live in the other test_*.cpp files.
#include <cstdio>

namespace relaytest {

inline int g_pass = 0;
inline int g_fail = 0;

inline void Run(const char* name, void (*test)()) {
    const int failedBefore = g_fail;
    test();
    std::printf("%s %s\n", g_fail == failedBefore ? "ok  " : "FAIL", name);
}

void RunAddressRuleTests();   // test_address_rule.cpp
void RunConfigTests();    // test_config.cpp
void RunRelayWireTests();   // test_relay_wire.cpp
void RunRosterTests();    // test_roster.cpp
void RunNearbyTests();    // test_nearby.cpp
void RunNearbySessionTests();   // test_nearby_session.cpp
void RunSessionTests();   // test_session.cpp
void RunLoopTests();      // test_run_loop.cpp
void RunUdpTests();       // test_udp.cpp

}  // namespace relaytest

#define CHECK(c) \
    do { \
        if (c) ++relaytest::g_pass; \
        else { ++relaytest::g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } \
    } while (0)
