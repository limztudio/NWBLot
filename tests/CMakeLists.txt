# Use the engine entry point so all GoogleTest suites share runtime initialization.
function(nwb_declare_gtest_executable target)
    nwb_declare_executable(${target})
    target_sources(${target} PRIVATE "${CMAKE_SOURCE_DIR}/tests/common/gtest_nwb_main.cpp")
    target_link_libraries(${target} PRIVATE nwb::gtest nwb_common nwb_alloc)
    add_test(NAME ${target} COMMAND ${target})
endfunction()

find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_subdirectory(unit)
add_subdirectory(integration)
add_subdirectory(smoke)
