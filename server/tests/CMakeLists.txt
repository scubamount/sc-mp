# SPDX-License-Identifier: GPL-3.0-only
# The server's tests, four of them, all registered with CTest so that `ctest` runs them with the rest of sc-mp:
#   test_server          the unit tests (config, address rule, wire bytes, roster, nearby-only forwarding, session,
#                        run loop, real UDP on loopback)
#   sco_relay_program    runs the real program and checks its exit status
#   net_boundary_checks  proves the sco-core boundary checks fire when they should: it compiles a header on its own
#                        and alters copies of the kit's files
#   relay_no_game_code   proves the server holds no game code and touches the kit's network library in one file
# Linux only, like the server.

add_executable(test_server
  test_main.cpp
  test_address_rule.cpp
  test_config.cpp
  test_nearby.cpp
  test_nearby_session.cpp
  test_relay_wire.cpp
  test_roster.cpp
  test_session.cpp
  test_run_loop.cpp
  test_udp.cpp)
target_link_libraries(test_server PRIVATE sco_relay_server)
# net_mem.h, the kit's in-memory network for tests, used from the extracted kit, unchanged. Only this test program
# includes it; the server does not.
target_include_directories(test_server PRIVATE "${SCO_CORE_SOURCE_ROOT}/tests")
target_compile_definitions(test_server PRIVATE
  SCO_RELAY_EXAMPLE_CONFIG="${SCO_RELAY_ROOT}/server/sco-relay.example.conf")
sco_relay_target_setup(test_server)
add_test(NAME test_server COMMAND test_server)
set_tests_properties(test_server PROPERTIES TIMEOUT 600)

add_test(NAME sco_relay_program
  COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/test_program.sh" "$<TARGET_FILE:sco-relay>")
set_tests_properties(sco_relay_program PROPERTIES TIMEOUT 300)

# The compiler is the one this build uses, so the header test and the compile-time checks see what the build sees.
add_test(NAME net_boundary_checks
  COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/test_net_boundary_checks.sh"
          "${SCO_RELAY_ROOT}" "${SCO_CORE_SOURCE_ROOT}" "${CMAKE_CXX_COMPILER}")
set_tests_properties(net_boundary_checks PROPERTIES TIMEOUT 600)

# What the build looks like, written when CMake generates, for server/tools/check-no-game-code.sh: where the
# libraries and the program are, what each links and passes on to what links it (INTERFACE_LINK_LIBRARIES, which
# reaches the program's link line through the target that lists it), the file that holds the program's link line
# where the generator writes one, and the kit's folder. The script reads these, so most of it works with any CMake
# generator; the real link line is read only where there is a file for it (Makefile generators).
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/relay-build-facts.txt" CONTENT
"cache=${CMAKE_BINARY_DIR}/CMakeCache.txt
kit=${SCO_CORE_SOURCE_ROOT}
program=$<TARGET_FILE:sco-relay>
program_sources=$<TARGET_PROPERTY:sco-relay,SOURCES>
program_links=$<TARGET_PROPERTY:sco-relay,LINK_LIBRARIES>
program_interface_links=$<TARGET_PROPERTY:sco-relay,INTERFACE_LINK_LIBRARIES>
link_file=$<TARGET_PROPERTY:sco-relay,BINARY_DIR>/CMakeFiles/sco-relay.dir/link.txt
server_library=$<TARGET_FILE:sco_relay_server>
server_links=$<TARGET_PROPERTY:sco_relay_server,LINK_LIBRARIES>
server_interface_links=$<TARGET_PROPERTY:sco_relay_server,INTERFACE_LINK_LIBRARIES>
net_library=$<TARGET_FILE:sco_net>
net_links=$<TARGET_PROPERTY:sco_net,LINK_LIBRARIES>
net_interface_links=$<TARGET_PROPERTY:sco_net,INTERFACE_LINK_LIBRARIES>
sockets_library=$<TARGET_FILE:sco_relay_sockets>
sockets_links=$<TARGET_PROPERTY:sco_relay_sockets,LINK_LIBRARIES>
sockets_interface_links=$<TARGET_PROPERTY:sco_relay_sockets,INTERFACE_LINK_LIBRARIES>
")
add_test(NAME relay_no_game_code
  COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/../tools/check-no-game-code.sh"
          "${SCO_RELAY_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/relay-build-facts.txt")
set_tests_properties(relay_no_game_code PROPERTIES TIMEOUT 120)
