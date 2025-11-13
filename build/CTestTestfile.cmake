# CMake generated Testfile for 
# Source directory: /Users/avimaslow/CLionProjects/Map
# Build directory: /Users/avimaslow/CLionProjects/Map/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
include("/Users/avimaslow/CLionProjects/Map/build/unit_tests-b12d07c_include.cmake")
add_test([=[orderbook_smoke]=] "/Users/avimaslow/CLionProjects/Map/build/orderbook_smoke")
set_tests_properties([=[orderbook_smoke]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/avimaslow/CLionProjects/Map/CMakeLists.txt;74;add_test;/Users/avimaslow/CLionProjects/Map/CMakeLists.txt;0;")
subdirs("_deps/catch2-build")
