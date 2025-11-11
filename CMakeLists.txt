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
        src/OrderBook.cpp          # <-- your OrderBook is here
        src/core/Logger.cpp        # <-- logger is in src/core
        src/replay/LogReader.cpp   # Week 2 replay
        src/strategy/BasicStrategy.cpp  # Week 2 strategy
)

target_include_directories(map_core
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ----------------------------------------------------
# Text-based demo / main sample
# ----------------------------------------------------
add_executable(map_hft_main
        src/main.cpp               # <-- your main is here
)
target_link_libraries(map_hft_main PRIVATE map_core)

# ----------------------------------------------------
# Live simulation app (Week 2)
# ----------------------------------------------------
add_executable(live_sim
        src/apps/live_sim.cpp
        include/map/core/RiskLimits.hpp      # ONLY the app source
)
target_link_libraries(live_sim PRIVATE map_core)

# ----------------------------------------------------
# Replay verification app (Week 2)
# ----------------------------------------------------
add_executable(replay_test
        src/apps/replay_test.cpp
)
target_link_libraries(replay_test PRIVATE map_core)

# ----------------------------------------------------
# Simple smoke test for the OrderBook
# ----------------------------------------------------
add_executable(orderbook_smoke
        tests/OrderBookSmoke.cpp
)
target_link_libraries(orderbook_smoke PRIVATE map_core)
add_test(NAME orderbook_smoke COMMAND orderbook_smoke)

# ----------------------------------------------------
# Optional SFML OrderBook viewer
# ----------------------------------------------------
option(MAP_BUILD_OB_VIEWER "Build SFML-based order book viewer" OFF)

if (MAP_BUILD_OB_VIEWER)
    # Try SFML 3.x first
    find_package(SFML 3 QUIET COMPONENTS Graphics Window System)

    if (SFML_FOUND)
        set(SFML_GRAPHICS_TARGET SFML::Graphics)
        set(SFML_WINDOW_TARGET   SFML::Window)
        set(SFML_SYSTEM_TARGET   SFML::System)
    else()
        # Fallback to SFML 2.x
        find_package(SFML REQUIRED COMPONENTS system window graphics)

        if (TARGET sfml-graphics)
            set(SFML_GRAPHICS_TARGET sfml-graphics)
            set(SFML_WINDOW_TARGET   sfml-window)
            set(SFML_SYSTEM_TARGET   sfml-system)
        else()
            message(FATAL_ERROR "SFML found but no known targets (sfml-graphics or SFML::Graphics)")
        endif()
    endif()

    add_executable(ob_viewer
            ob_viewer.cpp
    )

    target_link_libraries(ob_viewer
            PRIVATE
            map_core
            ${SFML_GRAPHICS_TARGET}
            ${SFML_WINDOW_TARGET}
            ${SFML_SYSTEM_TARGET}
    )
endif()

# ----------------------------------------------------
# Catch2 unit tests target (includes replay test)
# ----------------------------------------------------
add_executable(unit_tests
        tests/test_orderbook.cpp
        tests/test_eventbus.cpp
        tests/test_replay.cpp
)

target_link_libraries(unit_tests
        PRIVATE
        map_core
        Catch2::Catch2WithMain
)

catch_discover_tests(unit_tests)
