nwb_declare_static_library(nwb_os)
target_sources(nwb_os PRIVATE
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
    "${CMAKE_CURRENT_LIST_DIR}/linux/x11/text_input_dispatch.h"
    "${CMAKE_CURRENT_LIST_DIR}/linux/x11/text_input_key_history.h"
    "${CMAKE_CURRENT_LIST_DIR}/linux/x11/text_input_key_history.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/linux/x11/text_input_preedit.h"
    "${CMAKE_CURRENT_LIST_DIR}/linux/x11/text_input_preedit_buffer.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/linux/wayland/text_input_state.h"
    "${CMAKE_CURRENT_LIST_DIR}/linux/wayland/text_input_state.cpp"
)
target_link_libraries(nwb_os PUBLIC nwb_alloc PRIVATE nwb_common)
if(WIN32)
    target_sources(nwb_os PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/win32/*.c"
        "${CMAKE_CURRENT_LIST_DIR}/win32/*.cc"
        "${CMAKE_CURRENT_LIST_DIR}/win32/*.cxx"
        "${CMAKE_CURRENT_LIST_DIR}/win32/*.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/win32/*.h"
        "${CMAKE_CURRENT_LIST_DIR}/win32/*.hh"
        "${CMAKE_CURRENT_LIST_DIR}/win32/*.hxx"
        "${CMAKE_CURRENT_LIST_DIR}/win32/*.hpp"
        "${CMAKE_CURRENT_LIST_DIR}/win32/*.inl"
        "${CMAKE_CURRENT_LIST_DIR}/win32/*.ipp"
        "${CMAKE_CURRENT_LIST_DIR}/win32/*.ixx"
    )
    target_link_libraries(nwb_os PRIVATE user32 imm32)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(nwb_os PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/linux/*.c"
        "${CMAKE_CURRENT_LIST_DIR}/linux/*.cc"
        "${CMAKE_CURRENT_LIST_DIR}/linux/*.cxx"
        "${CMAKE_CURRENT_LIST_DIR}/linux/*.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/linux/*.h"
        "${CMAKE_CURRENT_LIST_DIR}/linux/*.hh"
        "${CMAKE_CURRENT_LIST_DIR}/linux/*.hxx"
        "${CMAKE_CURRENT_LIST_DIR}/linux/*.hpp"
        "${CMAKE_CURRENT_LIST_DIR}/linux/*.inl"
        "${CMAKE_CURRENT_LIST_DIR}/linux/*.ipp"
        "${CMAKE_CURRENT_LIST_DIR}/linux/*.ixx"
    )
    target_link_libraries(nwb_os PRIVATE nwb::x11)
    include("${CMAKE_CURRENT_LIST_DIR}/linux/text_input.cmake")

    if(TARGET nwb::wayland)
        target_link_libraries(nwb_os PRIVATE nwb::wayland)
        target_compile_definitions(nwb_os PRIVATE NWB_OS_WITH_WAYLAND=1)
        include("${CMAKE_CURRENT_LIST_DIR}/linux/wayland/primary_selection.cmake")
        nwb_enable_primary_selection_protocol(nwb_os)
    endif()
endif()
