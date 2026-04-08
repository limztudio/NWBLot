include("${PROJECT_SOURCE_DIR}/configuration/ImportedLibraries.cmake")

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
        target_compile_options(nwb_vendor_tbb PRIVATE -mrtm -mwaitpkg)
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
        target_compile_options(nwb_vendor_tbbmalloc PRIVATE -mrtm -mwaitpkg)
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
    if(TARGET TBB::tbb)
        if(NOT TARGET TBB::tbbmalloc)
            find_library(_tbbmalloc_library NAMES tbbmalloc libtbbmalloc.so.2 libtbbmalloc)
            if(NOT _tbbmalloc_library)
                message(FATAL_ERROR "Failed to locate tbbmalloc for Linux builds")
            endif()
            add_library(TBB::tbbmalloc UNKNOWN IMPORTED GLOBAL)
            get_target_property(_tbb_include_dirs TBB::tbb INTERFACE_INCLUDE_DIRECTORIES)
            set_target_properties(TBB::tbbmalloc PROPERTIES
                IMPORTED_LOCATION "${_tbbmalloc_library}"
                INTERFACE_INCLUDE_DIRECTORIES "${_tbb_include_dirs}"
            )
        endif()
    else()
        find_path(_nwb_tbb_include_dir
            NAMES tbb/tbb.h
            HINTS "${CMAKE_CURRENT_LIST_DIR}" "${CMAKE_CURRENT_LIST_DIR}/include"
            PATH_SUFFIXES include Include
        )
        find_library(_nwb_tbb_library
            NAMES tbb libtbb.so.12 libtbb
            HINTS /usr /usr/local /
            PATH_SUFFIXES lib Lib lib64
        )
        find_library(_nwb_tbbmalloc_library
            NAMES tbbmalloc libtbbmalloc.so.2 libtbbmalloc
            HINTS /usr /usr/local /
            PATH_SUFFIXES lib Lib lib64
        )

        if(NOT _nwb_tbb_include_dir OR NOT _nwb_tbb_library OR NOT _nwb_tbbmalloc_library)
            message(FATAL_ERROR
                "Failed to locate TBB. Install oneTBB development files or provide compatible TBB and tbbmalloc shared libraries with headers."
            )
        endif()

        add_library(nwb_tbb_raw UNKNOWN IMPORTED GLOBAL)
        nwb_set_imported_library_locations(nwb_tbb_raw DEFAULT "${_nwb_tbb_library}")
        set_target_properties(nwb_tbb_raw PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${_nwb_tbb_include_dir}"
            INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_nwb_tbb_include_dir}"
        )

        add_library(nwb_tbbmalloc_raw UNKNOWN IMPORTED GLOBAL)
        nwb_set_imported_library_locations(nwb_tbbmalloc_raw DEFAULT "${_nwb_tbbmalloc_library}")
        set_target_properties(nwb_tbbmalloc_raw PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${_nwb_tbb_include_dir}"
            INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_nwb_tbb_include_dir}"
        )
    endif()

    add_library(nwb_tbb INTERFACE)
    add_library(nwb::tbb ALIAS nwb_tbb)
    if(TARGET TBB::tbb)
        target_link_libraries(nwb_tbb INTERFACE TBB::tbb)
    else()
        target_link_libraries(nwb_tbb INTERFACE nwb_tbb_raw)
    endif()

    add_library(nwb_tbbmalloc INTERFACE)
    add_library(nwb::tbbmalloc ALIAS nwb_tbbmalloc)
    if(TARGET TBB::tbbmalloc)
        target_link_libraries(nwb_tbbmalloc INTERFACE TBB::tbbmalloc)
    else()
        target_link_libraries(nwb_tbbmalloc INTERFACE nwb_tbbmalloc_raw)
    endif()
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
