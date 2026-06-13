// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "internal.h"

NWB_CRASH_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Detail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CrashState g_State;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static PlatformKind::Enum CurrentPlatformKind()noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
    return PlatformKind::Windows;
#elif defined(NWB_PLATFORM_ANDROID)
    return PlatformKind::Android;
#elif defined(NWB_PLATFORM_LINUX)
    return PlatformKind::Linux;
#else
    return PlatformKind::Unknown;
#endif
}

const char* PlatformKindName(const u32 platform)noexcept{
    switch(platform){
    case PlatformKind::Windows:
        return "windows";
    case PlatformKind::Linux:
        return "linux";
    case PlatformKind::Android:
        return "android";
    default:
        return "unknown";
    }
}

const char* ReasonKindName(const u32 reasonKind)noexcept{
    switch(reasonKind){
    case CrashReasonKind::WindowsException:
        return "windows_exception";
    case CrashReasonKind::PosixSignal:
        return "signal";
    case CrashReasonKind::Terminate:
        return "terminate";
    case CrashReasonKind::ManualDump:
        return "manual_dump";
    default:
        return "unknown";
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void BuildCrashId(CrashRequest& outRequest, const u64 sequence)noexcept{
    CopyFixedBuffer(outRequest.crashId, "crash-");
    AppendUnsignedToFixedBuffer(outRequest.crashId, outRequest.processId);
    AppendFixedBuffer(outRequest.crashId, "-");
    AppendUnsignedToFixedBuffer(outRequest.crashId, sequence);
}

void SnapshotCrashState(CrashRequest& outRequest, const CrashReasonKind::Enum reasonKind, const u32 reasonCode)noexcept{
    outRequest = CrashRequest{};
    outRequest.platform = CurrentPlatformKind();
    outRequest.reasonKind = reasonKind;
    outRequest.reasonCode = reasonCode;
    outRequest.processId = CurrentProcessId();
    outRequest.threadId = CurrentThreadId();
    outRequest.dumpDetailMode = static_cast<u32>(g_State.dumpDetailMode);
    outRequest.enableGpuDumps = g_State.enableGpuDumps ? 1u : 0u;

    const u64 sequence = g_State.crashSequence.fetch_add(1u, MemoryOrder::relaxed);
    BuildCrashId(outRequest, sequence);

    CopyFixedBuffer(outRequest.applicationName, g_State.applicationName);
    CopyFixedBuffer(outRequest.versionText, g_State.versionText);
    CopyFixedBuffer(outRequest.buildId, g_State.buildId);
    CopyFixedBuffer(outRequest.abi, CurrentAbiName());
    CopyFixedBuffer(outRequest.spoolDirectory, g_State.spoolDirectoryText);
    CopyFixedBuffer(outRequest.logServerUrl, g_State.logServerUrl);

    for(usize i = 0u; i < s_MaxMetadata; ++i){
        if(!g_State.metadata[i].used)
            continue;
        if(outRequest.metadataCount >= s_MaxMetadata)
            break;
        outRequest.metadata[outRequest.metadataCount++] = g_State.metadata[i];
    }

    const usize begin = g_State.nextBreadcrumb >= s_MaxBreadcrumbs ? g_State.nextBreadcrumb - s_MaxBreadcrumbs : 0u;
    const usize end = g_State.nextBreadcrumb;
    for(usize i = begin; i < end; ++i){
        const FixedBreadcrumb& breadcrumb = g_State.breadcrumbs[i % s_MaxBreadcrumbs];
        if(!breadcrumb.used)
            continue;
        if(outRequest.breadcrumbCount >= s_MaxBreadcrumbs)
            break;
        outRequest.breadcrumbs[outRequest.breadcrumbCount++] = breadcrumb;
    }
}

bool RequestCrashDump(const CrashReasonKind::Enum reasonKind, const u32 reasonCode, const CrashDumpRequestOptions& options){
#if defined(NWB_PLATFORM_ANDROID)
    if(options.writePackageInProcess){
        CrashRequest request;
        SnapshotCrashState(request, reasonKind, reasonCode);
        if(!WriteCrashPackage(request))
            return false;

        if(options.uploadImmediately)
            static_cast<void>(UploadCrashPackage(request));
        return true;
    }
#endif

    return RequestCrashHandler(reasonKind, reasonCode, options.waitMilliseconds);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

