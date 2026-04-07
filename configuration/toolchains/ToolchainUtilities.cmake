include_guard(GLOBAL)

function(nwb_toolchain_append_existing_roots out_var)
    set(_roots)
    foreach(_candidate IN LISTS ARGN)
        if(_candidate)
            file(TO_CMAKE_PATH "${_candidate}" _candidate_path)
            if(EXISTS "${_candidate_path}")
                list(APPEND _roots "${_candidate_path}")
            endif()
        endif()
    endforeach()
    list(REMOVE_DUPLICATES _roots)
    set(${out_var} "${_roots}" PARENT_SCOPE)
endfunction()

function(nwb_toolchain_make_hints out_var)
    set(_hints)
    foreach(_root IN LISTS ARGN)
        if(_root)
            list(APPEND _hints "${_root}" "${_root}/bin")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES _hints)
    set(${out_var} "${_hints}" PARENT_SCOPE)
endfunction()

function(nwb_toolchain_find_required_program out_var)
    set(options)
    set(oneValueArgs DESCRIPTION)
    set(multiValueArgs NAMES HINTS)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "${options}" "${oneValueArgs}" "${multiValueArgs}")

    find_program(_nwb_toolchain_program
        NAMES ${ARG_NAMES}
        HINTS ${ARG_HINTS}
        NO_CACHE
    )
    if(NOT _nwb_toolchain_program)
        if(ARG_DESCRIPTION)
            set(_description "${ARG_DESCRIPTION}")
        else()
            list(JOIN ARG_NAMES ", " _description)
        endif()
        message(FATAL_ERROR
            "Could not locate ${_description}. Install LLVM/Ninja or set NWB_LLVM_ROOT, LLVM_ROOT, or NWB_NINJA."
        )
    endif()

    set(${out_var} "${_nwb_toolchain_program}" PARENT_SCOPE)
endfunction()

function(nwb_toolchain_find_vs_installation out_var)
    set(_vs_installation "")

    if(DEFINED ENV{VSINSTALLDIR} AND EXISTS "$ENV{VSINSTALLDIR}")
        file(TO_CMAKE_PATH "$ENV{VSINSTALLDIR}" _vs_installation)
    else()
        find_program(_vswhere
            NAMES vswhere vswhere.exe
            HINTS
                "C:/Program Files (x86)/Microsoft Visual Studio/Installer"
                "$ENV{ProgramFiles}/Microsoft Visual Studio/Installer"
            NO_CACHE
        )
        if(_vswhere)
            execute_process(
                COMMAND "${_vswhere}" -latest -products * -property installationPath
                OUTPUT_VARIABLE _vswhere_output
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            if(_vswhere_output)
                file(TO_CMAKE_PATH "${_vswhere_output}" _vs_installation)
            endif()
        endif()
    endif()

    set(${out_var} "${_vs_installation}" PARENT_SCOPE)
endfunction()
