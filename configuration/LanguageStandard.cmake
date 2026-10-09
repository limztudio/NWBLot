include_guard(GLOBAL)

include(CheckCXXSourceCompiles)

function(nwb_get_latest_cxx_flag out_var)
    if(NWB_COMPILER_FRONTEND_MSVC)
        set(_cxx_flag_candidates
            /std:c++latest
            /std:c++23
        )
    elseif(NWB_COMPILER_FRONTEND_GNU)
        set(_cxx_flag_candidates
            -std=c++2c
            -std=c++23
        )
    else()
        message(FATAL_ERROR
            "Unsupported C++ frontend '${NWB_COMPILER_FRONTEND}' while selecting the latest language mode."
        )
    endif()

    set(_selected_flag "")
    set(_required_flags "${CMAKE_REQUIRED_FLAGS}")
    foreach(_candidate IN LISTS _cxx_flag_candidates)
        string(REGEX REPLACE "[^A-Za-z0-9]" "_" _candidate_id "${_candidate}")
        set(_probe_var "NWB_HAS_CXX_EXPECTED_FLAG_${_candidate_id}")
        set(CMAKE_REQUIRED_FLAGS "${_required_flags} ${_candidate}")
        check_cxx_source_compiles([=[
            #include <expected>
            #if !defined(__cpp_lib_expected) || __cpp_lib_expected < 202202L
            #error C++23 expected is required
            #endif
            constexpr std::expected<int, int> success(1);
            constexpr std::expected<int, int> failure(std::unexpected(2));
            static_assert(success && *success == 1);
            static_assert(!failure && failure.error() == 2);
            int main(){ return 0; }
        ]=] ${_probe_var})
        if(${_probe_var})
            set(_selected_flag "${_candidate}")
            break()
        endif()
    endforeach()

    if(NOT _selected_flag)
        message(FATAL_ERROR
            "C++23 or newer is required for std::expected; no supported language mode was found for ${NWB_COMPILER_DESCRIPTION}."
        )
    endif()

    set(${out_var} "${_selected_flag}" PARENT_SCOPE)
endfunction()

function(nwb_apply_latest_cxx target)
    nwb_get_latest_cxx_flag(_nwb_latest_cxx_flag)
    target_compile_options(${target} PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:${_nwb_latest_cxx_flag}>
    )
endfunction()
