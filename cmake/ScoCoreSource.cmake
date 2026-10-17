# SPDX-License-Identifier: GPL-3.0-only
# Fetches the published sco-core "core-source" kit, checks its SHA-256, extracts it under the build tree and adds it
# as a subproject, for the relay server (server/). It does for the kit what ScoSdk.cmake does for the SDK archive,
# and it reads the SDK version and the download folder from there, so include ScoSdk.cmake first.
#
#   SCO_CORE_SOURCE_SHA256   the pin: SHA-256 of sco-sdk-core-source-<SCO_SDK_VERSION>.zip, from the SHA256SUMS of
#                            the same release as the SDK archive. Bump it together with SCO_SDK_VERSION.
#   SCO_SDK_ARCHIVE_DIR      (ScoSdk.cmake) a local copy of the release assets. The kit is taken from there instead
#                            of downloaded. Either way the archive must match the pin or configure stops, before
#                            anything is extracted.
#
# Sets SCO_CORE_SOURCE_ROOT (the extracted kit) and makes two targets available:
#   sco_net            the kit's network library, built by the kit's own CMake file (EXCLUDE_FROM_ALL: only what
#                      is linked gets built; the game pack and the kit's own tests are off)
#   sco_relay_sockets  the kit's two socket files, src/net/udp_posix.cpp and src/net/scope.cpp, compiled from the
#                      extracted kit as they are. The kit's service library, which they belong to in the kit's own
#                      build, needs the game plugin host; the relay never links it.
# No file of the kit is copied into this repository, and no sibling sco-core checkout is read.

if(NOT DEFINED SCO_SDK_VERSION)
  message(FATAL_ERROR "cmake/ScoCoreSource.cmake needs the SDK version that cmake/ScoSdk.cmake sets: "
                      "configure with SC_MP_FETCH_SDK=ON, or leave SC_MP_BUILD_RELAY off")
endif()

# From the release SHA256SUMS, line for sco-sdk-core-source-<version>.zip (sdk-v2.3.0 release).
set(SCO_CORE_SOURCE_SHA256 "2115df435cd0808062fc8aa16467547e8a3dccb17bcabb791b2cf3d0251912ef"
    CACHE STRING "SHA-256 of sco-sdk-core-source-<version>.zip")

set(_sco_cs_name "sco-sdk-core-source-${SCO_SDK_VERSION}")
set(_sco_cs_zip_name "${_sco_cs_name}.zip")
set(_sco_cs_url
    "https://github.com/scubamount/sco-core/releases/download/sdk-v${SCO_SDK_VERSION}/${_sco_cs_zip_name}")
set(_sco_cs_dl_dir "${CMAKE_BINARY_DIR}/_sdk")
set(_sco_cs_zip "${_sco_cs_dl_dir}/${_sco_cs_zip_name}")
set(_sco_cs_src_parent "${_sco_cs_dl_dir}/core-source-${SCO_CORE_SOURCE_SHA256}")
set(SCO_CORE_SOURCE_ROOT "${_sco_cs_src_parent}/${_sco_cs_name}")

function(_sco_cs_hash_ok file out)
  set(${out} FALSE PARENT_SCOPE)
  if(EXISTS "${file}")
    file(SHA256 "${file}" _h)
    if(_h STREQUAL SCO_CORE_SOURCE_SHA256)
      set(${out} TRUE PARENT_SCOPE)
    endif()
  endif()
endfunction()

