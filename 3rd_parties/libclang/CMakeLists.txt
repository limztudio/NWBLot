if(TARGET nwb::libclang)
    return()
endif()

include(CheckIncludeFile)
include(CheckLibraryExists)
include(CheckSymbolExists)
include(CMakePushCheckState)

nwb_declare_vendor_headers(nwb_vendor_libclang_headers nwb::libclang_headers "${CMAKE_CURRENT_LIST_DIR}/include")
target_compile_definitions(nwb_vendor_libclang_headers INTERFACE CINDEX_NO_EXPORTS)

set(BUG_REPORT_URL "https://github.com/llvm/llvm-project/issues/")
set(CLANG_DEFAULT_PIE_ON_LINUX 1)
set(CLANG_DEFAULT_LINKER "")
set(CLANG_DEFAULT_CXX_STDLIB "")
set(CLANG_DEFAULT_RTLIB "")
set(CLANG_DEFAULT_UNWINDLIB "")
set(CLANG_DEFAULT_OBJCOPY "objcopy")
set(CLANG_DEFAULT_OPENMP_RUNTIME "libomp")
set(CLANG_SYSTEMZ_DEFAULT_ARCH "z10")
set(CLANG_INSTALL_LIBDIR_BASENAME "lib")
set(CLANG_RESOURCE_DIR "")
set(C_INCLUDE_DIRS "")
set(DEFAULT_SYSROOT "")
set(GCC_INSTALL_PREFIX "")
set(ENABLE_X86_RELAX_RELOCATIONS 1)
set(PPC_LINUX_DEFAULT_IEEELONGDOUBLE 0)
set(CLANG_ENABLE_OBJC_REWRITER 0)
set(CLANG_ENABLE_STATIC_ANALYZER 0)
set(CLANG_SPAWN_CC1 0)
set(CLANG_ENABLE_CIR 0)
set(CLANG_USE_EXPERIMENTAL_CONST_INTERP 0)

set(CLANG_VERSION "23.1.6")
set(CLANG_VERSION_MAJOR 23)
set(CLANG_VERSION_MINOR 1)
set(CLANG_VERSION_PATCHLEVEL 6)
set(MAX_CLANG_ABI_COMPAT_VERSION 23)

cmake_push_check_state(RESET)
if(WIN32)
    set(CLANG_HAVE_RLIMITS 0)
    set(CLANG_HAVE_DLFCN_H 0)
    set(CLANG_HAVE_DLADDR 0)
