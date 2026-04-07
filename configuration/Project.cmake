include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/CompilerFrontend.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/BuildConfigurations.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CodeGen.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/GeneralOutput.cmake")

macro(nwb_configure_project)
    nwb_require_x64()
    nwb_configure_build_configs()
    nwb_configure_compiler_frontend()
    if(NOT NWB_COMPILER_IS_CLANG)
        message(FATAL_ERROR
            "NWBLot now requires Clang on every platform. Use the clang presets from CMakePresets.json."
        )
    endif()
    nwb_configure_general_output()
endmacro()
