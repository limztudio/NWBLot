# Use the engine entry point so all GoogleTest suites share runtime initialization.
function(nwb_declare_gtest_executable target)
    nwb_declare_executable(${target})
    target_sources(${target} PRIVATE
        "${CMAKE_SOURCE_DIR}/tests/common/gtest_nwb_main.cpp"
    )
    target_link_libraries(${target} PRIVATE nwb::gtest nwb_common nwb_alloc)
    add_test(NAME ${target} COMMAND ${target})
endfunction()

find_package(Python3 COMPONENTS Interpreter REQUIRED)

# Harnesses exit with this code when the host cannot run a test; window_capture_smoke.py SKIP_EXIT_CODE matches it.
set(NWB_TEST_SKIP_RETURN_CODE 77)
# Tests that own the desktop or the foreground window take this lock so they never run together.
set(NWB_TEST_DISPLAY_RESOURCE_LOCK nwb_display)

add_subdirectory(unit)
add_subdirectory(integration)
add_subdirectory(smoke)
add_subdirectory(name_symbols)
add_subdirectory(performance)
