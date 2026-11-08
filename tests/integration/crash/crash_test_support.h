// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "crash_test_paths.h"

#include <tests/common/filesystem_helpers.h>
#include <tests/common/test_context.h>
#include <tests/integration/crash/name_cases.adhd.h>

#include <core/crash/package_internal.h>

#include <global/filesystem/operations.h>
#include <global/filesystem/directory_iterator.h>
#include <global/process.h>
#include <global/process_execution.h>
#include <global/thread.h>
#include <global/diagnostics.h>
#include <global/exception.h>
#include <global/scope_exit.h>
#include <global/termination.h>

#include <gtest/gtest.h>


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

inline constexpr Name s_OperationScratch = NWB::Tests::CrashCases::s_CrashTestNameTestsIntegrationCrashOperationScratch;
inline constexpr Name s_InstallArena = NWB::Tests::CrashCases::s_CrashTestNameTestsIntegrationCrashInstall;
inline constexpr Name s_SignalChildInstallArena = NWB::Tests::CrashCases::s_CrashTestNameTestsIntegrationCrashSignalChildInstall;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline CrashTestPath BucketDirectory(NWB::Core::Alloc::GlobalArena& arena, const AStringView testGroup, const AStringView bucketName){
    return NWB::Tests::CrashTestPaths::SpoolDirectory(arena, testGroup) / bucketName;
}

[[nodiscard]] inline CrashTestPath PackageDirectory(NWB::Core::Alloc::GlobalArena& arena, const AStringView testGroup, const AStringView bucketName, const AStringView packageName){
    return BucketDirectory(arena, testGroup, bucketName) / packageName;
}

[[nodiscard]] b8 CreatePackageDirectory(NWB::Core::Alloc::GlobalArena& arena, AStringView testGroup, AStringView bucketName, AStringView packageName);

template<usize N>
inline void CopyPathText(const CrashTestPath& path, NWB::Core::Alloc::GlobalArena& arena, Array<char, N>& outText){
    const AString<NWB::Core::Alloc::GlobalArena> text = PathToString<char>(arena, path);
    CopyFixedBuffer(outText, AStringView(text.data(), text.size()));
}

template<usize N>
inline void BuildCrashIdForProcess(const u32 processId, const u64 sequence, Array<char, N>& outCrashId)noexcept{
    CopyFixedBuffer(outCrashId, AStringView(NWB::Core::Crash::PackageNames::s_CrashIdPrefix));
    AppendUnsignedToFixedBuffer(outCrashId, static_cast<u64>(processId));
    AppendFixedBuffer(outCrashId, "-");
    AppendUnsignedToFixedBuffer(outCrashId, sequence);
}

[[nodiscard]] NWB::Core::Crash::CrashDetail::PlatformKind::Enum CurrentCrashPlatformKind()noexcept;

void FillPackageRequest(const CrashTestPath& spoolDirectory, AStringView crashId, NWB::Core::Alloc::GlobalArena& arena, NWB::Core::Crash::CrashDetail::CrashRequest& request);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_WINDOWS) || (defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID))
extern u32 s_NativeLifecycleDiagnostics;
extern u32 s_UnlockedLifecycleDiagnostics;
extern Array<char, 192u> s_LastLifecycleDiagnostic;
extern Array<char, 32u> s_LastLifecycleOperation;

void ObserveNativeLifecycle(const DiagnosticEventRecord& record)noexcept;
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

