if(TARGET nwb::llvm)
    return()
endif()

include(CheckIncludeFile)
include(CheckSymbolExists)
include(CheckLibraryExists)
include(CheckCXXSourceCompiles)
include(CheckCSourceCompiles)
include(CheckStructHasMember)
include(CMakePushCheckState)

find_package(Threads REQUIRED)

set(PACKAGE_NAME "LLVM")
set(PACKAGE_VERSION "23.1.5")
set(PACKAGE_STRING "LLVM 23.1.5")
set(PACKAGE_BUGREPORT "https://github.com/llvm/llvm-project/issues/")
set(BUG_REPORT_URL "https://github.com/llvm/llvm-project/issues/")

set(LLVM_VERSION_MAJOR 23)
set(LLVM_VERSION_MINOR 1)
set(LLVM_VERSION_PATCH 5)
set(LLVM_DEFAULT_TARGET_TRIPLE "")
set(LLVM_ENABLE_THREADS 1)
set(LLVM_HAS_ATOMICS 1)
set(LLVM_FORCE_ENABLE_STATS 0)
set(LLVM_USE_INTEL_JITEVENTS 0)
set(LLVM_USE_OPROFILE 0)
set(LLVM_USE_PERF 0)
set(LLVM_ENABLE_ZLIB 0)
set(LLVM_ENABLE_ZSTD 0)
set(LLVM_UNREACHABLE_OPTIMIZE 1)
set(LLVM_ENABLE_IO_SANDBOX 0)
set(LLVM_ENABLE_DIA_SDK 0)
set(LLVM_ENABLE_TELEMETRY 1)
set(LLVM_ENABLE_DEBUGLOC_TRACKING_COVERAGE 0)
set(LLVM_ENABLE_DEBUGLOC_TRACKING_ORIGIN 0)
set(LLVM_ENABLE_ONDISK_CAS 1)
set(LLVM_ENABLE_ABI_BREAKING_CHECKS 0)
set(LLVM_ENABLE_REVERSE_ITERATION 0)
set(ENABLE_BACKTRACES 1)
set(ENABLE_CRASH_OVERRIDES 1)
set(LLVM_ENABLE_CRASH_DUMPS 0)
set(LLVM_WINDOWS_PREFER_FORWARD_SLASH 0)
set(HAVE_CRASHREPORTER_INFO 0)
set(HAVE_ICU 0)
set(HAVE_WINDOWS_ICU 0)
set(HAVE_ICONV 0)
set(LLVM_VERSION_PRINTER_SHOW_HOST_TARGET_INFO 1)
set(LLVM_VERSION_PRINTER_SHOW_BUILD_CONFIG 1)
set(LLVM_GISEL_COV_ENABLED 0)
set(LLVM_SUPPORT_XCODE_SIGNPOSTS 0)
set(LLVM_BUILD_STATIC 1)

if(WIN32)
    set(LLVM_ON_UNIX 0)
    set(LTDL_SHLIB_EXT ".dll")
    set(LLVM_PLUGIN_EXT ".dll")
    if(NWB_OUTPUT_ARCH STREQUAL "arm64")
        set(LLVM_HOST_TRIPLE "aarch64-pc-windows-msvc")
        set(LLVM_NATIVE_ARCH "AArch64")
    else()
        set(LLVM_HOST_TRIPLE "x86_64-pc-windows-msvc")
        set(LLVM_NATIVE_ARCH "X86")
    endif()
else()
    set(LLVM_ON_UNIX 1)
    set(LTDL_SHLIB_EXT ".so")
    set(LLVM_PLUGIN_EXT ".so")
    if(NWB_OUTPUT_ARCH STREQUAL "arm64")
        set(LLVM_HOST_TRIPLE "aarch64-unknown-linux-gnu")
        set(LLVM_NATIVE_ARCH "AArch64")
    else()
        set(LLVM_HOST_TRIPLE "x86_64-unknown-linux-gnu")
        set(LLVM_NATIVE_ARCH "X86")
    endif()
endif()

