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
        GIT_TAG v3.7.0   # any recent v3.x is fine
)

FetchContent_MakeAvailable(Catch2)

# Allow CMake to find Catch's extra CMake helpers
list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)
include(Catch)   # provides catch_discover_tests()

# ----------------------------------------------------
# Core engine library
# ----------------------------------------------------
add_library(map_core
        src/OrderBook.cpp
        src/core/Logger.cpp
        # src/core/EventBus.cpp   # EventBus is header-only right now, so no .cpp
)

target_include_directories(map_core
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ----------------------------------------------------
# Main demo executable (text-based)
# ----------------------------------------------------
add_executable(map_hft_main
        src/main.cpp
)
target_link_libraries(map_hft_main PRIVATE map_core)

# ----------------------------------------------------
# Simple smoke test for the OrderBook (manual assert-based)
# ----------------------------------------------------
add_executable(orderbook_smoke
        tests/OrderBookSmoke.cpp
)
target_link_libraries(orderbook_smoke PRIVATE map_core)

# Register it with CTest
add_test(NAME orderbook_smoke COMMAND orderbook_smoke)

# ----------------------------------------------------
# SFML OrderBook viewer
# ----------------------------------------------------
find_package(SFML 3 REQUIRED COMPONENTS Graphics Window System)

add_executable(ob_viewer
        ob_viewer.cpp        # adjust to src/ob_viewer.cpp if you move it
)
target_link_libraries(ob_viewer
        PRIVATE
        map_core
        SFML::Graphics
        SFML::Window
        SFML::System
)

# ----------------------------------------------------
# Catch2 unit tests target
# ----------------------------------------------------
add_executable(unit_tests
        tests/test_orderbook.cpp
        tests/test_eventbus.cpp
)

target_link_libraries(unit_tests
        PRIVATE
        map_core
        Catch2::Catch2WithMain
)

# Automatically register all TEST_CASEs in unit_tests with CTest
catch_discover_tests(unit_tests)
