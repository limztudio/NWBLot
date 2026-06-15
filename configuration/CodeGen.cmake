include_guard(GLOBAL)

function(nwb_resolve_target_output_name target config output_var)
    string(TOUPPER "${config}" _nwb_config_upper)
    get_target_property(_nwb_output_name ${target} "OUTPUT_NAME_${_nwb_config_upper}")
    if(NOT _nwb_output_name)
        get_target_property(_nwb_output_name ${target} OUTPUT_NAME)
    endif()
    if(NOT _nwb_output_name)
        set(_nwb_output_name "${target}")
    endif()

    set(${output_var} "${_nwb_output_name}" PARENT_SCOPE)
endfunction()

function(nwb_apply_debug_symbols target)
    if(NOT WIN32)
        return()
    endif()

    get_target_property(_nwb_target_type ${target} TYPE)

    if(NWB_COMPILER_FRONTEND_MSVC)
        set_target_properties(${target} PROPERTIES
            MSVC_DEBUG_INFORMATION_FORMAT ProgramDatabase
        )

        foreach(_nwb_config dbg opt fin)
            string(TOUPPER "${_nwb_config}" _nwb_config_upper)
            nwb_resolve_target_output_name(${target} "${_nwb_config}" _nwb_config_output_name)
            set_target_properties(${target} PROPERTIES
                "COMPILE_PDB_OUTPUT_DIRECTORY_${_nwb_config_upper}" "${NWB_OUTPUT_ROOT}/${_nwb_config}"
            )

            if(_nwb_target_type STREQUAL "STATIC_LIBRARY")
                set_target_properties(${target} PROPERTIES
                    "COMPILE_PDB_NAME_${_nwb_config_upper}" "${_nwb_config_output_name}"
                )
            else()
                set_target_properties(${target} PROPERTIES
                    "COMPILE_PDB_NAME_${_nwb_config_upper}" "${_nwb_config_output_name}_compile"
                )
            endif()
        endforeach()
    elseif(NWB_COMPILER_IS_CLANG_GNU)
        target_compile_options(${target} PRIVATE
            -gcodeview
        )
    endif()

    if(
        _nwb_target_type STREQUAL "EXECUTABLE"
        OR _nwb_target_type STREQUAL "SHARED_LIBRARY"
        OR _nwb_target_type STREQUAL "MODULE_LIBRARY"
    )
        foreach(_nwb_config dbg opt fin)
            string(TOUPPER "${_nwb_config}" _nwb_config_upper)
            nwb_resolve_target_output_name(${target} "${_nwb_config}" _nwb_config_output_name)
            set_target_properties(${target} PROPERTIES
                "PDB_OUTPUT_DIRECTORY_${_nwb_config_upper}" "${NWB_OUTPUT_ROOT}/${_nwb_config}"
                "PDB_NAME_${_nwb_config_upper}" "${_nwb_config_output_name}"
            )
        endforeach()

        if(NWB_COMPILER_IS_CLANG_GNU)
            target_link_options(${target} PRIVATE
                LINKER:/DEBUG
            )
        endif()
    endif()
endfunction()

function(nwb_apply_codegen target)
    set_target_properties(${target} PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
    )

    nwb_apply_latest_cxx(${target})
    nwb_apply_debug_symbols(${target})

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
            $<$<CONFIG:fin>:-fno-omit-frame-pointer>
        )
    else()
        message(FATAL_ERROR "NWBLot now requires a Clang-based toolchain.")
    endif()
endfunction()
