target_sources(nwb_os PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/x11/text_input.h"
    "${CMAKE_CURRENT_LIST_DIR}/x11/text_input_service.h"
    "${CMAKE_CURRENT_LIST_DIR}/x11/text_input_service.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/x11/text_input_preedit.cpp"
)
if(TARGET nwb::wayland)
    target_sources(nwb_os PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/wayland/text_input.h"
        "${CMAKE_CURRENT_LIST_DIR}/wayland/text_input_service.h"
        "${CMAKE_CURRENT_LIST_DIR}/wayland/text_input_service.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/wayland/text_input_protocol.cpp"
    )
    include("${CMAKE_CURRENT_LIST_DIR}/wayland/text_input.cmake")
    nwb_enable_text_input_protocol(nwb_os)
endif()
