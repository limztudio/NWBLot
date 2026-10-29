target_sources(nwb_os PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/x11/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/x11/*.cc"
    "${CMAKE_CURRENT_LIST_DIR}/x11/*.cxx"
    "${CMAKE_CURRENT_LIST_DIR}/x11/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/x11/*.h"
    "${CMAKE_CURRENT_LIST_DIR}/x11/*.hh"
    "${CMAKE_CURRENT_LIST_DIR}/x11/*.hxx"
    "${CMAKE_CURRENT_LIST_DIR}/x11/*.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/x11/*.inl"
    "${CMAKE_CURRENT_LIST_DIR}/x11/*.ipp"
    "${CMAKE_CURRENT_LIST_DIR}/x11/*.ixx"
)
if(TARGET nwb::wayland)
    target_sources(nwb_os PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/wayland/*.c"
        "${CMAKE_CURRENT_LIST_DIR}/wayland/*.cc"
        "${CMAKE_CURRENT_LIST_DIR}/wayland/*.cxx"
        "${CMAKE_CURRENT_LIST_DIR}/wayland/*.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/wayland/*.h"
        "${CMAKE_CURRENT_LIST_DIR}/wayland/*.hh"
        "${CMAKE_CURRENT_LIST_DIR}/wayland/*.hxx"
        "${CMAKE_CURRENT_LIST_DIR}/wayland/*.hpp"
        "${CMAKE_CURRENT_LIST_DIR}/wayland/*.inl"
        "${CMAKE_CURRENT_LIST_DIR}/wayland/*.ipp"
        "${CMAKE_CURRENT_LIST_DIR}/wayland/*.ixx"
    )
    include("${CMAKE_CURRENT_LIST_DIR}/wayland/text_input.cmake")
    nwb_enable_text_input_protocol(nwb_os)
endif()
