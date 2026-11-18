nwb_declare_gtest_executable(nwb_crash_tests)
nwb_target_adhd_sources(nwb_crash_tests NAMESPACE "NWB::Tests::CrashCases" SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/name_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/protocol_sdk.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/suite.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/crash_package_*.adhd"
)
if(WIN32 OR (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT ANDROID))
    nwb_target_adhd_sources(nwb_crash_tests NAMESPACE "NWB::Tests::CrashCases" SOURCES "${CMAKE_CURRENT_LIST_DIR}/desktop/*.adhd")
endif()
if(WIN32)
    nwb_target_adhd_sources(nwb_crash_tests NAMESPACE "NWB::Tests::CrashCases" SOURCES "${CMAKE_CURRENT_LIST_DIR}/windows/*.adhd")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT ANDROID)
    nwb_target_adhd_sources(nwb_crash_tests NAMESPACE "NWB::Tests::CrashCases" SOURCES "${CMAKE_CURRENT_LIST_DIR}/linux/*.adhd")
endif()
target_link_libraries(nwb_crash_tests PRIVATE nwb_crash_test_paths nwb_crash_package nwb_common nwb_alloc)
if(TARGET nwb_crash_handler)
    add_dependencies(nwb_crash_tests nwb_crash_handler)
endif()
set_tests_properties(nwb_crash_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
