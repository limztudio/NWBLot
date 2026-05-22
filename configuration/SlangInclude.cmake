include_guard(GLOBAL)

function(nwb_configure_slang_include)
    if(TARGET nwb::slangc)
        return()
    endif()

    set(NWB_SLANGC_EXECUTABLE "" CACHE FILEPATH "Path to the Slang command-line compiler.")

    set(_slangc_executable "${NWB_SLANGC_EXECUTABLE}")
    if(NOT _slangc_executable)
        find_program(_slangc_executable
            NAMES slangc slangc.exe
            HINTS
                "$ENV{VULKAN_SDK}/Bin"
                "$ENV{VULKAN_SDK}/bin"
            PATH_SUFFIXES bin Bin
        )
    endif()

    if(NOT _slangc_executable)
        message(FATAL_ERROR "NWB_BUILD_RESOURCE_COOKER requires slangc. Install slangc, set VULKAN_SDK, or provide NWB_SLANGC_EXECUTABLE.")
    endif()

    if(NOT EXISTS "${_slangc_executable}")
        message(FATAL_ERROR "NWB_SLANGC_EXECUTABLE does not exist: ${_slangc_executable}")
    endif()

    file(TO_CMAKE_PATH "${_slangc_executable}" _slangc_executable_define)
    set(NWB_SLANGC_EXECUTABLE "${_slangc_executable_define}" CACHE FILEPATH "Path to the Slang command-line compiler." FORCE)

    add_executable(nwb_slangc IMPORTED GLOBAL)
    set_target_properties(nwb_slangc PROPERTIES IMPORTED_LOCATION "${NWB_SLANGC_EXECUTABLE}")
    add_executable(nwb::slangc ALIAS nwb_slangc)
endfunction()
