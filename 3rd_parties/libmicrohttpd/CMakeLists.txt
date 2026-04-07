include("${PROJECT_SOURCE_DIR}/configuration/ImportedLibraries.cmake")

if(WIN32)
    add_library(nwb_vendor_microhttpd STATIC)
    add_library(nwb::microhttpd ALIAS nwb_vendor_microhttpd)
    target_sources(nwb_vendor_microhttpd PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/microhttpd/basicauth.c"
        "${CMAKE_CURRENT_LIST_DIR}/microhttpd/connection.c"
        "${CMAKE_CURRENT_LIST_DIR}/microhttpd/daemon.c"
        "${CMAKE_CURRENT_LIST_DIR}/microhttpd/digestauth.c"
        "${CMAKE_CURRENT_LIST_DIR}/microhttpd/gen_auth.c"
        "${CMAKE_CURRENT_LIST_DIR}/microhttpd/internal.c"
        "${CMAKE_CURRENT_LIST_DIR}/microhttpd/md5.c"
        "${CMAKE_CURRENT_LIST_DIR}/microhttpd/memorypool.c"
        "${CMAKE_CURRENT_LIST_DIR}/microhttpd/mhd_mono_clock.c"
        "${CMAKE_CURRENT_LIST_DIR}/microhttpd/mhd_panic.c"
        "${CMAKE_CURRENT_LIST_DIR}/microhttpd/mhd_send.c"
        "${CMAKE_CURRENT_LIST_DIR}/microhttpd/mhd_sockets.c"
        "${CMAKE_CURRENT_LIST_DIR}/microhttpd/mhd_str.c"
        "${CMAKE_CURRENT_LIST_DIR}/microhttpd/mhd_threads.c"
        "${CMAKE_CURRENT_LIST_DIR}/microhttpd/postprocessor.c"
        "${CMAKE_CURRENT_LIST_DIR}/microhttpd/reason_phrase.c"
        "${CMAKE_CURRENT_LIST_DIR}/microhttpd/response.c"
        "${CMAKE_CURRENT_LIST_DIR}/microhttpd/sha256.c"
        "${CMAKE_CURRENT_LIST_DIR}/microhttpd/sha512_256.c"
        "${CMAKE_CURRENT_LIST_DIR}/microhttpd/sysfdsetsize.c"
        "${CMAKE_CURRENT_LIST_DIR}/microhttpd/tsearch.c"
    )
    target_include_directories(nwb_vendor_microhttpd PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}"
    )
    target_include_directories(nwb_vendor_microhttpd SYSTEM PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/include"
    )
    target_compile_definitions(nwb_vendor_microhttpd
        PRIVATE
            _LIB
            MHD_W32LIB
            _CRT_SECURE_NO_DEPRECATE
            WIN32_LEAN_AND_MEAN=1
    )
    nwb_disable_vendor_warnings(nwb_vendor_microhttpd)
    if(NWB_COMPILER_FRONTEND_MSVC)
        target_compile_options(nwb_vendor_microhttpd PRIVATE /wd4127 /wd5287)
    endif()
    target_link_libraries(nwb_vendor_microhttpd PUBLIC ws2_32)
else()
    nwb_find_library_target(nwb_microhttpd REQUIRED
        INCLUDE_NAME microhttpd.h
        LIB_NAMES microhttpd
    )
    add_library(nwb::microhttpd ALIAS nwb_microhttpd)
endif()
