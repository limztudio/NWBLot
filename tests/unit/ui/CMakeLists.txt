nwb_declare_static_library(nwb_ui_name_cases)
nwb_target_adhd_sources(nwb_ui_name_cases NAMESPACE "NWB::Tests::UiCases" SOURCES "${CMAKE_CURRENT_LIST_DIR}/name_cases.adhd")

nwb_declare_gtest_executable(nwb_ui_tests)
nwb_target_adhd_sources(nwb_ui_tests NAMESPACE "__hidden_ui_glyph_sdf_tests" SOURCES "${CMAKE_CURRENT_LIST_DIR}/glyph_shader_case_types.adhd")
target_sources(nwb_ui_tests PRIVATE
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
    "${CMAKE_SOURCE_DIR}/tests/common/font_fixture.h"
)
target_link_libraries(nwb_ui_tests PRIVATE nwb_ui_name_cases nwb_ui_gpu nwb_ui_images nwb_ui_edit nwb_ui_widgets nwb_ui nwb_ui_text nwb_assets_font_atlas nwb::harfbuzz)
target_compile_definitions(nwb_ui_tests PRIVATE NWB_TEST_FONT_DIRECTORY="${CMAKE_SOURCE_DIR}/impl/assets/ui/fonts/default")

