if(WIN32)
    set(_nwb_frame_lifecycle_tests_timeout_seconds 60)
    nwb_declare_gtest_executable(nwb_frame_lifecycle_tests)
    target_sources(nwb_frame_lifecycle_tests PRIVATE
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
    target_link_libraries(nwb_frame_lifecycle_tests PRIVATE nwb_frame)
    set_tests_properties(nwb_frame_lifecycle_tests PROPERTIES RESOURCE_LOCK ${NWB_TEST_DISPLAY_RESOURCE_LOCK} TIMEOUT ${_nwb_frame_lifecycle_tests_timeout_seconds})
    unset(_nwb_frame_lifecycle_tests_timeout_seconds)
endif()
