nwb_declare_static_library(nwb_os)
target_sources(nwb_os PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/clipboard.h"
    "${CMAKE_CURRENT_LIST_DIR}/clipboard_service.h"
    "${CMAKE_CURRENT_LIST_DIR}/clipboard_service.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/clipboard_factory.cpp"
)
target_link_libraries(nwb_os PUBLIC nwb_alloc PRIVATE nwb_common)
if(WIN32)
    target_sources(nwb_os PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/win32/clipboard_service.h"
        "${CMAKE_CURRENT_LIST_DIR}/win32/clipboard_service.cpp"
    )
    target_link_libraries(nwb_os PRIVATE user32)
endif()
