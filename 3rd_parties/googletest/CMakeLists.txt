# The engine supplies its own test entry point; gtest_main.cc is omitted.
# The parent aligns CRT and iterator-debug settings with engine targets.
cmake_minimum_required(VERSION 3.13)
project(googletest CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../../configuration/UnityBuild.cmake")

add_library(gtest STATIC "${CMAKE_CURRENT_LIST_DIR}/src/gtest-all.cc")
add_library(nwb::gtest ALIAS gtest)

target_include_directories(gtest
    SYSTEM PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_LIST_DIR}")   # gtest-all.cc pulls in "src/..." relative to the package root

find_package(Threads QUIET)
if(Threads_FOUND)
    target_link_libraries(gtest PUBLIC Threads::Threads)
endif()

set_target_properties(gtest PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON)
nwb_disable_vendor_warnings(gtest)
