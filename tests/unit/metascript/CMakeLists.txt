nwb_declare_gtest_executable(nwb_metascript_tests)
target_sources(nwb_metascript_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/*.cc"
    "${CMAKE_CURRENT_LIST_DIR}/*.cxx"
    "${CMAKE_CURRENT_LIST_DIR}/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.h"
    "${CMAKE_CURRENT_LIST_DIR}/*.hh"
    "${CMAKE_CURRENT_LIST_DIR}/*.hxx"
    "${CMAKE_CURRENT_LIST_DIR}/*.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.inl"
    "${CMAKE_CURRENT_LIST_DIR}/*.ipp"
    "${CMAKE_CURRENT_LIST_DIR}/*.ixx"
)
target_link_libraries(nwb_metascript_tests PRIVATE nwb_metascript nwb_common nwb_alloc)

if(TARGET nwb_adhd_compiler)
    nwb_target_adhd_sources(nwb_metascript_tests NAMESPACE "NWB::Tests" SOURCES "${CMAKE_CURRENT_LIST_DIR}/generated/reflection_container.adhd" "${CMAKE_CURRENT_LIST_DIR}/generated/compact_string.adhd" "${CMAKE_CURRENT_LIST_DIR}/generated/builtin_shadow_global.adhd")
    target_sources(nwb_metascript_tests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/generated/reflection_container_tests.cpp" "${CMAKE_CURRENT_LIST_DIR}/generated/compact_string_tests.cpp")
endif()
