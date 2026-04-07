include("${CMAKE_CURRENT_LIST_DIR}/ToolchainUtilities.cmake")

nwb_toolchain_append_existing_roots(_nwb_llvm_roots
    "$ENV{NWB_LLVM_ROOT}"
    "$ENV{LLVM_ROOT}"
)
nwb_toolchain_make_hints(_nwb_llvm_hints ${_nwb_llvm_roots})

nwb_toolchain_find_required_program(_nwb_clang
    DESCRIPTION "clang"
    NAMES clang
    HINTS ${_nwb_llvm_hints}
)
nwb_toolchain_find_required_program(_nwb_clangxx
    DESCRIPTION "clang++"
    NAMES clang++
    HINTS ${_nwb_llvm_hints}
)
nwb_toolchain_find_required_program(_nwb_llvm_ar
    DESCRIPTION "llvm-ar"
    NAMES llvm-ar
    HINTS ${_nwb_llvm_hints}
)
nwb_toolchain_find_required_program(_nwb_llvm_ranlib
    DESCRIPTION "llvm-ranlib"
    NAMES llvm-ranlib
    HINTS ${_nwb_llvm_hints}
)

if(DEFINED ENV{NWB_NINJA} AND EXISTS "$ENV{NWB_NINJA}")
    file(TO_CMAKE_PATH "$ENV{NWB_NINJA}" _nwb_ninja)
else()
    nwb_toolchain_append_existing_roots(_nwb_ninja_roots
        "$ENV{NWB_NINJA_ROOT}"
        "$ENV{NWB_LLVM_ROOT}"
        "$ENV{LLVM_ROOT}"
    )
    nwb_toolchain_make_hints(_nwb_ninja_hints ${_nwb_ninja_roots})
    nwb_toolchain_find_required_program(_nwb_ninja
        DESCRIPTION "ninja"
        NAMES ninja
        HINTS ${_nwb_ninja_hints}
    )
endif()

set(CMAKE_MAKE_PROGRAM "${_nwb_ninja}" CACHE FILEPATH "" FORCE)
set(CMAKE_C_COMPILER "${_nwb_clang}" CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER "${_nwb_clangxx}" CACHE FILEPATH "" FORCE)
set(CMAKE_AR "${_nwb_llvm_ar}" CACHE FILEPATH "" FORCE)
set(CMAKE_RANLIB "${_nwb_llvm_ranlib}" CACHE FILEPATH "" FORCE)
