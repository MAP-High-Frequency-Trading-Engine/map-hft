cmake_minimum_required(VERSION 3.23)
project(map_hft LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)
find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)

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

FetchContent_Declare(
        websocketpp
        GIT_REPOSITORY https://github.com/zaphoyd/websocketpp.git
        GIT_TAG 0.8.2
)

FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
)

FetchContent_Declare(
        asio
        GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
        GIT_TAG asio-1-30-2
)

FetchContent_Populate(websocketpp)
FetchContent_Populate(nlohmann_json)
FetchContent_Populate(asio)

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
        src/strategy/ZeroDTEStrategy.cpp
        src/strategy/GammaScalpStrategy.cpp
        src/execution/AlgoExecutor.cpp
        src/risk/RiskLimits.cpp        # Risk limits
        src/risk/GreeksRisk.cpp
        src/risk/PnLTracker.cpp        # PnL tracker (NEW, lives in core)
        src/MarketDataFeed.cpp         # Market data feed
        src/alpaca/AlpacaClient.cpp
        src/alpaca/AlpacaExecutor.cpp
        src/alpaca/AlpacaStream.cpp
        src/alpaca/OptionChainProvider.cpp
        src/logic/OptionManager.cpp
)

target_include_directories(map_core
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${websocketpp_SOURCE_DIR}
        ${asio_SOURCE_DIR}/asio/include
        ${nlohmann_json_SOURCE_DIR}/single_include
)

target_link_libraries(map_core
        PUBLIC
        CURL::libcurl
        Threads::Threads
        OpenSSL::SSL
        OpenSSL::Crypto
)
target_compile_definitions(map_core PUBLIC ASIO_STANDALONE WEBSOCKETPP_CPP11_STL)

# ----------------------------------------------------
# Text-based demo / main sample
# ----------------------------------------------------
add_executable(map_hft_main
        src/main.cpp
)
target_link_libraries(map_hft_main PRIVATE map_core)

# ----------------------------------------------------
# Live simulation app (multi-symbol)
# ----------------------------------------------------
add_executable(live_sim
        src/apps/live_sim.cpp          # only the app source
)
target_link_libraries(live_sim PRIVATE map_core)

# ----------------------------------------------------
# Live Alpaca bridge
# ----------------------------------------------------
add_executable(alpaca_bridge
        src/apps/alpaca_bridge.cpp
)
target_link_libraries(alpaca_bridge PRIVATE map_core)

# ----------------------------------------------------
# Replay verification app
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
# Live simulation from real LOB CSV data (optional)
# ----------------------------------------------------
# add_executable(live_sim_csv
#         src/apps/live_sim_csv.cpp
# )
# target_link_libraries(live_sim_csv PRIVATE map_core)

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
        tests/test_risk.cpp
        tests/test_replay.cpp
        tests/test_greeks_risk.cpp
        tests/test_lockfree_queue.cpp
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
target_compile_definitions(live_sim PRIVATE MAP_BENCHMARK_MODE)
target_compile_definitions(replay_test PRIVATE MAP_BENCHMARK_MODE)