# Target backend registration macros are intentionally empty for static AST/libclang usage.
set(LLVM_ENUM_TARGETS "")
set(LLVM_ENUM_ASM_PRINTERS "")
set(LLVM_ENUM_ASM_PARSERS "")
set(LLVM_ENUM_DISASSEMBLERS "")
set(LLVM_ENUM_TARGETMCAS "")
set(LLVM_ENUM_EXEGESIS "")
foreach(_nwb_llvm_target_flag
    AARCH64 AMDGPU ARC ARM AVR BPF CSKY DIRECTX HEXAGON LANAI LOONGARCH
    M68K MIPS MSP430 NVPTX POWERPC RISCV SPARC SPIRV SYSTEMZ VE
    WEBASSEMBLY X86 XCORE XTENSA
)
    set(LLVM_HAS_${_nwb_llvm_target_flag}_TARGET 0)
endforeach()

# Undef init macros since no LLVM CodeGen targets are linked.
set(_nwb_llvm_saved_native_arch "${LLVM_NATIVE_ARCH}")

cmake_push_check_state(RESET)
check_symbol_exists(FE_ALL_EXCEPT "fenv.h" HAVE_DECL_FE_ALL_EXCEPT)
check_symbol_exists(FE_INEXACT "fenv.h" HAVE_DECL_FE_INEXACT)
check_symbol_exists(strerror_s "string.h" HAVE_DECL_STRERROR_S)
check_cxx_source_compiles("int main(){ void *p = __builtin_thread_pointer(); (void)p; return 0; }" HAVE_BUILTIN_THREAD_POINTER)
check_symbol_exists(arc4random "stdlib.h" HAVE_DECL_ARC4RANDOM)

set(BACKTRACE_HEADER "backtrace.h")
if(WIN32)
    check_symbol_exists(_chsize_s "io.h" HAVE__CHSIZE_S)
    check_c_source_compiles("void __chkstk(void); int main(){ return 0; }" HAVE___CHKSTK)
    set(HAVE_PTHREAD_H 0)
    set(HAVE_SYS_MMAN_H 0)
    set(HAVE_SYSEXITS_H 0)
    set(HAVE_UNISTD_H 0)
    set(HAVE_SYS_IOCTL_H 0)
    set(HAVE_DLOPEN 0)
    set(HAVE_FUTIMENS 0)
    set(HAVE_FUTIMES 0)
    set(HAVE_GETAUXVAL 0)
    set(HAVE_GETPAGESIZE 0)
    set(HAVE_GETRUSAGE 0)
    set(HAVE_ISATTY 0)
    set(HAVE_LIBPTHREAD 0)
    set(HAVE_MALLINFO 0)
    set(HAVE_MALLINFO2 0)
    set(HAVE_POSIX_SPAWN 0)
    set(HAVE_PREAD 0)
    set(HAVE_PTHREAD_GETNAME_NP 0)
    set(HAVE_PTHREAD_MUTEX_LOCK 0)
    set(HAVE_PTHREAD_RWLOCK_INIT 0)
    set(HAVE_PTHREAD_SETNAME_NP 0)
    set(HAVE_SBRK 0)
    set(HAVE_SETENV 0)
    set(HAVE_SIGALTSTACK 0)
    set(HAVE_STRERROR_R 0)
    set(HAVE_STRUCT_STAT_ST_MTIMESPEC_TV_NSEC 0)
    set(HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC 0)
    set(HAVE_SYSCONF 0)
    set(HAVE_BACKTRACE 0)
    set(HAVE__UNWIND_BACKTRACE 0)
