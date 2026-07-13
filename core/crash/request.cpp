
#include "internal.h"

#include <global/diagnostics.h>

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
    case CrashReasonKind::GpuCrash:
        return s_GpuCrashCategory;
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

static AStringView EventNameForRequest(const CrashReasonKind::Enum reasonKind, const CrashDumpRequestOptions& options)noexcept{
    if(!options.event.empty())
        return options.event;
    if(options.triggerCategory == DiagnosticEventCategory::s_Assert)
        return AStringView(DiagnosticEventName::s_Assert);
    if(options.triggerCategory == DiagnosticEventCategory::s_FatalAssert)
        return AStringView(DiagnosticEventName::s_Assert);
    if(reasonKind == CrashReasonKind::GpuCrash)
        return AStringView(DiagnosticEventName::s_GpuCrash);
    if(reasonKind == CrashReasonKind::ManualDump)
        return AStringView(DiagnosticEventName::s_ManualDump);
    return AStringView(DiagnosticEventName::s_Crash);
}

void SnapshotCrashState(CrashRequest& outRequest, const CrashReasonKind::Enum reasonKind, const u32 reasonCode)noexcept{
    outRequest = CrashRequest{};
    outRequest.platform = CurrentPlatformKind();
    outRequest.reasonKind = reasonKind;
    outRequest.reasonCode = reasonCode;
    outRequest.processId = CurrentProcessId();
    outRequest.threadId = CurrentThreadId();
    outRequest.dumpDetailMode = static_cast<u32>(g_State.dumpDetailMode);
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

void SuppressNextPlatformCrashCapture()noexcept{
    g_State.suppressedPlatformCrashCaptures.fetch_add(1u, MemoryOrder::release);
}

bool TryConsumeSuppressedPlatformCrashCapture()noexcept{
    u32 count = g_State.suppressedPlatformCrashCaptures.load(MemoryOrder::acquire);
    while(count != 0u){
        if(g_State.suppressedPlatformCrashCaptures.compare_exchange_weak(count, count - 1u, MemoryOrder::acq_rel, MemoryOrder::acquire))
            return true;
    }
    return false;
}

CrashDumpResult RequestCrashDump(const CrashReasonKind::Enum reasonKind, const u32 reasonCode, const CrashDumpRequestOptions& options){
    CrashRequest request;
    SnapshotCrashState(request, reasonKind, reasonCode);
    request.exceptionPointers = options.exceptionPointers;
    request.faultAddress = options.faultAddress;
    request.instructionPointer = options.triggerInstructionPointer != 0u
        ? options.triggerInstructionPointer
        : options.instructionPointer
    ;
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
    u32 outputCallstackFrameCount = 0u;
    if(options.triggerInstructionPointer != 0u)
        request.callstackFrames[outputCallstackFrameCount++] = options.triggerInstructionPointer;

    for(u32 i = callstackFramesToSkip; i < availableCallstackFrameCount && outputCallstackFrameCount < s_MaxCallstackFrames; ++i){
        const u64 frame = options.callstackFrames[i];
        if(frame == 0u)
            continue;

        bool duplicate = false;
        for(u32 existing = 0u; existing < outputCallstackFrameCount; ++existing){
            if(request.callstackFrames[existing] == frame){
                duplicate = true;
                break;
            }
        }
        if(!duplicate)
            request.callstackFrames[outputCallstackFrameCount++] = frame;
    }
    request.callstackFrameCount = outputCallstackFrameCount;
    CopyFixedBuffer(request.event, EventNameForRequest(reasonKind, options));
    CopyFixedBuffer(request.triggerCategory, options.triggerCategory);
    CopyFixedBuffer(request.triggerExpression, options.triggerExpression);
    CopyFixedBuffer(request.triggerMessage, options.triggerMessage);
    CopyFixedBuffer(request.triggerFile, options.triggerFile);

    // Ship the (untruncated) GPU report and the optional binary GPU crash dump as package files written
    // straight from the caller's existing bytes: no copy into the fixed POD and no growable-heap allocation.
    // The handler archives the package directory, so the files are included automatically. gpuReport/gpuDump
    // are only set on the GPU device-lost path (a normal thread context), so this never adds file I/O to the
    // hard-crash (signal/SEH) path. WriteTextFile opens the stream in binary mode and writes verbatim, so it
    // serializes raw '.rgd' and '.nv-gpudmp' dump bytes unchanged.
    if(!options.gpuReport.empty() || !options.gpuDump.empty()){
        Alloc::PersistentArena& dumpArena = DumpArena();
        const ::Path<Alloc::PersistentArena> packageDirectory = RequestPendingDirectory(dumpArena, request);
        ErrorCode error;
        if(EnsureDirectories(packageDirectory, error)){
            if(!options.gpuReport.empty()){
                if(!WriteTextFile(packageDirectory / PackageNames::s_GpuCrashReportFileName, options.gpuReport))
                    return CrashDumpResult{ CrashDumpStatus::PackageWriteFailed };
            }
            if(!options.gpuDump.empty()){
                const char* gpuDumpFileName = nullptr;
                if(options.gpuDumpKind == GpuCrashDumpKind::RadeonGpuDetective)
                    gpuDumpFileName = PackageNames::s_GpuDetectiveCaptureFileName;
                else if(options.gpuDumpKind == GpuCrashDumpKind::Aftermath)
                    gpuDumpFileName = PackageNames::s_AftermathGpuDumpFileName;

                if(gpuDumpFileName){
                    if(!WriteTextFile(packageDirectory / gpuDumpFileName, options.gpuDump))
                        return CrashDumpResult{ CrashDumpStatus::PackageWriteFailed };
                }
            }
        }
        else
            return CrashDumpResult{ CrashDumpStatus::PackageWriteFailed };
    }

    const CrashDumpTransportStatus::Enum transportStatus = RequestCrashHandler(request, options.waitMilliseconds);
    if(transportStatus == CrashDumpTransportStatus::Failed)
        return CrashDumpResult{ CrashDumpStatus::RequestFailed };
    if(transportStatus == CrashDumpTransportStatus::Sent)
        return CrashDumpResult{ CrashDumpStatus::RequestQueued };
    if(transportStatus == CrashDumpTransportStatus::TimedOut)
        return CrashDumpResult{ CrashDumpStatus::RequestTimedOut };
    if(transportStatus == CrashDumpTransportStatus::PackageWriteFailed)
        return CrashDumpResult{ CrashDumpStatus::PackageWriteFailed };

    return CrashDumpResult{ CrashDumpStatus::PackageWritten };
}

void NotifyCrashHandler(const CrashReasonKind::Enum reasonKind, const u32 reasonCode, const CrashDumpRequestOptions& options)noexcept{
    CrashDumpRequestOptions requestOptions = options;
    requestOptions.waitMilliseconds = s_PlatformCrashHandlerWaitMilliseconds;
    const CrashDumpResult requestResult = RequestCrashDump(reasonKind, reasonCode, requestOptions);
    if(!requestResult.requestAccepted())
        return;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

