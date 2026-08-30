include("${CMAKE_CURRENT_LIST_DIR}/ToolchainUtilities.cmake")

if(NOT DEFINED NWB_TARGET_ARCH OR NWB_TARGET_ARCH STREQUAL "")
    set(_nwb_requested_arch "${CMAKE_HOST_SYSTEM_PROCESSOR}")
else()
    set(_nwb_requested_arch "${NWB_TARGET_ARCH}")
endif()
string(TOLOWER "${_nwb_requested_arch}" _nwb_requested_arch)

if(_nwb_requested_arch MATCHES "^(x64|amd64|x86_64|x86-64)$")
    set(_nwb_target_arch "x64")
    set(_nwb_target_processor "AMD64")
    set(_nwb_target_triple "x86_64-pc-windows-msvc")
elseif(_nwb_requested_arch MATCHES "^(arm64|aarch64)$")
    set(_nwb_target_arch "arm64")
    set(_nwb_target_processor "ARM64")
    set(_nwb_target_triple "aarch64-pc-windows-msvc")
else()
    message(FATAL_ERROR "Unsupported NWB_TARGET_ARCH '${_nwb_requested_arch}'. Expected x64 or arm64.")
endif()

set(NWB_TARGET_ARCH "${_nwb_target_arch}" CACHE STRING "NWBLot target architecture" FORCE)
set_property(CACHE NWB_TARGET_ARCH PROPERTY STRINGS x64 arm64)
set(CMAKE_SYSTEM_PROCESSOR "${_nwb_target_processor}" CACHE STRING "" FORCE)

string(TOLOWER "${CMAKE_HOST_SYSTEM_PROCESSOR}" _nwb_host_processor)
if(_nwb_host_processor MATCHES "^(arm64|aarch64)$")
    set(_nwb_host_tool_arch "ARM64")
    set(_nwb_host_sdk_arch "arm64")
else()
    set(_nwb_host_tool_arch "x64")
    set(_nwb_host_sdk_arch "x64")
endif()

nwb_toolchain_find_vs_installation(_nwb_vs_installation)

nwb_toolchain_append_existing_roots(_nwb_llvm_roots
    "$ENV{NWB_LLVM_ROOT}"
    "$ENV{LLVM_ROOT}"
    "$ENV{VCINSTALLDIR}/Tools/Llvm/${_nwb_host_tool_arch}"
    "$ENV{VCINSTALLDIR}/Tools/Llvm/x64"
    "$ENV{VCINSTALLDIR}/Tools/Llvm/ARM64"
    "$ENV{VCINSTALLDIR}/Tools/Llvm"
    "${_nwb_vs_installation}/VC/Tools/Llvm/${_nwb_host_tool_arch}"
    "${_nwb_vs_installation}/VC/Tools/Llvm/x64"
    "${_nwb_vs_installation}/VC/Tools/Llvm/ARM64"
    "${_nwb_vs_installation}/VC/Tools/Llvm"
    "$ENV{ProgramFiles}/LLVM"
)
nwb_toolchain_make_hints(_nwb_llvm_hints ${_nwb_llvm_roots})

nwb_toolchain_find_required_program(_nwb_clang
    DESCRIPTION "clang"
    NAMES clang clang.exe
    HINTS ${_nwb_llvm_hints}
)
nwb_toolchain_find_required_program(_nwb_clangxx
    DESCRIPTION "clang++"
    NAMES clang++ clang++.exe
    HINTS ${_nwb_llvm_hints}
)
nwb_toolchain_find_required_program(_nwb_lld_link
    DESCRIPTION "lld-link"
    NAMES lld-link lld-link.exe
    HINTS ${_nwb_llvm_hints}
)
nwb_toolchain_find_required_program(_nwb_llvm_ar
    DESCRIPTION "llvm-ar"
    NAMES llvm-ar llvm-ar.exe
    HINTS ${_nwb_llvm_hints}
)
nwb_toolchain_find_required_program(_nwb_llvm_ranlib
    DESCRIPTION "llvm-ranlib"
    NAMES llvm-ranlib llvm-ranlib.exe
    HINTS ${_nwb_llvm_hints}
)

set(_nwb_windows_sdk_bin_roots
    "$ENV{WindowsSdkVerBinPath}"
    "$ENV{WindowsSdkVerBinPath}/${_nwb_host_sdk_arch}"
    "$ENV{WindowsSdkVerBinPath}/x64"
    "$ENV{WindowsSdkVerBinPath}/arm64"
    "$ENV{WindowsSdkBinPath}"
    "$ENV{WindowsSdkBinPath}/${_nwb_host_sdk_arch}"
    "$ENV{WindowsSdkBinPath}/x64"
    "$ENV{WindowsSdkBinPath}/arm64"
)
file(GLOB _nwb_windows_sdk_version_dirs LIST_DIRECTORIES true "C:/Program Files (x86)/Windows Kits/10/bin/*")
foreach(_nwb_sdk_dir IN LISTS _nwb_windows_sdk_version_dirs)
    if(IS_DIRECTORY "${_nwb_sdk_dir}")
        list(APPEND _nwb_windows_sdk_bin_roots
            "${_nwb_sdk_dir}"
            "${_nwb_sdk_dir}/${_nwb_host_sdk_arch}"
            "${_nwb_sdk_dir}/x64"
            "${_nwb_sdk_dir}/arm64"
        )
    endif()
endforeach()
nwb_toolchain_append_existing_roots(_nwb_windows_sdk_roots ${_nwb_windows_sdk_bin_roots})
nwb_toolchain_make_hints(_nwb_windows_sdk_hints ${_nwb_windows_sdk_roots})

nwb_toolchain_find_required_program(_nwb_rc
    DESCRIPTION "Windows rc.exe"
    NAMES rc rc.exe
    HINTS ${_nwb_windows_sdk_hints}
)
nwb_toolchain_find_required_program(_nwb_mt
    DESCRIPTION "Windows mt.exe"
    NAMES mt mt.exe
    HINTS ${_nwb_windows_sdk_hints}
)

nwb_toolchain_find_ninja(_nwb_ninja
    ROOTS "${_nwb_vs_installation}/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja"
    NAMES ninja ninja.exe
)

set(CMAKE_MAKE_PROGRAM "${_nwb_ninja}" CACHE FILEPATH "" FORCE)
set(CMAKE_C_COMPILER "${_nwb_clang}" CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER "${_nwb_clangxx}" CACHE FILEPATH "" FORCE)
set(CMAKE_C_COMPILER_TARGET "${_nwb_target_triple}" CACHE STRING "" FORCE)
set(CMAKE_CXX_COMPILER_TARGET "${_nwb_target_triple}" CACHE STRING "" FORCE)
set(CMAKE_LINKER "${_nwb_lld_link}" CACHE FILEPATH "" FORCE)
set(CMAKE_AR "${_nwb_llvm_ar}" CACHE FILEPATH "" FORCE)
set(CMAKE_RANLIB "${_nwb_llvm_ranlib}" CACHE FILEPATH "" FORCE)
set(CMAKE_RC_COMPILER "${_nwb_rc}" CACHE FILEPATH "" FORCE)
set(CMAKE_MT "${_nwb_mt}" CACHE FILEPATH "" FORCE)
set(CMAKE_NINJA_FORCE_RESPONSE_FILE ON CACHE BOOL "" FORCE)
