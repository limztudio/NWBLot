include_guard(GLOBAL)

function(nwb_apply_codegen target)
    set_target_properties(${target} PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
    )

    nwb_apply_latest_cxx(${target})

    if(WIN32 AND (NWB_COMPILER_IS_CLANG OR NWB_COMPILER_IS_MSVC))
        set_property(TARGET ${target} PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:dbg>:Debug>DLL"
        )
    endif()

    target_compile_definitions(${target} PRIVATE
        $<$<CONFIG:dbg>:_DEBUG>
        $<$<CONFIG:dbg>:PROP_DBG>
        $<$<CONFIG:opt>:NDEBUG>
        $<$<CONFIG:opt>:PROP_OPT>
        $<$<CONFIG:fin>:NDEBUG>
        $<$<CONFIG:fin>:PROP_FIN>
    )

    if(WIN32)
        target_compile_definitions(${target} PRIVATE
            NOMINMAX
            WIN32_LEAN_AND_MEAN
        )
    endif()

    if(NWB_COMPILER_FRONTEND_MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /permissive-
            /GR-
            /wd4201
            $<$<CONFIG:dbg>:/Od>
            $<$<CONFIG:dbg>:/Ob0>
            $<$<CONFIG:dbg>:/Oy->
            $<$<CONFIG:opt>:/O2>
            $<$<CONFIG:opt>:/Ob1>
            $<$<CONFIG:opt>:/Oy->
            $<$<CONFIG:fin>:/O2>
            $<$<CONFIG:fin>:/Ob2>
            $<$<CONFIG:fin>:/Oy>
        )
        if(NWB_COMPILER_IS_MSVC)
            target_compile_options(${target} PRIVATE
                /MP
                /sdl
            )
        endif()
        target_link_options(${target} PRIVATE
            $<$<CONFIG:dbg>:/DEBUG>
            $<$<CONFIG:opt>:/DEBUG>
            $<$<CONFIG:opt>:/OPT:REF>
            $<$<CONFIG:opt>:/OPT:ICF>
            $<$<CONFIG:fin>:/DEBUG>
            $<$<CONFIG:fin>:/OPT:REF>
            $<$<CONFIG:fin>:/OPT:ICF>
        )
    elseif(NWB_COMPILER_IS_CLANG)
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
            $<$<CONFIG:dbg>:-O0>
            $<$<CONFIG:dbg>:-g>
            $<$<CONFIG:dbg>:-fno-omit-frame-pointer>
            $<$<CONFIG:opt>:-O2>
            $<$<CONFIG:opt>:-g>
            $<$<CONFIG:opt>:-fno-omit-frame-pointer>
            $<$<CONFIG:fin>:-O3>
            $<$<CONFIG:fin>:-g>
            $<$<CONFIG:fin>:-fomit-frame-pointer>
        )
    else()
        message(FATAL_ERROR "NWBLot now requires a Clang-based toolchain.")
    endif()
endfunction()
