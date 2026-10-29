nwb_declare_gtest_executable(nwb_graphics_presentation_tests)
target_sources(nwb_graphics_presentation_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/cooperative_vector_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/device_creation_policy_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/presentation_receipt_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.h"
    "${CMAKE_CURRENT_LIST_DIR}/*.hh"
    "${CMAKE_CURRENT_LIST_DIR}/*.hxx"
    "${CMAKE_CURRENT_LIST_DIR}/*.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.inl"
    "${CMAKE_CURRENT_LIST_DIR}/*.ipp"
)
target_link_libraries(nwb_graphics_presentation_tests PRIVATE nwb_graphics_backend nwb_common nwb_alloc)

nwb_declare_gtest_executable(nwb_graphics_resource_tests)
target_sources(nwb_graphics_resource_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/gpu_*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/resource_queue_sharing_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.h"
    "${CMAKE_CURRENT_LIST_DIR}/*.hh"
    "${CMAKE_CURRENT_LIST_DIR}/*.hxx"
    "${CMAKE_CURRENT_LIST_DIR}/*.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.inl"
    "${CMAKE_CURRENT_LIST_DIR}/*.ipp"
)
target_link_libraries(nwb_graphics_resource_tests PRIVATE nwb_graphics_backend nwb_common nwb_alloc)
