include_guard(DIRECTORY)

if(NOT COMMAND nwb_configure_unity_build)
    macro(nwb_configure_unity_build)
        set(CMAKE_UNITY_BUILD ON)
    endmacro()

    function(nwb_apply_unity_build target)
        set_target_properties(${target} PROPERTIES UNITY_BUILD ON)
    endfunction()

    function(nwb_skip_unity_build_sources)
        set_source_files_properties(${ARGN} PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
    endfunction()
endif()

nwb_configure_unity_build()
