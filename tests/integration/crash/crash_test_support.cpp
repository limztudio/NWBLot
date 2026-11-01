// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "crash_test_support.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CrashTestSupport{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 CreatePackageDirectory(NWB::Core::Alloc::GlobalArena& arena, const AStringView testGroup, const AStringView bucketName, const AStringView packageName){
    const CrashTestPath packageDirectory = PackageDirectory(arena, testGroup, bucketName, packageName);
    if(!EnsureDirectories(packageDirectory))
        return false;

    return WriteTextFile(packageDirectory / "marker.txt", AStringView("package"));
}

NWB::Core::Crash::CrashDetail::PlatformKind::Enum CurrentCrashPlatformKind()noexcept{
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

void FillPackageRequest(const CrashTestPath& spoolDirectory, const AStringView crashId, NWB::Core::Alloc::GlobalArena& arena, NWB::Core::Crash::CrashDetail::CrashRequest& request){
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
u32 s_NativeLifecycleDiagnostics = 0u;
u32 s_UnlockedLifecycleDiagnostics = 0u;
Array<char, 192u> s_LastLifecycleDiagnostic = {};
Array<char, 32u> s_LastLifecycleOperation = {};

void ObserveNativeLifecycle(const DiagnosticEventRecord& record)noexcept{
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

