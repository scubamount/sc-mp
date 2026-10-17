# SPDX-License-Identifier: GPL-3.0-only
# The relay server: one library with all of its code (the tests link it too) and the program on top. It is built
# from the published sco-core source kit (cmake/ScoCoreSource.cmake, which the root CMakeLists.txt includes when
# SC_MP_BUILD_RELAY is on) and shares plugins/multiplayer/relay_wire.h with the plugin. Linux only; see README.md.
#
# The sources include "server/<name>.h" and "plugins/multiplayer/relay_wire.h", so the include root is the
# repository root.
get_filename_component(SCO_RELAY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

# Warnings for the server's own code: -Wall -Wextra, and every warning is an error. The kit's code keeps the kit's
# own setting (SCO_WERROR); it is not ours to tighten.
function(sco_relay_target_setup target)
  target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
  set_target_properties(${target} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
endfunction()

# Every server source but main.cpp, defined once so that the tests build the very same code.
set(SCO_RELAY_SERVER_SOURCES
  address_rule.cpp
  config.cpp
  log.cpp
  nearby.cpp
  net_boundary.cpp
  relay_info.cpp
  roster.cpp
  run_loop.cpp
  session.cpp
  traffic.cpp)
list(TRANSFORM SCO_RELAY_SERVER_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")

# sco_net and sco_relay_sockets come from cmake/ScoCoreSource.cmake. We never link the kit's service library or its
# plugin host: they belong to the game plugin host.
add_library(sco_relay_server STATIC ${SCO_RELAY_SERVER_SOURCES})
target_include_directories(sco_relay_server PUBLIC "${SCO_RELAY_ROOT}")
target_link_libraries(sco_relay_server PUBLIC sco_net sco_relay_sockets)
sco_relay_target_setup(sco_relay_server)

add_executable(sco-relay main.cpp)
target_link_libraries(sco-relay PRIVATE sco_relay_server)
sco_relay_target_setup(sco-relay)

if(SC_MP_BUILD_TESTS)
  add_subdirectory(tests)
endif()
