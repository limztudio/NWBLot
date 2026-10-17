// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "internal.h"

#include <global/diagnostics.h>
#include <global/thread.h>
#include <global/scope_exit.h>

NWB_CRASH_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Detail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CrashState g_State;

inline constexpr StringView s_UnknownPlatformLabel = "unknown";
inline constexpr StringView s_UnknownReasonLabel = "unknown";


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

StringView PlatformKindName(const u32 platform)noexcept{
    switch(platform){
    case PlatformKind::Windows:
        return "windows";
    case PlatformKind::Linux:
        return "linux";
    case PlatformKind::Android:
        return "android";
    default:
        return s_UnknownPlatformLabel;
    }
}

StringView ReasonKindName(const u32 reasonKind)noexcept{
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
        return s_UnknownReasonLabel;
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

void PublishCrashStateSnapshot()noexcept{
    const u32 imageIndex = 1u - g_State.publishedSnapshotImage.load(MemoryOrder::relaxed);
    CrashSnapshotImage& image = g_State.snapshotImages[imageIndex];
    u64 expected = 0u;
    while(!image.access.compare_exchange_weak(expected, s_SnapshotWriteClaim, MemoryOrder::acq_rel, MemoryOrder::relaxed)){
        expected = 0u;
        MachinePause(1);
    }

    CrashStateSnapshot& snapshot = image.value;
    snapshot.dumpDetailMode = g_State.dumpDetailMode;
    snapshot.spoolRetention = g_State.spoolRetention;
    snapshot.nextBreadcrumb = g_State.nextBreadcrumb;
    CopyFixedBuffer(snapshot.applicationName, g_State.applicationName);
    CopyFixedBuffer(snapshot.versionText, g_State.versionText);
    CopyFixedBuffer(snapshot.buildId, g_State.buildId);
    CopyFixedBuffer(snapshot.spoolDirectoryText, g_State.spoolDirectoryText);
    CopyFixedBuffer(snapshot.logServerUrl, g_State.logServerUrl);
    CopyFixedBuffer(snapshot.crashUploadToken, g_State.crashUploadToken);
    for(usize i = 0u; i < s_MaxMetadata; ++i)
        snapshot.metadata[i] = g_State.metadata[i];
    for(usize i = 0u; i < s_MaxBreadcrumbs; ++i)
        snapshot.breadcrumbs[i] = g_State.breadcrumbs[i];

    // Clear the write claim before publication: a fault may interrupt the publishing thread immediately after the index store.
    image.access.store(0u, MemoryOrder::release);
    g_State.publishedSnapshotImage.store(imageIndex, MemoryOrder::release);
}

void SnapshotCrashState(CrashRequest& outRequest, const CrashReasonKind::Enum reasonKind, const u32 reasonCode)noexcept{
    outRequest = CrashRequest{};
    outRequest.platform = CurrentPlatformKind();
    outRequest.reasonKind = reasonKind;
    outRequest.reasonCode = reasonCode;
    outRequest.processId = CurrentProcessId();
    outRequest.threadId = CurrentThreadId();
    const u64 sequence = g_State.crashSequence.fetch_add(1u, MemoryOrder::relaxed);
    BuildCrashId(outRequest, sequence);
    CopyFixedBuffer(outRequest.abi, CurrentAbiName());

    for(;;){
        const u32 imageIndex = g_State.publishedSnapshotImage.load(MemoryOrder::acquire);
        CrashSnapshotImage& image = g_State.snapshotImages[imageIndex];
        u64 readers = image.access.load(MemoryOrder::acquire);
        if((readers & s_SnapshotWriteClaim) != 0u || readers == s_SnapshotMaxReaders)
            continue;
        if(!image.access.compare_exchange_weak(readers, readers + 1u, MemoryOrder::acquire, MemoryOrder::acquire))
            continue;
        ScopeExit releaseImage([&image]()noexcept{ image.access.fetch_sub(1u, MemoryOrder::release); });

        const CrashStateSnapshot& snapshot = image.value;
        outRequest.dumpDetailMode = static_cast<u32>(snapshot.dumpDetailMode);
        outRequest.spoolRetention = snapshot.spoolRetention;
        CopyFixedBuffer(outRequest.applicationName, snapshot.applicationName);
        CopyFixedBuffer(outRequest.versionText, snapshot.versionText);
        CopyFixedBuffer(outRequest.buildId, snapshot.buildId);
        CopyFixedBuffer(outRequest.spoolDirectory, snapshot.spoolDirectoryText);
        CopyFixedBuffer(outRequest.logServerUrl, snapshot.logServerUrl);
        CopyFixedBuffer(outRequest.crashUploadToken, snapshot.crashUploadToken);

        for(usize i = 0u; i < s_MaxMetadata; ++i){
            if(!snapshot.metadata[i].used)
                continue;
            outRequest.metadata[outRequest.metadataCount++] = snapshot.metadata[i];
        }

        const usize begin = snapshot.nextBreadcrumb >= s_MaxBreadcrumbs ? snapshot.nextBreadcrumb - s_MaxBreadcrumbs : 0u;
        for(usize i = begin; i < snapshot.nextBreadcrumb; ++i){
            const FixedBreadcrumb& breadcrumb = snapshot.breadcrumbs[i % s_MaxBreadcrumbs];
            if(breadcrumb.used)
                outRequest.breadcrumbs[outRequest.breadcrumbCount++] = breadcrumb;
        }
        return;
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

CrashRequest BuildCrashDumpRequest(const CrashReasonKind::Enum reasonKind, const u32 reasonCode, const CrashDumpRequestOptions& options)noexcept{
    CrashRequest request;
    SnapshotCrashState(request, reasonKind, reasonCode);
    request.exceptionPointers = options.exceptionPointers;
#if defined(NWB_PLATFORM_WINDOWS)
    if(options.manualContext){
        request.manualContextSize = sizeof(CONTEXT);
        request.manualExceptionRecordSize = sizeof(EXCEPTION_RECORD);
        request.manualContextArchitecture = s_WindowsContextArchitecture;
        request.manualContext = options.manualContext->context;
        // RtlCaptureContext owns only this fixed CONTEXT, without separately allocated XSTATE bytes.
        request.manualContext.ContextFlags &= ~s_WindowsExtendedContextFlag;
        request.manualExceptionRecord = options.manualContext->exceptionRecord;
        request.exceptionPointers = 0u;
    }
#endif
    request.faultAddress = options.faultAddress;
    request.instructionPointer = options.triggerInstructionPointer != 0u ? options.triggerInstructionPointer : options.instructionPointer;
    request.stackPointer = options.stackPointer;
    request.framePointer = options.framePointer;
    request.triggerLine = options.triggerLine;
    const u32 availableCallstackFrameCount = options.callstackFrameCount > s_MaxCallstackFrames ? static_cast<u32>(s_MaxCallstackFrames) : options.callstackFrameCount;
    const u32 callstackFramesToSkip = options.callstackFramesToSkip > availableCallstackFrameCount ? availableCallstackFrameCount : options.callstackFramesToSkip;
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

    return request;
}

CrashDumpResult RequestCrashDump(const CrashReasonKind::Enum reasonKind, const u32 reasonCode, const CrashDumpRequestOptions& options){
    const CrashRequest request = BuildCrashDumpRequest(reasonKind, reasonCode, options);
    if(!IsCurrentCrashRequest(request))
        return CrashDumpResult{ CrashDumpStatus::RequestFailed };

    // GPU report/dump ship as package files straight from caller bytes (no POD copy, no heap alloc).
    // Device-lost path only (normal thread context), never the signal/SEH path; binary-verbatim writes.
    if(!options.gpuReport.empty() || !options.gpuDump.empty()){
        Alloc::PersistentArena& dumpArena = DumpArena();
        const ::Path<Alloc::PersistentArena> packageDirectory = RequestPendingDirectory(dumpArena, request);
        if(EnsureDirectories(packageDirectory)){
            if(!options.gpuReport.empty()){
                if(!WriteTextFile(packageDirectory / PackageNames::s_GpuCrashReportFileName, options.gpuReport))
                    return CrashDumpResult{ CrashDumpStatus::PackageWriteFailed };
            }
            if(!options.gpuDump.empty()){
                StringView gpuDumpFileName;
                if(options.gpuDumpKind == GpuCrashDumpKind::RadeonGpuDetective)
                    gpuDumpFileName = PackageNames::s_GpuDetectiveCaptureFileName;
                else if(options.gpuDumpKind == GpuCrashDumpKind::Aftermath)
                    gpuDumpFileName = PackageNames::s_AftermathGpuDumpFileName;

                if(!gpuDumpFileName.empty()){
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

