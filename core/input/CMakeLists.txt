nwb_declare_static_library(nwb_input_codes)
nwb_target_adhd_sources(nwb_input_codes NAMESPACE "NWB::Core::InputCodeValues" SOURCES "${CMAKE_CURRENT_LIST_DIR}/input_codes.adhd")

nwb_declare_static_library(nwb_input)
nwb_target_adhd_sources(nwb_input NAMESPACE "NWB::Core::InputArenaScope" SOURCES "${CMAKE_CURRENT_LIST_DIR}/arena_names.adhd")
target_sources(nwb_input PRIVATE
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
target_link_libraries(nwb_input PUBLIC nwb_input_codes nwb_common nwb_alloc)

