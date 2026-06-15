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
        return s_ManualDumpCategory;
    default:
        return "unknown";
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void BuildCrashId(CrashRequest& outRequest, const u64 sequence)noexcept{
    CopyFixedBuffer(outRequest.crashId, PackageNames::s_CrashIdPrefix);
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
    outRequest.spoolRetention = g_State.spoolRetention;

    const u64 sequence = g_State.crashSequence.fetch_add(1u, MemoryOrder::relaxed);
    BuildCrashId(outRequest, sequence);

    CopyFixedBuffer(outRequest.applicationName, g_State.applicationName);
    CopyFixedBuffer(outRequest.versionText, g_State.versionText);
    CopyFixedBuffer(outRequest.buildId, g_State.buildId);
    CopyFixedBuffer(outRequest.abi, CurrentAbiName());
    CopyFixedBuffer(outRequest.spoolDirectory, g_State.spoolDirectoryText);
    CopyFixedBuffer(outRequest.logServerUrl, g_State.logServerUrl);
    CopyFixedBuffer(outRequest.crashUploadToken, g_State.crashUploadToken);

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

CrashDumpResult RequestCrashDump(const CrashReasonKind::Enum reasonKind, const u32 reasonCode, const CrashDumpRequestOptions& options){
    CrashRequest request;
    SnapshotCrashState(request, reasonKind, reasonCode);
    request.uploadPolicy = options.uploadAfterWrite ? CrashUploadPolicy::ImmediateAfterWrite : CrashUploadPolicy::None;
    request.exceptionPointers = options.exceptionPointers;
    request.faultAddress = options.faultAddress;
    request.instructionPointer = options.instructionPointer;
    request.stackPointer = options.stackPointer;
    request.framePointer = options.framePointer;
    request.triggerLine = options.triggerLine;
    const u32 availableCallstackFrameCount = options.callstackFrameCount > s_MaxCallstackFrames
        ? static_cast<u32>(s_MaxCallstackFrames)
        : options.callstackFrameCount
    ;
    const u32 callstackFramesToSkip = options.callstackFramesToSkip > availableCallstackFrameCount
        ? availableCallstackFrameCount
        : options.callstackFramesToSkip
    ;
    request.callstackFramesToSkip = callstackFramesToSkip;
    request.callstackFrameCount = availableCallstackFrameCount - callstackFramesToSkip;
    for(u32 i = 0u; i < request.callstackFrameCount; ++i)
        request.callstackFrames[i] = options.callstackFrames[i + callstackFramesToSkip];
    CopyFixedBuffer(request.triggerEvent, options.triggerEvent);
    CopyFixedBuffer(request.triggerCategory, options.triggerCategory);
    CopyFixedBuffer(request.triggerExpression, options.triggerExpression);
    CopyFixedBuffer(request.triggerMessage, options.triggerMessage);
    CopyFixedBuffer(request.triggerFile, options.triggerFile);

#if defined(NWB_PLATFORM_ANDROID)
    if(options.writePackageInProcess){
        if(!WriteCrashPackage(request))
            return CrashDumpResult{ CrashDumpStatus::PackageWriteFailed };

        if(!options.uploadAfterWrite)
            return CrashDumpResult{ CrashDumpStatus::PackageWritten };

        return UploadCrashPackage(request)
            ? CrashDumpResult{ CrashDumpStatus::Uploaded }
            : CrashDumpResult{ CrashDumpStatus::UploadFailed }
        ;
    }
#endif

    const CrashDumpTransportStatus::Enum transportStatus = RequestCrashHandler(request, options.waitMilliseconds);
    if(transportStatus == CrashDumpTransportStatus::Failed)
        return CrashDumpResult{ CrashDumpStatus::RequestFailed };
    if(transportStatus == CrashDumpTransportStatus::Sent)
        return CrashDumpResult{ CrashDumpStatus::RequestQueued };

    const CrashDumpResult packageResult = CrashPackageResult(request);
    if(packageResult.status != CrashDumpStatus::RequestQueued)
        return packageResult;

    return transportStatus == CrashDumpTransportStatus::TimedOut
        ? CrashDumpResult{ CrashDumpStatus::RequestTimedOut }
        : CrashDumpResult{ CrashDumpStatus::PackageWriteFailed }
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

