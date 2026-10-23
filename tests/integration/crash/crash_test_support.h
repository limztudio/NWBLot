// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <tests/common/filesystem_helpers.h>
#include <tests/common/test_context.h>

#include <gtest/gtest.h>

#include <core/crash/package_internal.h>

#include <global/filesystem/operations.h>
#include <global/filesystem/directory_iterator.h>
#include <global/thread.h>
#include <global/diagnostics.h>
#include <global/exception.h>
#include <global/scope_exit.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Shared crash-package test paths, request builders, and native-lifecycle observation.
namespace CrashTestSupport{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_PackageReadyTimeoutMS = 3000u;
inline constexpr AStringView s_SpoolDirectory = "spool";
inline constexpr AStringView s_ApplicationVersion = "1";

inline constexpr AStringView s_Crash001 = "crash-001";
inline constexpr AStringView s_Crash002 = "crash-002";
inline constexpr AStringView s_Crash003 = "crash-003";
inline constexpr AStringView s_CrashTests = "crash_tests";
inline constexpr AStringView s_Test = "test";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestArena = NWB::Tests::TestArena<struct CrashTestsTag>;
using CrashTestPath = Path<NWB::Core::Alloc::GlobalArena>;
using NWB::Tests::TextFileContains;
using NWB::Tests::WaitForDirectory;
namespace CrashNames = NWB::Core::Crash::PackageNames;

inline constexpr Name s_OperationScratch("tests/integration/crash/operation_scratch");
inline constexpr Name s_InstallArena("tests/integration/crash/install");
inline constexpr Name s_SignalChildInstallArena("tests/integration/crash/signal_child_install");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline CrashTestPath CrashRootDirectory(NWB::Core::Alloc::GlobalArena& arena){
    const auto executableDirectory = GetExecutableDirectory(arena);
    if(executableDirectory)
        return *executableDirectory / CrashNames::s_DefaultRootDirectoryName;

    return CrashTestPath(arena, CrashNames::s_DefaultRootDirectoryName);
}

[[nodiscard]] inline CrashTestPath TestRootDirectory(NWB::Core::Alloc::GlobalArena& arena){
    return CrashRootDirectory(arena) / "test_storage";
}

[[nodiscard]] inline CrashTestPath TestCaseDirectory(NWB::Core::Alloc::GlobalArena& arena, const AStringView testGroup){
    return TestRootDirectory(arena) / testGroup;
}

[[nodiscard]] inline CrashTestPath SpoolDirectory(NWB::Core::Alloc::GlobalArena& arena, const AStringView testGroup){
    return TestCaseDirectory(arena, testGroup) / s_SpoolDirectory;
}

[[nodiscard]] inline CrashTestPath BucketDirectory(NWB::Core::Alloc::GlobalArena& arena, const AStringView testGroup, const AStringView bucketName){
    return SpoolDirectory(arena, testGroup) / bucketName;
}

[[nodiscard]] inline CrashTestPath PackageDirectory(NWB::Core::Alloc::GlobalArena& arena, const AStringView testGroup, const AStringView bucketName, const AStringView packageName){
    return BucketDirectory(arena, testGroup, bucketName) / packageName;
}

inline void RemoveTestArtifacts(NWB::Core::Alloc::GlobalArena& arena, const AStringView testGroup){
    EXPECT_TRUE(RemoveAllIfExists(TestCaseDirectory(arena, testGroup)));
}

[[nodiscard]] inline bool CreatePackageDirectory(NWB::Core::Alloc::GlobalArena& arena, const AStringView testGroup, const AStringView bucketName, const AStringView packageName){
    const CrashTestPath packageDirectory = PackageDirectory(arena, testGroup, bucketName, packageName);
    if(!EnsureDirectories(packageDirectory))
        return false;

    return WriteTextFile(packageDirectory / "marker.txt", AStringView("package"));
}

template<usize N>
inline void CopyPathText(const CrashTestPath& path, NWB::Core::Alloc::GlobalArena& arena, char (&outText)[N]){
    const AString<NWB::Core::Alloc::GlobalArena> text = PathToString<char>(arena, path);
    CopyFixedBuffer(outText, AStringView(text.data(), text.size()));
}

template<usize N>
inline void BuildCrashIdForProcess(char (&outCrashId)[N], const u32 processId, const u64 sequence)noexcept{
    CopyFixedBuffer(outCrashId, AStringView(CrashNames::s_CrashIdPrefix));
    AppendUnsignedToFixedBuffer(outCrashId, static_cast<u64>(processId));
    AppendFixedBuffer(outCrashId, "-");
    AppendUnsignedToFixedBuffer(outCrashId, sequence);
}

[[nodiscard]] inline NWB::Core::Crash::CrashDetail::PlatformKind::Enum CurrentCrashPlatformKind()noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
    return NWB::Core::Crash::CrashDetail::PlatformKind::Windows;
#elif defined(NWB_PLATFORM_ANDROID)
    return NWB::Core::Crash::CrashDetail::PlatformKind::Android;
#elif defined(NWB_PLATFORM_LINUX)
    return NWB::Core::Crash::CrashDetail::PlatformKind::Linux;
#else
    return NWB::Core::Crash::CrashDetail::PlatformKind::Unknown;
#endif
}

inline void FillPackageRequest(const CrashTestPath& spoolDirectory, const AStringView crashId, NWB::Core::Alloc::GlobalArena& arena, NWB::Core::Crash::CrashDetail::CrashRequest& request){
    request = NWB::Core::Crash::CrashDetail::CrashRequest{};
    request.platform = CurrentCrashPlatformKind();
    request.reasonKind = NWB::Core::Crash::CrashDetail::CrashReasonKind::ManualDump;
    request.processId = CurrentProcessId();
    request.threadId = CurrentThreadId();
    CopyFixedBuffer(request.crashId, crashId);
    CopyFixedBuffer(request.applicationName, AStringView(s_CrashTests));
    CopyFixedBuffer(request.versionText, AStringView(s_ApplicationVersion));
    CopyFixedBuffer(request.buildId, AStringView(s_Test));
    CopyFixedBuffer(request.abi, CurrentAbiName());
    CopyPathText(spoolDirectory, arena, request.spoolDirectory);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_WINDOWS) || (defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID))
inline u32 s_NativeLifecycleDiagnostics = 0u;
inline u32 s_UnlockedLifecycleDiagnostics = 0u;
inline char s_LastLifecycleDiagnostic[192u] = {};
inline char s_LastLifecycleOperation[32u] = {};

inline void ObserveNativeLifecycle(const DiagnosticEventRecord& record)noexcept{
    if(record.category != AStringView("crash_native_lifecycle"))
        return;
    ++s_NativeLifecycleDiagnostics;
    CopyFixedBuffer(s_LastLifecycleDiagnostic, record.message);
    CopyFixedBuffer(s_LastLifecycleOperation, record.expression);
    if(NWB::Core::Crash::CrashDetail::g_State.mutex.try_lock()){
        ++s_UnlockedLifecycleDiagnostics;
        NWB::Core::Crash::CrashDetail::g_State.mutex.unlock();
    }
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

