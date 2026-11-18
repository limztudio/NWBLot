nwb_declare_static_library(nwb_test_common_names)
nwb_target_adhd_sources(nwb_test_common_names NAMESPACE "NWB::Tests::CommonCases" SOURCES "${CMAKE_CURRENT_LIST_DIR}/common/name_cases.adhd")
nwb_target_adhd_sources(nwb_test_common_names NAMESPACE "NWB::Tests" SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/common/gtest_macros.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/common/test_helpers.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/common/arena_owner_fixture.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/common/capturing_logger.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/common/ecs_test_world.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/common/filesystem_helpers.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/common/floating_point_environment_guard.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/common/font_fixture.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/common/simd_memory_guard_fixture.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/common/test_context.adhd"
)
nwb_target_adhd_sources(nwb_test_common_names NAMESPACE "NWB::Tests::Smoke" SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/common/frame_graph_snapshot_validation.adhd"
)
nwb_target_adhd_sources(nwb_test_common_names NAMESPACE "NWB::UiWidgetTests" SOURCES "${CMAKE_CURRENT_LIST_DIR}/unit/ui/arithmetic_fixture_rows.adhd")
target_link_libraries(nwb_test_common_names PUBLIC nwb::gtest nwb_common nwb_alloc PRIVATE nwb_assets_font_types)

# Use the engine entry point so all GoogleTest suites share runtime initialization.
function(nwb_declare_gtest_executable target)
    nwb_declare_executable(${target})
    nwb_target_adhd_sources(${target} NAMESPACE "NWB::Tests::GTestMain" SOURCES "${CMAKE_SOURCE_DIR}/tests/common/gtest_nwb_main.adhd")
    target_link_libraries(${target} PRIVATE nwb::gtest nwb_common nwb_alloc nwb_test_common_names)
    add_test(NAME ${target} COMMAND ${target})
endfunction()

find_package(Python3 COMPONENTS Interpreter REQUIRED)

# Match SKIP_EXIT_CODE in window_capture_smoke.py when a host cannot run a test.
set(NWB_TEST_SKIP_RETURN_CODE 77)
# Tests that own the desktop or the foreground window take this lock so they never run together.
set(NWB_TEST_DISPLAY_RESOURCE_LOCK nwb_display)

add_subdirectory(unit)
add_subdirectory(integration)
add_subdirectory(smoke)
add_subdirectory(name_symbols)
add_subdirectory(performance)
