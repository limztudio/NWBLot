if(WIN32)
    add_library(nwb_vendor_zlib STATIC)
    add_library(nwb::zlib ALIAS nwb_vendor_zlib)
    target_sources(nwb_vendor_zlib PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/adler32.c"
        "${CMAKE_CURRENT_LIST_DIR}/compress.c"
        "${CMAKE_CURRENT_LIST_DIR}/crc32.c"
        "${CMAKE_CURRENT_LIST_DIR}/deflate.c"
        "${CMAKE_CURRENT_LIST_DIR}/gzclose.c"
        "${CMAKE_CURRENT_LIST_DIR}/gzlib.c"
        "${CMAKE_CURRENT_LIST_DIR}/gzread.c"
        "${CMAKE_CURRENT_LIST_DIR}/gzwrite.c"
        "${CMAKE_CURRENT_LIST_DIR}/inflate.c"
        "${CMAKE_CURRENT_LIST_DIR}/infback.c"
        "${CMAKE_CURRENT_LIST_DIR}/inftrees.c"
        "${CMAKE_CURRENT_LIST_DIR}/inffast.c"
        "${CMAKE_CURRENT_LIST_DIR}/trees.c"
        "${CMAKE_CURRENT_LIST_DIR}/uncompr.c"
        "${CMAKE_CURRENT_LIST_DIR}/zutil.c"
        "${CMAKE_CURRENT_LIST_DIR}/zconf.h"
        "${CMAKE_CURRENT_LIST_DIR}/zlib.h"
        "${CMAKE_CURRENT_LIST_DIR}/crc32.h"
        "${CMAKE_CURRENT_LIST_DIR}/deflate.h"
        "${CMAKE_CURRENT_LIST_DIR}/gzguts.h"
        "${CMAKE_CURRENT_LIST_DIR}/inffast.h"
        "${CMAKE_CURRENT_LIST_DIR}/inffixed.h"
        "${CMAKE_CURRENT_LIST_DIR}/inflate.h"
        "${CMAKE_CURRENT_LIST_DIR}/inftrees.h"
        "${CMAKE_CURRENT_LIST_DIR}/trees.h"
        "${CMAKE_CURRENT_LIST_DIR}/zutil.h"
    )
    target_include_directories(nwb_vendor_zlib SYSTEM PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}"
    )
    target_compile_definitions(nwb_vendor_zlib PRIVATE
        NO_FSEEKO
        _CRT_SECURE_NO_DEPRECATE
        _CRT_NONSTDC_NO_DEPRECATE
    )
    nwb_disable_vendor_warnings(nwb_vendor_zlib)
else()
    find_package(ZLIB REQUIRED)
    add_library(nwb_zlib INTERFACE)
    add_library(nwb::zlib ALIAS nwb_zlib)
    target_link_libraries(nwb_zlib INTERFACE ZLIB::ZLIB)
endif()
