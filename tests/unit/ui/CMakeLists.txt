nwb_declare_gtest_executable(nwb_ui_tests)
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
)
target_link_libraries(nwb_ui_tests PRIVATE nwb_ui_gpu nwb_ui_images nwb_ui_edit nwb_ui_widgets nwb_ui nwb_ui_text nwb::harfbuzz)
target_compile_definitions(nwb_ui_tests PRIVATE NWB_TEST_FONT_DIRECTORY="${CMAKE_SOURCE_DIR}/impl/assets/ui/fonts/default")
