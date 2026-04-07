include_guard(GLOBAL)

include(CheckCXXCompilerFlag)

function(nwb_get_latest_cxx_flag out_var)
    if(DEFINED NWB_LATEST_CXX_FLAG AND NOT NWB_LATEST_CXX_FLAG STREQUAL "")
        set(${out_var} "${NWB_LATEST_CXX_FLAG}" PARENT_SCOPE)
        return()
    endif()

    if(NWB_COMPILER_FRONTEND_MSVC)
        set(_cxx_flag_candidates
            /std:c++latest
            /std:c++23
            /std:c++20
        )
    elseif(NWB_COMPILER_FRONTEND_GNU)
        set(_cxx_flag_candidates
            -std=c++2c
            -std=c++23
            -std=c++20
        )
    else()
        message(FATAL_ERROR
            "Unsupported C++ frontend '${NWB_COMPILER_FRONTEND}' while selecting the latest language mode."
        )
    endif()

    set(_selected_flag "")
    foreach(_candidate IN LISTS _cxx_flag_candidates)
        string(REGEX REPLACE "[^A-Za-z0-9]" "_" _candidate_id "${_candidate}")
        set(_probe_var "NWB_HAS_CXX_FLAG_${_candidate_id}")
        check_cxx_compiler_flag("${_candidate}" ${_probe_var})
        if(${_probe_var})
            set(_selected_flag "${_candidate}")
            break()
        endif()
    endforeach()

    if(NOT _selected_flag)
        message(FATAL_ERROR
            "Could not find a supported C++ language mode flag for ${NWB_COMPILER_DESCRIPTION}."
        )
    endif()

    set(NWB_LATEST_CXX_FLAG "${_selected_flag}" CACHE INTERNAL "Latest supported C++ language mode flag")
    set(${out_var} "${_selected_flag}" PARENT_SCOPE)
endfunction()

function(nwb_apply_latest_cxx target)
    nwb_get_latest_cxx_flag(_nwb_latest_cxx_flag)
    target_compile_options(${target} PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:${_nwb_latest_cxx_flag}>
    )
endfunction()