else()
    check_include_file(sys/resource.h CLANG_HAVE_RLIMITS)
    check_include_file(dlfcn.h CLANG_HAVE_DLFCN_H)
    if(CLANG_HAVE_DLFCN_H)
        check_library_exists(dl dlopen "" _nwb_clang_have_libdl)
        if(_nwb_clang_have_libdl)
            list(APPEND CMAKE_REQUIRED_LIBRARIES dl)
        endif()
        list(APPEND CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
        check_symbol_exists(dladdr dlfcn.h CLANG_HAVE_DLADDR)
    endif()
endif()
cmake_pop_check_state()

set(_nwb_clang_gen_inc "${CMAKE_CURRENT_BINARY_DIR}/include")
set(_nwb_clang_gen_basic_lib "${CMAKE_CURRENT_BINARY_DIR}/lib/Basic")
file(MAKE_DIRECTORY "${_nwb_clang_gen_inc}/clang/Config" "${_nwb_clang_gen_inc}/clang/Basic" "${_nwb_clang_gen_basic_lib}")

configure_file("${CMAKE_CURRENT_LIST_DIR}/cmake/config.h.cmake" "${_nwb_clang_gen_inc}/clang/Config/config.h")
configure_file("${CMAKE_CURRENT_LIST_DIR}/cmake/Version.inc.in" "${_nwb_clang_gen_inc}/clang/Basic/Version.inc" @ONLY)
file(CONFIGURE OUTPUT "${_nwb_clang_gen_basic_lib}/VCSVersion.inc" CONTENT "#define LLVM_REVISION \"5b10d2d74929eae78912771222c9855af97496f4\"\n#define LLVM_REPOSITORY \"https://github.com/llvm/llvm-project.git\"\n" @ONLY)

include("${CMAKE_CURRENT_LIST_DIR}/sources.cmake")

add_library(nwb_vendor_libclang STATIC ${NWB_LIBCLANG_SOURCES})
add_library(nwb::libclang ALIAS nwb_vendor_libclang)

set_target_properties(nwb_vendor_libclang PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS OFF
)

target_include_directories(nwb_vendor_libclang
    PRIVATE
        "${_nwb_clang_gen_inc}"
        "${_nwb_clang_gen_basic_lib}"
        "${CMAKE_CURRENT_LIST_DIR}/include"
        "${CMAKE_CURRENT_LIST_DIR}/lib"
        "${CMAKE_CURRENT_LIST_DIR}/lib/AST"
        "${CMAKE_CURRENT_LIST_DIR}/lib/Basic"
        "${CMAKE_CURRENT_LIST_DIR}/lib/Driver"
        "${CMAKE_CURRENT_LIST_DIR}/lib/ScalableStaticAnalysis/Analyses"
        "${CMAKE_CURRENT_LIST_DIR}/lib/Sema"
        "${CMAKE_CURRENT_LIST_DIR}/tools/libclang"
)

target_compile_definitions(nwb_vendor_libclang
    PUBLIC
        CINDEX_NO_EXPORTS
    PRIVATE
        CLANG_BUILD_STATIC
        __STDC_CONSTANT_MACROS
        __STDC_FORMAT_MACROS
        __STDC_LIMIT_MACROS
)

if(NWB_COMPILER_FRONTEND_MSVC)
    target_compile_options(nwb_vendor_libclang PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/GR->)
else()
    target_compile_options(nwb_vendor_libclang PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti> $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>)
endif()

target_link_libraries(nwb_vendor_libclang
    PUBLIC
        nwb::libclang_headers
    PRIVATE
        nwb::llvm
)

if(WIN32)
    target_link_libraries(nwb_vendor_libclang PRIVATE version)
endif()

nwb_disable_vendor_warnings(nwb_vendor_libclang)
nwb_apply_debug_symbols(nwb_vendor_libclang)
nwb_skip_unity_build_sources(
    "${CMAKE_CURRENT_LIST_DIR}/lib/APINotes/APINotesWriter.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/APINotes/APINotesYAMLCompiler.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/AST/CommentLexer.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/AST/CommentParser.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/AST/CommentSema.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/AST/MicrosoftMangle.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Analysis/IntervalPartition.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Analysis/LifetimeSafety/LoanPropagation.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Analysis/LifetimeSafety/MovedLoans.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Basic/LangStandards.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Basic/Targets/AArch64.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Basic/Targets/AMDGPU.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Basic/Targets/ARM.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Basic/Targets/BPF.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Basic/Targets/DirectX.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Basic/Targets/Hexagon.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Basic/Targets/LoongArch.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Basic/Targets/Mips.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Basic/Targets/NVPTX.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Basic/Targets/PPC.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Basic/Targets/SPIR.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Basic/Targets/SystemZ.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Basic/Targets/VE.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Basic/Targets/WebAssembly.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Basic/Targets/XCore.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/DependencyScanning/ModuleDepCollector.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Driver/Compilation.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Driver/ToolChain.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Driver/ToolChains/CSKYToolChain.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Driver/ToolChains/Clang.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Driver/ToolChains/CommonArgs.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Driver/ToolChains/Cuda.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Driver/ToolChains/Cygwin.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Driver/ToolChains/Darwin.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Driver/ToolChains/FreeBSD.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Driver/ToolChains/Fuchsia.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Driver/ToolChains/Gnu.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Driver/ToolChains/Haiku.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Driver/ToolChains/Hurd.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Driver/ToolChains/Linux.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Driver/ToolChains/MSVC.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Driver/ToolChains/Managarm.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Driver/ToolChains/MinGW.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Driver/ToolChains/NetBSD.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Driver/ToolChains/OpenBSD.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Driver/ToolChains/PS4CPU.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Driver/ToolChains/Serenity.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Driver/ToolChains/Solaris.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Driver/ToolChains/UEFI.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Driver/ToolChains/WebAssembly.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Edit/RewriteObjCFoundationAPI.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/ExtractAPI/Serialization/SymbolGraphSerializer.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Frontend/CompilerInvocation.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Lex/Lexer.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Lex/MacroArgs.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Parse/ParseOpenMP.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/ScalableStaticAnalysis/Analyses/PointerFlow/PointerFlowExtractor.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/ScalableStaticAnalysis/Analyses/UnsafeBufferUsage/UnsafeBufferUsageExtractor.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/ScalableStaticAnalysis/Analyses/UnsafeBufferUsage/UnsafeBufferUsageFormat.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Sema/SemaSYCL.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Sema/SemaTemplateInstantiate.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Serialization/ASTWriter.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Tooling/CompilationDatabase.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Tooling/DependencyScanningTool.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Tooling/Tooling.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/UnifiedSymbolResolution/USRGeneration.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/tools/libclang/CIndexer.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/tools/libclang/CXComment.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/tools/libclang/CXExtractAPI.cpp"
)

file(TO_CMAKE_PATH "${CMAKE_CURRENT_LIST_DIR}/resource" _nwb_libclang_resource_dir)
set_property(GLOBAL PROPERTY NWB_LIBCLANG_RESOURCE_DIR "${_nwb_libclang_resource_dir}")

# Provide the bundled Clang resource headers directory to libclang consumers.
function(nwb_apply_libclang_runtime target)
    get_property(_resource_dir GLOBAL PROPERTY NWB_LIBCLANG_RESOURCE_DIR)
    if(_resource_dir)
        target_compile_definitions(${target} PRIVATE "NWB_LIBCLANG_DEFAULT_RESOURCE_DIR=\"${_resource_dir}\"")
    endif()
endfunction()
