add_library(nwb_vendor_miniz STATIC
    "${CMAKE_CURRENT_LIST_DIR}/miniz.c"
    "${CMAKE_CURRENT_LIST_DIR}/miniz_tdef.c"
    "${CMAKE_CURRENT_LIST_DIR}/miniz_tinfl.c"
    "${CMAKE_CURRENT_LIST_DIR}/miniz_zip.c"
    "${CMAKE_CURRENT_LIST_DIR}/miniz.h"
    "${CMAKE_CURRENT_LIST_DIR}/miniz_common.h"
    "${CMAKE_CURRENT_LIST_DIR}/miniz_export.h"
    "${CMAKE_CURRENT_LIST_DIR}/miniz_tdef.h"
    "${CMAKE_CURRENT_LIST_DIR}/miniz_tinfl.h"
    "${CMAKE_CURRENT_LIST_DIR}/miniz_zip.h"
)
add_library(nwb::miniz ALIAS nwb_vendor_miniz)

target_include_directories(nwb_vendor_miniz SYSTEM PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}"
)

nwb_disable_vendor_warnings(nwb_vendor_miniz)
nwb_apply_debug_symbols(nwb_vendor_miniz)
