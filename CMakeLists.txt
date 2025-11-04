cmake_minimum_required(VERSION 3.23)
project(map_hft LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Headers live under include/
include_directories(include)

# Core library (your engine)
add_library(map_core
        src/OrderBook.cpp
)

# Main demo executable
add_executable(map_hft_main
        src/main.cpp
)
target_link_libraries(map_hft_main map_core)

# Simple smoke test for the OrderBook
add_executable(orderbook_smoke
        tests/OrderBookSmoke.cpp
)
target_link_libraries(orderbook_smoke map_core)
