cmake_minimum_required(VERSION 3.20)
project(jolt C)

include(Grimace)

execute_process(COMMAND git describe --tags --always --dirty WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE JOLT_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT JOLT_VERSION)
    set(JOLT_VERSION dev)
endif()
add_compile_definitions(JOLT_VERSION="${JOLT_VERSION}")

set(JOLT_DEPS "${CMAKE_CURRENT_SOURCE_DIR}/build/deps" CACHE PATH "Fetched third-party sources (tools/fetch.sh)")
set(JOLT_GUEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/build/guest" CACHE PATH "Guest build output with guest-manifest.h")

add_library(jolt_digest STATIC common/digest.c "${JOLT_DEPS}/crypto-algorithms/sha256.c")
target_link_libraries(jolt_digest PRIVATE grimace::headers)
target_include_directories(jolt_digest PRIVATE "${JOLT_DEPS}/crypto-algorithms" common/shim)
target_compile_options(jolt_digest PRIVATE "SHELL:-include stdint.h -include string.h")

file(GLOB vterm_sources "${JOLT_DEPS}/libvterm-0.3.3/src/*.c")
add_library(jolt_vterm STATIC ${vterm_sources})
target_link_libraries(jolt_vterm PRIVATE grimace::headers)
target_include_directories(jolt_vterm PUBLIC "${JOLT_DEPS}/libvterm-0.3.3/include")
target_compile_options(jolt_vterm PRIVATE "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/terminal/vterm-platform.h")

file(GLOB terminal_sources terminal/*.c)
grimace_add_executable(jolt-term ${terminal_sources} common/kernel.c common/tty.c common/optional.c common/hostpath.c common/text.c OUTPUT JoLT-Term.exe ICON assets/jolt.ico)
target_include_directories(jolt-term PRIVATE common "${JOLT_GUEST_DIR}")
target_link_libraries(jolt-term PRIVATE jolt_vterm jolt_digest grimace::winsock)
target_compile_options(jolt-term PRIVATE -Wall -Wextra -Werror)

file(GLOB worker_sources worker/*.c)
grimace_add_executable(jolt-worker ${worker_sources} worker/bridge.S common/kernel.c common/tty.c common/optional.c common/hostpath.c common/text.c
    OUTPUT JoltWorker.exe TEXT_ADDRESS 0x00801000)
target_include_directories(jolt-worker PRIVATE common "${JOLT_GUEST_DIR}")
target_compile_options(jolt-worker PRIVATE -Wall -Wextra -Werror)
target_link_libraries(jolt-worker PRIVATE jolt_digest grimace::winsock)