else()
    check_include_file(pthread.h HAVE_PTHREAD_H)
    check_include_file(sys/mman.h HAVE_SYS_MMAN_H)
    check_include_file(sysexits.h HAVE_SYSEXITS_H)
    check_include_file(unistd.h HAVE_UNISTD_H)
    check_include_file(sys/ioctl.h HAVE_SYS_IOCTL_H)
    check_include_file(execinfo.h _nwb_have_execinfo_h)
    if(_nwb_have_execinfo_h)
        check_symbol_exists(backtrace "execinfo.h" HAVE_BACKTRACE)
        if(HAVE_BACKTRACE)
            set(BACKTRACE_HEADER "execinfo.h")
        endif()
    endif()
    check_symbol_exists(_Unwind_Backtrace "unwind.h" HAVE__UNWIND_BACKTRACE)
    list(APPEND CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
    check_symbol_exists(dlopen dlfcn.h HAVE_DLOPEN)
    check_symbol_exists(futimens sys/stat.h HAVE_FUTIMENS)
    check_symbol_exists(futimes sys/time.h HAVE_FUTIMES)
    check_symbol_exists(getauxval sys/auxv.h HAVE_GETAUXVAL)
    check_symbol_exists(getpagesize unistd.h HAVE_GETPAGESIZE)
    check_symbol_exists(getrusage sys/resource.h HAVE_GETRUSAGE)
    check_symbol_exists(isatty unistd.h HAVE_ISATTY)
    check_symbol_exists(mallinfo malloc.h HAVE_MALLINFO)
    check_symbol_exists(mallinfo2 malloc.h HAVE_MALLINFO2)
    check_symbol_exists(posix_spawn spawn.h HAVE_POSIX_SPAWN)
    check_symbol_exists(pread unistd.h HAVE_PREAD)
    check_symbol_exists(sbrk unistd.h HAVE_SBRK)
    check_symbol_exists(setenv stdlib.h HAVE_SETENV)
    check_symbol_exists(sigaltstack signal.h HAVE_SIGALTSTACK)
    check_symbol_exists(strerror_r string.h HAVE_STRERROR_R)
    check_symbol_exists(sysconf unistd.h HAVE_SYSCONF)
    check_struct_has_member("struct stat" st_mtim.tv_nsec "sys/types.h;sys/stat.h" HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC)
    check_struct_has_member("struct stat" st_mtimespec.tv_nsec "sys/types.h;sys/stat.h" HAVE_STRUCT_STAT_ST_MTIMESPEC_TV_NSEC)
    if(HAVE_PTHREAD_H)
        set(HAVE_LIBPTHREAD 1)
        list(APPEND CMAKE_REQUIRED_LIBRARIES Threads::Threads)
        check_symbol_exists(pthread_mutex_lock pthread.h HAVE_PTHREAD_MUTEX_LOCK)
        check_symbol_exists(pthread_rwlock_init pthread.h HAVE_PTHREAD_RWLOCK_INIT)
        check_symbol_exists(pthread_getname_np pthread.h HAVE_PTHREAD_GETNAME_NP)
        check_symbol_exists(pthread_setname_np pthread.h HAVE_PTHREAD_SETNAME_NP)
    endif()
    check_library_exists(rt clock_gettime "" NWB_LLVM_HAVE_LIBRT)
endif()
cmake_pop_check_state()

set(_nwb_llvm_gen_inc "${CMAKE_CURRENT_BINARY_DIR}/include")
file(MAKE_DIRECTORY "${_nwb_llvm_gen_inc}/llvm/Config" "${_nwb_llvm_gen_inc}/llvm/Support")

configure_file("${CMAKE_CURRENT_LIST_DIR}/cmake/Config/Targets.def.in" "${_nwb_llvm_gen_inc}/llvm/Config/Targets.def" @ONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/cmake/Config/AsmPrinters.def.in" "${_nwb_llvm_gen_inc}/llvm/Config/AsmPrinters.def" @ONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/cmake/Config/AsmParsers.def.in" "${_nwb_llvm_gen_inc}/llvm/Config/AsmParsers.def" @ONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/cmake/Config/Disassemblers.def.in" "${_nwb_llvm_gen_inc}/llvm/Config/Disassemblers.def" @ONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/cmake/Config/TargetMCAs.def.in" "${_nwb_llvm_gen_inc}/llvm/Config/TargetMCAs.def" @ONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/cmake/Config/TargetExegesis.def.in" "${_nwb_llvm_gen_inc}/llvm/Config/TargetExegesis.def" @ONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/cmake/Config/Targets.h.cmake" "${_nwb_llvm_gen_inc}/llvm/Config/Targets.h")
configure_file("${CMAKE_CURRENT_LIST_DIR}/cmake/Config/llvm-config.h.cmake" "${_nwb_llvm_gen_inc}/llvm/Config/llvm-config.h")
configure_file("${CMAKE_CURRENT_LIST_DIR}/cmake/Config/config.h.cmake" "${_nwb_llvm_gen_inc}/llvm/Config/config.h")
configure_file("${CMAKE_CURRENT_LIST_DIR}/cmake/Config/abi-breaking.h.cmake" "${_nwb_llvm_gen_inc}/llvm/Config/abi-breaking.h")

file(CONFIGURE OUTPUT "${_nwb_llvm_gen_inc}/llvm/Support/Extension.def" CONTENT "//extension handlers\n#undef HANDLE_EXTENSION\n" @ONLY)
file(CONFIGURE OUTPUT "${_nwb_llvm_gen_inc}/llvm/Support/VCSRevision.h" CONTENT "#define LLVM_REVISION \"392dfff97d29b59d77e5cac6d4b6ef2585fe7117\"\n#define LLVM_REPOSITORY \"https://github.com/llvm/llvm-project.git\"\n" @ONLY)

include("${CMAKE_CURRENT_LIST_DIR}/sources.cmake")

add_library(nwb_vendor_llvm STATIC ${NWB_LLVM_SOURCES})
add_library(nwb::llvm ALIAS nwb_vendor_llvm)

set_target_properties(nwb_vendor_llvm PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS OFF
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS OFF
)

target_include_directories(nwb_vendor_llvm SYSTEM PUBLIC
    "${_nwb_llvm_gen_inc}"
    "${CMAKE_CURRENT_LIST_DIR}/include"
)

target_compile_definitions(nwb_vendor_llvm
    PUBLIC
        LLVM_BUILD_STATIC
    PRIVATE
        __STDC_CONSTANT_MACROS
        __STDC_FORMAT_MACROS
        __STDC_LIMIT_MACROS
        BLAKE3_NO_AVX512
        BLAKE3_NO_AVX2
        BLAKE3_NO_SSE41
        BLAKE3_NO_SSE2
)

if(NWB_COMPILER_FRONTEND_MSVC)
    target_compile_options(nwb_vendor_llvm PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/GR->)
    set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/lib/Analysis/ConstantFolding.cpp" PROPERTIES COMPILE_OPTIONS "/fp:except")
else()
    target_compile_options(nwb_vendor_llvm PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti> $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>)
    set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/lib/Analysis/ConstantFolding.cpp" PROPERTIES COMPILE_OPTIONS "-ftrapping-math")
endif()

target_link_libraries(nwb_vendor_llvm
    PRIVATE
        nwb::siphash
        nwb::llvm_libc
)

if(WIN32)
    target_link_libraries(nwb_vendor_llvm PUBLIC psapi shell32 ole32 uuid advapi32 ws2_32 ntdll)
else()
    target_link_libraries(nwb_vendor_llvm PUBLIC Threads::Threads ${CMAKE_DL_LIBS} m)
    if(NWB_LLVM_HAVE_LIBRT)
        target_link_libraries(nwb_vendor_llvm PUBLIC rt)
    endif()
endif()

nwb_disable_vendor_warnings(nwb_vendor_llvm)
nwb_apply_debug_symbols(nwb_vendor_llvm)
nwb_skip_unity_build_sources(
    "${CMAKE_CURRENT_LIST_DIR}/lib/Bitcode/Reader/MetadataLoader.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/DebugInfo/CodeView/MergingTypeTableBuilder.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/DebugInfo/CodeView/TypeRecordMapping.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Demangle/DLangDemangle.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Demangle/MicrosoftDemangle.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Demangle/MicrosoftDemangleNodes.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Demangle/RustDemangle.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Frontend/HLSL/RootSignatureMetadata.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/MC/MCAsmInfoWasm.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/MC/WinCOFFObjectWriter.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Object/MachOUniversal.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Object/ModuleSymbolTable.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Object/OffloadBinary.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/ObjectYAML/CodeViewYAMLTypes.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/ProfileData/InstrProfReader.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/ProfileData/InstrProfWriter.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/ProfileData/MemProfSummaryBuilder.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Remarks/Remark.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Support/Atomic.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Support/BalancedPartitioning.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Support/COM.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Support/CrashRecoveryContext.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Support/MSP430AttributeParser.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Support/ManagedStatic.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Support/RewriteRope.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/TargetParser/AMDGPUTargetParser.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/TargetParser/CSKYTargetParser.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/TargetParser/X86TargetParser.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/TextAPI/TextStubV5.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Transforms/Scalar/BDCE.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Transforms/Scalar/ConstraintElimination.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Transforms/Scalar/GVNSink.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Transforms/Scalar/LoopInterchange.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Transforms/Utils/Local.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Transforms/Utils/LowerGlobalDtors.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Transforms/Utils/LowerInvoke.cpp"
)