file(MAKE_DIRECTORY "${_sco_cs_dl_dir}")
_sco_cs_hash_ok("${_sco_cs_zip}" _have)
if(NOT _have)
  if(SCO_SDK_ARCHIVE_DIR)
    set(_local "${SCO_SDK_ARCHIVE_DIR}/${_sco_cs_zip_name}")
    if(NOT EXISTS "${_local}")
      message(FATAL_ERROR "SCO_SDK_ARCHIVE_DIR is set but ${_local} does not exist")
    endif()
    message(STATUS "sco-core source kit: using local archive ${_local}")
    file(COPY_FILE "${_local}" "${_sco_cs_zip}")
  else()
    message(STATUS "sco-core source kit: downloading ${_sco_cs_url}")
    file(DOWNLOAD "${_sco_cs_url}" "${_sco_cs_zip}"
         EXPECTED_HASH SHA256=${SCO_CORE_SOURCE_SHA256}
         TLS_VERIFY ON
         TIMEOUT 600
         INACTIVITY_TIMEOUT 60
         STATUS _dl_status)
    list(GET _dl_status 0 _dl_code)
    if(NOT _dl_code EQUAL 0)
      file(REMOVE "${_sco_cs_zip}")
      # EXPECTED_HASH makes file(DOWNLOAD) fail when the bytes are not the pin, and says so in the status. Any other
      # failure (no network, a TLS error) is a download failure and is reported as one.
      if(_dl_status MATCHES "HASH mismatch")
        message(FATAL_ERROR "sco-core source kit ${_sco_cs_zip_name} is not the one pinned: its SHA-256 is not "
                            "${SCO_CORE_SOURCE_SHA256} (hash mismatch): ${_dl_status}; refusing to build it")
      endif()
      message(FATAL_ERROR "sco-core source kit download failed: ${_dl_status}. Check the network, or pass "
                          "-DSCO_SDK_ARCHIVE_DIR=<folder> with the release assets.")
    endif()
  endif()
  # Whichever way the file arrived, it is checked here, before anything is extracted.
  _sco_cs_hash_ok("${_sco_cs_zip}" _have)
  if(NOT _have)
    file(SHA256 "${_sco_cs_zip}" _got)
    file(REMOVE "${_sco_cs_zip}")
    message(FATAL_ERROR "sco-core source kit ${_sco_cs_zip_name} has SHA-256 ${_got}, expected "
                        "${SCO_CORE_SOURCE_SHA256} (hash mismatch); refusing to build it")
  endif()
endif()
message(STATUS "sco-core source kit: ${_sco_cs_zip_name} SHA-256 verified")

# The extraction folder is keyed by the verified hash, so a different archive never reuses it.
if(NOT EXISTS "${SCO_CORE_SOURCE_ROOT}/SHA256SUMS")
  file(REMOVE_RECURSE "${_sco_cs_src_parent}")
  file(ARCHIVE_EXTRACT INPUT "${_sco_cs_zip}" DESTINATION "${_sco_cs_src_parent}")
  if(NOT EXISTS "${SCO_CORE_SOURCE_ROOT}/SHA256SUMS")
    message(FATAL_ERROR "sco-core source kit archive did not contain ${_sco_cs_name}/SHA256SUMS")
  endif()
endif()

# The kernel only: no Star Citizen game pack, and none of the kit's own tests (the kit's build is the one the
# kit's authors run; here it is a dependency). SCO_WERROR keeps the kit's own setting: warnings in its code are
# errors, as in its own build.
set(SCO_GAME_SC OFF CACHE BOOL "the relay never builds the game pack" FORCE)
set(SCO_BUILD_TESTS OFF CACHE BOOL "the kit's own tests are not part of this build" FORCE)
add_subdirectory("${SCO_CORE_SOURCE_ROOT}" sco-core-source EXCLUDE_FROM_ALL)

# These two files only need the kit's headers, so one compiled copy serves the server, its tests and the program.
add_library(sco_relay_sockets STATIC
  "${SCO_CORE_SOURCE_ROOT}/src/net/udp_posix.cpp"
  "${SCO_CORE_SOURCE_ROOT}/src/net/scope.cpp")
target_include_directories(sco_relay_sockets PUBLIC "${SCO_CORE_SOURCE_ROOT}/include")
target_compile_options(sco_relay_sockets PRIVATE -Wall -Wextra $<$<BOOL:${SCO_WERROR}>:-Werror>)
set_target_properties(sco_relay_sockets PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
