find_package(Threads REQUIRED)

if(WIN32)
    add_library(nwb_vendor_tbb SHARED)
    add_library(nwb::tbb ALIAS nwb_vendor_tbb)
    target_sources(nwb_vendor_tbb PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/tbb/address_waiter.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/src/tbb/allocator.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/src/tbb/arena.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/src/tbb/arena_slot.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/src/tbb/concurrent_bounded_queue.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/src/tbb/dynamic_link.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/src/tbb/exception.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/src/tbb/global_control.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/src/tbb/governor.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/src/tbb/itt_notify.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/src/tbb/main.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/src/tbb/market.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/src/tbb/misc.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/src/tbb/misc_ex.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/src/tbb/observer_proxy.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/src/tbb/parallel_pipeline.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/src/tbb/private_server.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/src/tbb/profiling.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/src/tbb/queuing_rw_mutex.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/src/tbb/rml_tbb.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/src/tbb/rtm_mutex.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/src/tbb/rtm_rw_mutex.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/src/tbb/semaphore.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/src/tbb/small_object_pool.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/src/tbb/task.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/src/tbb/task_dispatcher.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/src/tbb/task_group_context.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/src/tbb/tcm_adaptor.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/src/tbb/threading_control.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/src/tbb/thread_dispatcher.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/src/tbb/thread_request_serializer.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/src/tbb/version.cpp"
    )
    target_include_directories(nwb_vendor_tbb SYSTEM PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")
    target_compile_definitions(nwb_vendor_tbb PRIVATE
        __TBB_BUILD
        __TBB_SKIP_DEPENDENCY_SIGNATURE_VERIFICATION=1
        __TBB_USE_ITT_NOTIFY
        _CRT_SECURE_NO_WARNINGS
    )
    if(NWB_COMPILER_IS_CLANG)
        target_compile_definitions(nwb_vendor_tbb PRIVATE
            __TBB_WAITPKG_INTRINSICS_PRESENT=0
            __TBB_TSX_INTRINSICS_PRESENT=0
        )
    endif()
    if(NWB_COMPILER_FRONTEND_MSVC)
        target_compile_options(nwb_vendor_tbb PRIVATE /wd4324)
    endif()
    nwb_disable_vendor_warnings(nwb_vendor_tbb)
    target_link_libraries(nwb_vendor_tbb PRIVATE Threads::Threads)
    target_link_options(nwb_vendor_tbb PRIVATE
        "LINKER:/DEF:${CMAKE_CURRENT_LIST_DIR}/src/tbb/def/win64-tbb.def"
    )
    set_target_properties(nwb_vendor_tbb PROPERTIES
        OUTPUT_NAME_DBG "tbb12_debug"
        OUTPUT_NAME_OPT "tbb12"
        OUTPUT_NAME_FIN "tbb12"
    )

    add_library(nwb_vendor_tbbmalloc SHARED)
    add_library(nwb::tbbmalloc ALIAS nwb_vendor_tbbmalloc)
    target_sources(nwb_vendor_tbbmalloc PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/tbbmalloc/backend.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/src/tbbmalloc/backref.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/src/tbbmalloc/frontend.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/src/tbbmalloc/large_objects.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/src/tbbmalloc/tbbmalloc.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/src/tbb/itt_notify.cpp"
    )
    target_include_directories(nwb_vendor_tbbmalloc SYSTEM PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")
    target_compile_definitions(nwb_vendor_tbbmalloc PRIVATE
        __TBBMALLOC_BUILD
        __TBB_USE_ITT_NOTIFY
        _CRT_SECURE_NO_WARNINGS
    )
    if(NWB_COMPILER_IS_CLANG)
        target_compile_definitions(nwb_vendor_tbbmalloc PRIVATE
            __TBB_WAITPKG_INTRINSICS_PRESENT=0
            __TBB_TSX_INTRINSICS_PRESENT=0
        )
    endif()
    if(NWB_COMPILER_FRONTEND_MSVC)
        target_compile_options(nwb_vendor_tbbmalloc PRIVATE /wd4324 /wd4267 /wd4244 /wd4245 /wd4458)
    endif()
    nwb_disable_vendor_warnings(nwb_vendor_tbbmalloc)
    target_link_libraries(nwb_vendor_tbbmalloc PRIVATE Threads::Threads)
    target_link_options(nwb_vendor_tbbmalloc PRIVATE
        "LINKER:/DEF:${CMAKE_CURRENT_LIST_DIR}/src/tbbmalloc/def/win64-tbbmalloc.def"
    )
    set_target_properties(nwb_vendor_tbbmalloc PROPERTIES
        OUTPUT_NAME_DBG "tbbmalloc_debug"
        OUTPUT_NAME_OPT "tbbmalloc"
        OUTPUT_NAME_FIN "tbbmalloc"
    )
else()
    find_package(TBB CONFIG QUIET)
    if(NOT TARGET TBB::tbb)
        message(FATAL_ERROR "Linux builds currently expect a system TBB package that provides TBB::tbb")
    endif()
    if(NOT TARGET TBB::tbbmalloc)
        find_library(_tbbmalloc_library NAMES tbbmalloc)
        if(NOT _tbbmalloc_library)
            message(FATAL_ERROR "Linux builds currently expect a system tbbmalloc library")
        endif()
        add_library(TBB::tbbmalloc UNKNOWN IMPORTED GLOBAL)
        get_target_property(_tbb_include_dirs TBB::tbb INTERFACE_INCLUDE_DIRECTORIES)
        set_target_properties(TBB::tbbmalloc PROPERTIES
            IMPORTED_LOCATION "${_tbbmalloc_library}"
            INTERFACE_INCLUDE_DIRECTORIES "${_tbb_include_dirs}"
        )
    endif()

    add_library(nwb_tbb INTERFACE)
    add_library(nwb::tbb ALIAS nwb_tbb)
    target_link_libraries(nwb_tbb INTERFACE TBB::tbb)

    add_library(nwb_tbbmalloc INTERFACE)
    add_library(nwb::tbbmalloc ALIAS nwb_tbbmalloc)
    target_link_libraries(nwb_tbbmalloc INTERFACE TBB::tbbmalloc)
endif()

if(NOT TARGET nwb::tbb_headers)
    add_library(nwb_tbb_headers INTERFACE)
    add_library(nwb::tbb_headers ALIAS nwb_tbb_headers)
    if(TARGET nwb::tbb)
        get_target_property(_nwb_tbb_include_dirs nwb::tbb INTERFACE_INCLUDE_DIRECTORIES)
        if(_nwb_tbb_include_dirs)
            target_include_directories(nwb_tbb_headers INTERFACE ${_nwb_tbb_include_dirs})
        else()
            target_link_libraries(nwb_tbb_headers INTERFACE nwb::tbb)
        endif()
    endif()
endif()
