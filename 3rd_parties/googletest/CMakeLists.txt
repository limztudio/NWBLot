# NWB CMakeLists for vendored GoogleTest (gtest only; no gmock). Built from the single gtest-all.cc unity
# source, which #includes the other src/*.cc files. The engine supplies its own test entry point
# (tests/gtest_nwb_main.cpp) so gtest_main.cc is intentionally not compiled here. The CRT / iterator-debug
# level is aligned with engine targets by the parent via nwb_match_engine_runtime.
cmake_minimum_required(VERSION 3.13)
project(googletest CXX)

add_library(gtest STATIC "${CMAKE_CURRENT_LIST_DIR}/src/gtest-all.cc")
add_library(nwb::gtest ALIAS gtest)

target_include_directories(gtest
    PUBLIC  "${CMAKE_CURRENT_LIST_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_LIST_DIR}")   # gtest-all.cc pulls in "src/..." relative to the package root

find_package(Threads QUIET)
if(Threads_FOUND)
    target_link_libraries(gtest PUBLIC Threads::Threads)
endif()

set_target_properties(gtest PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON)
nwb_disable_vendor_warnings(gtest)
