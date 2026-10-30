cmake_minimum_required(VERSION 3.30)

project(Cosmos
    VERSION 0.1.0
    DESCRIPTION "Cosmos — styling and animation layer for Iris"
    LANGUAGES CXX)

if((CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14)
   OR (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 19))
  message(FATAL_ERROR "Cosmos needs GCC 14 or later or Clang 19 or later; found ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
endif()

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# ---------------------------------------------------------------------------
# Tests and the LSP server build by default only when Cosmos is the top-level
# project, so a consumer fetching Cosmos for the `cosmos` library doesn't also
# compile test_cosmos, cosmos_lsp or test_cosmos_lsp. Set either option ON to
# build them anyway.
# ---------------------------------------------------------------------------
option(COSMOS_BUILD_TESTS "Build test_cosmos (and test_cosmos_lsp with COSMOS_BUILD_LSP)" ${PROJECT_IS_TOP_LEVEL})
option(COSMOS_BUILD_LSP "Build cosmos_lsp" ${PROJECT_IS_TOP_LEVEL})

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
# the same way `iris` does (iris/libs/amanuensis). Used only by cosmos_lsp's
# JSON-RPC transport (tools/cosmos-lsp/JsonRpc.h) — the cosmos core library
# itself has no JSON dependency.
# ---------------------------------------------------------------------------
add_subdirectory(libs/amanuensis)

# ---------------------------------------------------------------------------
# Cosmos core — static library. Owns parsing, cascade/selector resolution,
# and variable resolution, producing a generic resolved-style IR (docs
# /cosmos_core_spec.md §3). Deliberately has no dependency on Penumbra, Iris,
# or any other backend/host — mapping the IR onto a concrete backend's style
# structs is a separate bridge repo's job (`iris-penumbra-backend` for
# Penumbra), mirroring the `iris`/`iris-penumbra-backend` split exactly.
# ---------------------------------------------------------------------------
add_library(cosmos STATIC
    src/Cosmos/Tokenizer.cpp
    src/Cosmos/Parser.cpp
    src/Cosmos/Resolver.cpp
    src/Cosmos/Properties.cpp
    src/Cosmos/Checker.cpp)

target_include_directories(cosmos PUBLIC include)
target_compile_features(cosmos PUBLIC cxx_std_26)

# ---------------------------------------------------------------------------
# cosmos-lsp — stdio LSP server for `.cosmos` files (docs/cosmos_lsp_decision.md).
# Split into a lib + a one-file executable, same as iris/tools/iris-lsp, so
# cosmos_lsp_tests can link the real implementation without also linking a second
# `main()`.
# ---------------------------------------------------------------------------
if(COSMOS_BUILD_LSP)
    add_library(cosmos_lsp_lib STATIC
        tools/cosmos-lsp/Server.cpp
        tools/cosmos-lsp/CompletionContext.cpp
        tools/cosmos-lsp/JsonRpc.cpp
        tools/cosmos-lsp/LspSupport.cpp
        tools/cosmos-lsp/Navigation.cpp
        tools/cosmos-lsp/ServerNavigation.cpp
        tools/cosmos-lsp/ServerEditing.cpp)
    target_include_directories(cosmos_lsp_lib PUBLIC tools/cosmos-lsp)
    target_link_libraries(cosmos_lsp_lib PUBLIC cosmos amanuensis)

    add_executable(cosmos_lsp tools/cosmos-lsp/main.cpp)
    target_link_libraries(cosmos_lsp PRIVATE cosmos_lsp_lib)
endif()

# ---------------------------------------------------------------------------
# Tests — one Cimmerian-based executable, matching `iris`'s
# DESCRIBE/IT/ASSERT_* + auto-registration convention (see that repo's
# CLAUDE.md "Build and test" section).
# ---------------------------------------------------------------------------
if(COSMOS_BUILD_TESTS)
    add_executable(test_cosmos
        tests/TestMain.cpp
        tests/TokenizerTests.cpp
        tests/ParserTests.cpp
        tests/ResolverTests.cpp
        tests/InheritanceTests.cpp
        tests/PropertiesTests.cpp
        tests/CheckerTests.cpp)

    target_link_libraries(test_cosmos PRIVATE cosmos cimmerian)
    set_target_properties(test_cosmos PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
endif()

if(COSMOS_BUILD_TESTS AND COSMOS_BUILD_LSP)
    add_executable(test_cosmos_lsp
        tools/cosmos-lsp/tests/TestMain.cpp
        tools/cosmos-lsp/tests/JsonRpcTests.cpp
        tools/cosmos-lsp/tests/CompletionContextTests.cpp
        tools/cosmos-lsp/tests/ServerTests.cpp
        tools/cosmos-lsp/tests/NavigationTests.cpp
        tools/cosmos-lsp/tests/ServerNavigationTests.cpp
        tools/cosmos-lsp/tests/ServerEditingTests.cpp)

    target_link_libraries(test_cosmos_lsp PRIVATE cosmos_lsp_lib cimmerian)
    set_target_properties(test_cosmos_lsp PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools/cosmos-lsp/tests")
endif()
