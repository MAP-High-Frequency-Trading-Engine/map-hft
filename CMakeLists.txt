cmake_minimum_required(VERSION 3.23)
project(map_hft LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ----------------------------------------------------
# Testing / Catch2 setup
# ----------------------------------------------------
include(CTest)
enable_testing()

include(FetchContent)

FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.7.0
)

FetchContent_MakeAvailable(Catch2)

list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)
include(Catch)

# ----------------------------------------------------
# Core engine library
# ----------------------------------------------------
add_library(map_core
        src/OrderBook.cpp              # OrderBook
        src/core/Logger.cpp            # Logger
        src/replay/LogReader.cpp       # Replay
        src/strategy/BasicStrategy.cpp # Strategy
        src/risk/RiskLimits.cpp        # Risk limits
        src/risk/PnLTracker.cpp        # PnL tracker
        src/MarketDataFeed.cpp         # Market data feed (CSVLOBFeed)
        src/risk/ImpactModel.cpp       # Impact model
)

target_include_directories(map_core
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ----------------------------------------------------
# Text-based demo / main sample
# ----------------------------------------------------
add_executable(map_hft_main
        src/main.cpp
)
target_link_libraries(map_hft_main PRIVATE map_core)

# ----------------------------------------------------
# Live simulation apps
# ----------------------------------------------------

# Single-threaded app
add_executable(live_sim
        src/apps/live_sim.cpp
)
target_link_libraries(live_sim PRIVATE map_core)

# Multi-threaded app
add_executable(live_sim_mt
        src/apps/live_sim_mt.cpp
)
target_link_libraries(live_sim_mt PRIVATE map_core)

# ----------------------------------------------------
# Simple smoke test for the OrderBook
# ----------------------------------------------------
add_executable(orderbook_smoke
        tests/OrderBookSmoke.cpp
)
target_link_libraries(orderbook_smoke PRIVATE map_core)
add_test(NAME orderbook_smoke COMMAND orderbook_smoke)

# ----------------------------------------------------
# Replay verification app
# ----------------------------------------------------
add_executable(replay_test
        src/apps/replay_test.cpp
)
target_link_libraries(replay_test PRIVATE map_core)

# ----------------------------------------------------
# Catch2 unit tests target (includes replay test)
# ----------------------------------------------------
add_executable(unit_tests
        tests/test_orderbook.cpp
        tests/test_eventbus.cpp
        tests/test_risk.cpp
        tests/test_replay.cpp
)
target_link_libraries(unit_tests
        PRIVATE
        map_core
        Catch2::Catch2WithMain
)
catch_discover_tests(unit_tests)

# ----------------------------------------------------
# Benchmark-mode compile switches
# ----------------------------------------------------
target_compile_definitions(live_sim   PRIVATE MAP_BENCHMARK_MODE)
target_compile_definitions(replay_test PRIVATE MAP_BENCHMARK_MODE)

# ----------------------------------------------------
# Optional PMR allocators in OrderBook
# ----------------------------------------------------
option(MAP_USE_PMR "Use std::pmr arena allocators in OrderBook" ON)

if (MAP_USE_PMR)
    target_compile_definitions(map_core PUBLIC MAP_USE_PMR)
endif()

# ----------------------------------------------------
# Global optimization flags for Release
# ----------------------------------------------------
if (CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O3 -DNDEBUG)
    add_link_options(-flto)
endif()
