if(TARGET nwb::ufbx)
    return()
endif()

add_library(nwb_vendor_ufbx STATIC
    "${CMAKE_CURRENT_LIST_DIR}/ufbx.c"
    "${CMAKE_CURRENT_LIST_DIR}/ufbx.h"
)
add_library(nwb::ufbx ALIAS nwb_vendor_ufbx)

target_include_directories(nwb_vendor_ufbx SYSTEM PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}"
)

if(WIN32)
    target_compile_definitions(nwb_vendor_ufbx PRIVATE
        _CRT_SECURE_NO_WARNINGS
    )
endif()

nwb_disable_vendor_warnings(nwb_vendor_ufbx)
