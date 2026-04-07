include("${CMAKE_CURRENT_LIST_DIR}/ToolchainUtilities.cmake")

nwb_toolchain_find_vs_installation(_nwb_vs_installation)

nwb_toolchain_append_existing_roots(_nwb_llvm_roots
    "$ENV{NWB_LLVM_ROOT}"
    "$ENV{LLVM_ROOT}"
    "$ENV{VCINSTALLDIR}/Tools/Llvm/x64"
    "$ENV{VCINSTALLDIR}/Tools/Llvm"
    "${_nwb_vs_installation}/VC/Tools/Llvm/x64"
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
    "$ENV{WindowsSdkVerBinPath}/x64"
    "$ENV{WindowsSdkBinPath}"
    "$ENV{WindowsSdkBinPath}/x64"
)
file(GLOB _nwb_windows_sdk_version_dirs LIST_DIRECTORIES true "C:/Program Files (x86)/Windows Kits/10/bin/*")
foreach(_nwb_sdk_dir IN LISTS _nwb_windows_sdk_version_dirs)
    if(IS_DIRECTORY "${_nwb_sdk_dir}")
        list(APPEND _nwb_windows_sdk_bin_roots "${_nwb_sdk_dir}" "${_nwb_sdk_dir}/x64")
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

if(DEFINED ENV{NWB_NINJA} AND EXISTS "$ENV{NWB_NINJA}")
    file(TO_CMAKE_PATH "$ENV{NWB_NINJA}" _nwb_ninja)
else()
    nwb_toolchain_append_existing_roots(_nwb_ninja_roots
        "$ENV{NWB_NINJA_ROOT}"
        "${_nwb_vs_installation}/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja"
    )
    nwb_toolchain_make_hints(_nwb_ninja_hints ${_nwb_ninja_roots})
    nwb_toolchain_find_required_program(_nwb_ninja
        DESCRIPTION "ninja"
        NAMES ninja ninja.exe
        HINTS ${_nwb_ninja_hints}
    )
endif()

set(CMAKE_MAKE_PROGRAM "${_nwb_ninja}" CACHE FILEPATH "" FORCE)
set(CMAKE_C_COMPILER "${_nwb_clang}" CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER "${_nwb_clangxx}" CACHE FILEPATH "" FORCE)
set(CMAKE_C_COMPILER_TARGET "x86_64-pc-windows-msvc" CACHE STRING "" FORCE)
set(CMAKE_CXX_COMPILER_TARGET "x86_64-pc-windows-msvc" CACHE STRING "" FORCE)
set(CMAKE_LINKER "${_nwb_lld_link}" CACHE FILEPATH "" FORCE)
set(CMAKE_AR "${_nwb_llvm_ar}" CACHE FILEPATH "" FORCE)
set(CMAKE_RANLIB "${_nwb_llvm_ranlib}" CACHE FILEPATH "" FORCE)
set(CMAKE_RC_COMPILER "${_nwb_rc}" CACHE FILEPATH "" FORCE)
set(CMAKE_MT "${_nwb_mt}" CACHE FILEPATH "" FORCE)
set(CMAKE_NINJA_FORCE_RESPONSE_FILE ON CACHE BOOL "" FORCE)
