nwb_declare_static_library(nwb_os)
target_sources(nwb_os PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/clipboard.h"
    "${CMAKE_CURRENT_LIST_DIR}/clipboard_service.h"
    "${CMAKE_CURRENT_LIST_DIR}/clipboard_service.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/clipboard_factory.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/clipboard_text.h"
    "${CMAKE_CURRENT_LIST_DIR}/clipboard_text.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/text_input.h"
    "${CMAKE_CURRENT_LIST_DIR}/text_input_service.h"
    "${CMAKE_CURRENT_LIST_DIR}/text_input_service.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/text_input_events.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/text_input_factory.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/text_input_text.h"
    "${CMAKE_CURRENT_LIST_DIR}/text_input_text.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/linux/x11/text_input_dispatch.h"
    "${CMAKE_CURRENT_LIST_DIR}/linux/x11/text_input_preedit.h"
    "${CMAKE_CURRENT_LIST_DIR}/linux/x11/text_input_preedit_buffer.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/linux/wayland/text_input_state.h"
    "${CMAKE_CURRENT_LIST_DIR}/linux/wayland/text_input_state.cpp"
)
target_link_libraries(nwb_os PUBLIC nwb_alloc PRIVATE nwb_common)
if(WIN32)
    target_sources(nwb_os PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/win32/clipboard_service.h"
        "${CMAKE_CURRENT_LIST_DIR}/win32/clipboard_service.cpp"
    )
    target_sources(nwb_os PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/win32/text_input.h"
        "${CMAKE_CURRENT_LIST_DIR}/win32/text_input_context.h"
        "${CMAKE_CURRENT_LIST_DIR}/win32/text_input_service.h"
        "${CMAKE_CURRENT_LIST_DIR}/win32/text_input_service.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/win32/text_input_messages.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/win32/text_input_fallback.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/win32/text_input_composition.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/win32/text_input_preedit.cpp"
    )
    target_link_libraries(nwb_os PRIVATE user32 imm32)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(nwb_os PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/linux/x11/clipboard.h"
        "${CMAKE_CURRENT_LIST_DIR}/linux/x11/clipboard_service.h"
        "${CMAKE_CURRENT_LIST_DIR}/linux/x11/clipboard_service.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/linux/x11/clipboard_read.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/linux/x11/clipboard_owner.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/linux/x11/x11_checked.h"
        "${CMAKE_CURRENT_LIST_DIR}/linux/x11/x11_checked.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/linux/pipe_io.h"
        "${CMAKE_CURRENT_LIST_DIR}/linux/pipe_io.cpp"
    )
    target_link_libraries(nwb_os PRIVATE nwb::x11)
    include("${CMAKE_CURRENT_LIST_DIR}/linux/text_input.cmake")

    if(TARGET nwb::wayland)
        target_sources(nwb_os PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/linux/wayland/clipboard.h"
            "${CMAKE_CURRENT_LIST_DIR}/linux/wayland/clipboard_service.h"
            "${CMAKE_CURRENT_LIST_DIR}/linux/wayland/clipboard_service.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/linux/wayland/clipboard_protocol.cpp"
        )
        target_link_libraries(nwb_os PRIVATE nwb::wayland)
        target_compile_definitions(nwb_os PRIVATE NWB_OS_WITH_WAYLAND=1)
        include("${CMAKE_CURRENT_LIST_DIR}/linux/wayland/primary_selection.cmake")
        nwb_enable_primary_selection_protocol(nwb_os)
    endif()
endif()
