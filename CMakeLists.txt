cmake_minimum_required(VERSION 3.30)

project(Lustre
    VERSION 0.1.0
    DESCRIPTION "Lustre — styling and animation layer for Iris"
    LANGUAGES CXX)

if((CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14)
   OR (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 19))
  message(FATAL_ERROR "Lustre needs GCC 14 or later or Clang 19 or later; found ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
endif()

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# ---------------------------------------------------------------------------
# Cimmerian — the ecosystem's own BDD-style test framework, vendored as a git
# submodule the same way `iris` does. No CIMMERIAN_VISUAL_PLATFORM/
# CIMMERIAN_ENABLE_* override needed here (an earlier commit forced
# CIMMERIAN_VISUAL_PLATFORM to "None" before this add_subdirectory, working
# around an older vendored Cimmerian that built its visual/snapshot sources
# unconditionally, defaulting to X11 on Linux and so pulling in
# find_package(X11 REQUIRED) + libXtst for a capability this repo's tests
# never used -- and critically, that force-set ran unconditionally as part of
# processing this file, clobbering CIMMERIAN_VISUAL_PLATFORM even when a
# consumer fetching this repo alongside iris-proto's own vendored Cimmerian
# had already set it correctly, since CMake always re-executes this file's
# statements regardless of add_subdirectory's own internal `if(TARGET
# cimmerian)` early-return self-guard): the vendored commit now bumped gates
# all of that behind CIMMERIAN_ENABLE_VISUAL_TESTING/
# CIMMERIAN_ENABLE_SNAPSHOT_TESTING, each defaulting to
# CIMMERIAN_IS_ROOT_PROJECT (false here), so both already default off with
# nothing set, and the removed force-set can no longer clobber a consumer's
# own choice.
# ---------------------------------------------------------------------------
add_subdirectory(libs/cimmerian)

# ---------------------------------------------------------------------------
# Amanuensis — the ecosystem's own JSON library, vendored as a git submodule
# the same way `iris` does (iris/libs/amanuensis). Used only by lustre_lsp's
# JSON-RPC transport (tools/lustre-lsp/JsonRpc.h) — the lustre core library
# itself has no JSON dependency.
# ---------------------------------------------------------------------------
add_subdirectory(libs/amanuensis)

# ---------------------------------------------------------------------------
# Lustre core — static library. Owns parsing, cascade/selector resolution,
# and variable resolution, producing a generic resolved-style IR (docs
# /lustre_core_spec.md §3). Deliberately has no dependency on Penumbra, Iris,
# or any other backend/host — mapping the IR onto a concrete backend's style
# structs is a separate bridge repo's job (`iris-penumbra-backend` for
# Penumbra), mirroring the `iris`/`iris-penumbra-backend` split exactly.
# ---------------------------------------------------------------------------
add_library(lustre STATIC
    src/Lustre/Tokenizer.cpp
    src/Lustre/Parser.cpp
    src/Lustre/Resolver.cpp)

target_include_directories(lustre PUBLIC include)
target_compile_features(lustre PUBLIC cxx_std_26)

# ---------------------------------------------------------------------------
# lustre-lsp — stdio LSP server for `.lustre` files (docs/lustre_lsp_decision.md).
# Split into a lib + a one-file executable, same as iris/tools/iris-lsp, so
# lustre_lsp_tests can link the real implementation without also linking a second
# `main()`.
# ---------------------------------------------------------------------------
add_library(lustre_lsp_lib STATIC
    tools/lustre-lsp/Server.cpp
    tools/lustre-lsp/CompletionContext.cpp
    tools/lustre-lsp/JsonRpc.cpp)
target_include_directories(lustre_lsp_lib PUBLIC tools/lustre-lsp)
target_link_libraries(lustre_lsp_lib PUBLIC lustre amanuensis)

add_executable(lustre_lsp tools/lustre-lsp/main.cpp)
target_link_libraries(lustre_lsp PRIVATE lustre_lsp_lib)

# ---------------------------------------------------------------------------
# Tests — one Cimmerian-based executable, matching `iris`'s
# DESCRIBE/IT/ASSERT_* + auto-registration convention (see that repo's
# CLAUDE.md "Build and test" section).
# ---------------------------------------------------------------------------
add_executable(test_lustre
    tests/TestMain.cpp
    tests/TokenizerTests.cpp
    tests/ParserTests.cpp
    tests/ResolverTests.cpp
    tests/InheritanceTests.cpp)

target_link_libraries(test_lustre PRIVATE lustre cimmerian)
set_target_properties(test_lustre PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")

add_executable(test_lustre_lsp
    tools/lustre-lsp/tests/TestMain.cpp
    tools/lustre-lsp/tests/JsonRpcTests.cpp
    tools/lustre-lsp/tests/CompletionContextTests.cpp
    tools/lustre-lsp/tests/ServerTests.cpp)

target_link_libraries(test_lustre_lsp PRIVATE lustre_lsp_lib cimmerian)
set_target_properties(test_lustre_lsp PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools/lustre-lsp/tests")
