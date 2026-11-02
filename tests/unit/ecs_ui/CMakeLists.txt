nwb_declare_gtest_executable(nwb_ecs_ui_tests)
target_sources(nwb_ecs_ui_tests PRIVATE
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
    "${CMAKE_SOURCE_DIR}/tests/unit/ui/fixture_text_shaping.cpp"
    "${CMAKE_SOURCE_DIR}/tests/unit/ui/fixture_text_shaping.h"
    "${CMAKE_SOURCE_DIR}/tests/unit/ui/multiline_fixture.cpp"
    "${CMAKE_SOURCE_DIR}/tests/unit/ui/multiline_fixture.h"
    "${CMAKE_SOURCE_DIR}/tests/unit/ui/native_input.cpp"
    "${CMAKE_SOURCE_DIR}/tests/unit/ui/native_input.h"
)
target_link_libraries(nwb_ecs_ui_tests PRIVATE nwb_ecs_ui nwb_ui_edit)

