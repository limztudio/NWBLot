# NVIDIA Nsight Aftermath GPU crash dump SDK (prebuilt, vendored).
#
# The SDK ships only as a runtime shared library (.dll / .so) plus headers; there is no static
# library. The engine loads it dynamically (LoadLibrary / dlopen) so the dependency stays optional
# and graceful: on non-NVIDIA GPUs, or when the runtime is absent, the engine simply skips Aftermath
# instead of failing to launch. Consumers therefore link only the headers (nwb::aftermath_headers)
# and call nwb_copy_aftermath_runtime() to place the runtime next to their executable.
#
# License: the .dll / .so are the redistributable portions per the bundled LICENSE; the headers and
# import lib are build-time only. See LICENSE and nsight-aftermath-usage-guidelines.txt.

set(_nwb_aftermath_include "${CMAKE_CURRENT_LIST_DIR}/include")

if(WIN32)
    set(_nwb_aftermath_runtime "${CMAKE_CURRENT_LIST_DIR}/lib/windows/x64/GFSDK_Aftermath_Lib.x64.dll")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_nwb_aftermath_runtime "${CMAKE_CURRENT_LIST_DIR}/lib/linux/x64/libGFSDK_Aftermath_Lib.x64.so")
else()
    set(_nwb_aftermath_runtime "")
endif()

add_library(nwb_vendor_aftermath_headers INTERFACE)
target_include_directories(nwb_vendor_aftermath_headers SYSTEM INTERFACE "${_nwb_aftermath_include}")
if(_nwb_aftermath_runtime AND EXISTS "${_nwb_aftermath_runtime}")
    target_compile_definitions(nwb_vendor_aftermath_headers INTERFACE NWB_WITH_AFTERMATH=1)
    set_property(GLOBAL PROPERTY NWB_AFTERMATH_RUNTIME "${_nwb_aftermath_runtime}")
endif()
add_library(nwb::aftermath_headers ALIAS nwb_vendor_aftermath_headers)

# Copy the Aftermath runtime next to ${target}'s executable so the dynamic load resolves it from the
# application directory. No-op when the runtime is unavailable for the current platform.
function(nwb_copy_aftermath_runtime target)
    get_property(_runtime GLOBAL PROPERTY NWB_AFTERMATH_RUNTIME)
    if(NOT _runtime)
        return()
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_runtime}" "$<TARGET_FILE_DIR:${target}>"
        COMMENT "Copying NVIDIA Aftermath runtime next to ${target}"
        VERBATIM
    )
    if(NOT WIN32)
        set_property(TARGET ${target} APPEND PROPERTY BUILD_RPATH "$ORIGIN")
    endif()
endfunction()
