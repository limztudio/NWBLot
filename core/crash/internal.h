// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"
#include "protocol.h"
#include "package_names.h"

#include <core/alloc/scratch.h>

#include <global/exception.h>
#include <global/generic.h>
#include <global/process.h>
#include <global/sync.h>
#include <global/thread.h>
#include <global/type_borrow.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CrashDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_MaxDiagnosticSites = 64u;
inline constexpr u32 s_PlatformCrashHandlerWaitMilliseconds = 3000u;
inline constexpr u32 s_HandlerExitWaitMilliseconds = 3000u;
inline constexpr u32 s_ManualCrashDumpWaitMilliseconds = 10000u;
inline constexpr usize s_HandlerArgumentTextCapacity = 32u;

#ifndef NWB_CRASH_HANDLER_EXECUTABLE_NAME
#define NWB_CRASH_HANDLER_EXECUTABLE_NAME "crash_handler"
#endif

#if defined(FOUNDATION_PLATFORM_WINDOWS)
inline constexpr TStringView s_HandlerExecutableFileName = FOUNDATION_TEXT(NWB_CRASH_HANDLER_EXECUTABLE_NAME) FOUNDATION_TEXT(".exe");
#else
inline constexpr TStringView s_HandlerExecutableFileName = FOUNDATION_TEXT(NWB_CRASH_HANDLER_EXECUTABLE_NAME);
#endif

inline constexpr TStringView s_RequestHandleArgument = FOUNDATION_TEXT("--request-handle");
inline constexpr TStringView s_AckHandleArgument = FOUNDATION_TEXT("--ack-handle");
inline constexpr TStringView s_AckEventArgument = FOUNDATION_TEXT("--ack-event");
inline constexpr StringView s_RequestFdArgument = "--request-fd";
inline constexpr StringView s_AckFdArgument = "--ack-fd";
inline constexpr StringView s_DefaultBreadcrumbCategory = "general";
inline constexpr StringView s_ManualDumpCategory = "manual_dump";
inline constexpr StringView s_GpuCrashCategory = "gpu_crash";
inline constexpr usize s_SnapshotImageCount = 2u;
inline constexpr usize s_MaxCrashSignals = 6u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
using CrashStringT = AString<ArenaT>;
template<typename ArenaT>
using CrashBytesT = Vector<u8, ArenaT>;


namespace CrashDumpTransportStatus{
    enum Enum : u8{
        Failed,
        Sent,
        PackageWritten,
        TimedOut,
        PackageWriteFailed,
    };
};


namespace PlatformOperation{
    enum Enum : u8{
        None,
        PrepareSpool,
        PrepareCommand,
        PrepareAttributes,
        CreateRequestChannel,
        CreateAckChannel,
        CreateStartupChannel,
        SetInheritance,
        CreateAckEvent,
        CreateProcess,
        CloseHandle,
        WaitProcess,
        TerminateProcess,
        CloseDescriptor,
        SetDescriptorFlags,
        PrepareChild,
        ExecHandler,
        WaitChild,
        KillChild,
        InstallSignalStack,
        RestoreSignalStack,
        InstallSignalAction,
        RestoreSignalAction,
        OpenEmergencyFile,
        RestoreExceptionFilter,
    };
};

namespace PlatformErrorDomain{
    enum Enum : u8{
        Windows,
        Posix,
        ExitCode,
        Signal,
        Contract,
        Filesystem,
    };
};

struct PlatformFailure{
    PlatformOperation::Enum operation = PlatformOperation::None;
    PlatformErrorDomain::Enum domain = PlatformErrorDomain::Contract;
    u64 code = 0u;
    ErrorCode filesystemError{};
};

// Created before the lifecycle lock: deferred observation runs after unlocking, including during unwind.
// Retains the primary operation failure (or first cleanup failure) and total count; other details are coalesced into that explicit count.
class PlatformCleanupObservations final : NoCopy{
public:
    PlatformCleanupObservations()noexcept = default;
    ~PlatformCleanupObservations()noexcept;


public:
    void record(const PlatformFailure& failure)noexcept;
    void recordPrimary(const PlatformFailure& failure)noexcept;


private:
    PlatformFailure m_first{};
    u64 m_failureCount = 0u;
};


struct FixedDiagnosticSite{
    u64 hash = 0u;
    u32 captureCount = 0u;
    u8 used = 0u;
};

struct ManualDumpContextStorage;

struct CrashDumpRequestOptions{
    u32 waitMilliseconds = 0u;
    u32 callstackFrameCount = 0u;
    u64 exceptionPointers = 0u;
    u64 faultAddress = 0u;
    u64 instructionPointer = 0u;
    u64 stackPointer = 0u;
    u64 framePointer = 0u;
    Array<u64, s_MaxCallstackFrames> callstackFrames = {};
    AStringView event;
    AStringView triggerCategory;
    AStringView triggerExpression;
    AStringView triggerMessage;
    AStringView triggerFile;
    AStringView gpuReport;
    AStringView gpuDump;
    GpuCrashDumpKind::Enum gpuDumpKind = GpuCrashDumpKind::None;
    u64 triggerInstructionPointer = 0u;
    u32 triggerLine = 0u;
    u32 callstackFramesToSkip = 0u;
#if defined(FOUNDATION_PLATFORM_WINDOWS)
    const ManualDumpContextStorage* manualContext = nullptr;
#endif
};

struct ManualDumpContextStorage final : NoCopy{
#if defined(FOUNDATION_PLATFORM_WINDOWS)
    CONTEXT context = {};
    EXCEPTION_RECORD exceptionRecord = {};
    EXCEPTION_POINTERS exceptionPointers = {};
#endif


public:
    ManualDumpContextStorage()noexcept = default;
    ManualDumpContextStorage(const ManualDumpContextStorage&) = delete;
    ManualDumpContextStorage(ManualDumpContextStorage&&) = delete;
#if defined(FOUNDATION_PLATFORM_WINDOWS)
    explicit ManualDumpContextStorage(const CrashRequest& request)noexcept
        : context(request.manualContext)
        , exceptionRecord(request.manualExceptionRecord)
        , exceptionPointers{ &exceptionRecord, &context }{}
#endif
};

// Snapshot images are immutable while pinned; only normal-context writers hold the lifecycle mutex.
// A fault reader never takes that mutex or waits for an interrupted writer to finish.
inline constexpr u64 s_SnapshotWriteClaim = u64{ 1u } << 63u;
inline constexpr u64 s_SnapshotMaxReaders = s_SnapshotWriteClaim - 1u;
static_assert(Atomic<u64>::is_always_lock_free && Atomic<u32>::is_always_lock_free, "crash snapshot publication requires native lock-free atomics");

struct CrashStateSnapshot{
    CrashSpoolRetentionConfig spoolRetention;
    Array<FixedMetadata, s_MaxMetadata> metadata = {};
    Array<FixedBreadcrumb, s_MaxBreadcrumbs> breadcrumbs = {};
    usize nextBreadcrumb = 0u;
    Array<char, s_MaxShortText> applicationName = {};
    Array<char, s_MaxShortText> versionText = {};
    Array<char, s_MaxMediumText> buildId = {};
    Array<char, s_MaxPathText> spoolDirectoryText = {};
    Array<char, s_MaxUrlText> logServerUrl = {};
    Array<char, s_MaxMediumText> crashUploadToken = {};
    DumpDetailMode::Enum dumpDetailMode = DumpDetailMode::Small;
};

struct CrashSnapshotImage{
    CrashStateSnapshot value;
    Atomic<u64> access{ 0u };
};

struct CrashState{
    CrashSpoolRetentionConfig spoolRetention;
    Array<FixedBreadcrumb, s_MaxBreadcrumbs> breadcrumbs = {};
    Array<FixedDiagnosticSite, s_MaxDiagnosticSites> diagnosticSites = {};
    usize nextBreadcrumb = 0u;
    Atomic<u64> breadcrumbOrder{ 1u };
    Atomic<u64> crashSequence{ 1u };
    Array<CrashSnapshotImage, s_SnapshotImageCount> snapshotImages;

#if defined(FOUNDATION_PLATFORM_WINDOWS)
    HANDLE requestWriteHandle = INVALID_HANDLE_VALUE;
    HANDLE ackReadHandle = INVALID_HANDLE_VALUE;
    HANDLE crashHandledEvent = nullptr;
    PROCESS_INFORMATION handlerProcessInfo = {};
    LPTOP_LEVEL_EXCEPTION_FILTER previousExceptionFilter = nullptr;
#elif defined(FOUNDATION_PLATFORM_LINUX) && !defined(FOUNDATION_PLATFORM_ANDROID)
    stack_t previousSignalStack = {};
    Array<struct sigaction, s_MaxCrashSignals> previousSignalActions = {};
    Array<int, s_MaxCrashSignals> installedSignals = {};
    Array<b8, s_MaxCrashSignals> signalActionInstalled = {};
    b8 signalStackInstalled = false;
    ThreadId signalStackOwner{};
#elif defined(FOUNDATION_PLATFORM_ANDROID)
    stack_t previousSignalStack = {};
    Array<struct sigaction, s_MaxCrashSignals> previousSignalActions = {};
    Array<int, s_MaxCrashSignals> installedSignals = {};
    Array<b8, s_MaxCrashSignals> signalActionInstalled = {};
    b8 signalStackInstalled = false;
    ThreadId signalStackOwner{};
#endif

    ::TerminateHandler previousTerminateHandler = nullptr;

    CrashCapturePolicy capturePolicy;
    Futex mutex;
    u32 diagnosticCaptureCount = 0u;
    Atomic<u32> publishedSnapshotImage{ 0u };
    Atomic<u32> suppressedPlatformCrashCaptures{ 0u };
    // Crash-transport channel guard: one CAS claims the pipe/event so concurrent faulting threads never interleave fixed-POD writes.
    // Distinct from `mutex` (the assert path may hold it), and atomic (not Futex) so it stays signal/SEH-safe.
    Atomic<u32> transportInFlight{ 0u };

#if defined(FOUNDATION_PLATFORM_LINUX) && !defined(FOUNDATION_PLATFORM_ANDROID)
    int requestWriteFd = -1;
    int ackReadFd = -1;
    pid_t handlerPid = -1;
#elif defined(FOUNDATION_PLATFORM_ANDROID)
    int emergencyWriteFd = -1;
#endif

    Array<FixedMetadata, s_MaxMetadata> metadata = {};
    Array<char, s_MaxUrlText> logServerUrl = {};
    Array<char, s_MaxPathText> spoolDirectoryText = {};
    Array<char, s_MaxPathText> handlerExecutablePathText = {};
    Array<char, s_MaxMediumText> buildId = {};
    Array<char, s_MaxMediumText> crashUploadToken = {};
    Array<char, s_MaxShortText> applicationName = {};
    Array<char, s_MaxShortText> versionText = {};
    DumpDetailMode::Enum dumpDetailMode = DumpDetailMode::Small;
    b8 installed = false;
    b8 handlerStarted = false;
    b8 platformRestorePending = false;
    b8 terminateHandlerInstalled = false;

#if defined(FOUNDATION_PLATFORM_WINDOWS)
    b8 exceptionFilterInstalled = false;
#endif
};


extern CrashState g_State;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline ::Path<ArenaT> PendingDirectory(const ::Path<ArenaT>& spoolDirectory){
    return spoolDirectory / PackageNames::s_PendingDirectoryName;
}

template<typename ArenaT>
[[nodiscard]] inline ::Path<ArenaT> UploadedDirectory(const ::Path<ArenaT>& spoolDirectory){
    return spoolDirectory / PackageNames::s_UploadedDirectoryName;
}

template<typename ArenaT>
[[nodiscard]] inline ::Path<ArenaT> UploadingDirectory(const ::Path<ArenaT>& spoolDirectory){
    return spoolDirectory / PackageNames::s_UploadingDirectoryName;
}

template<typename ArenaT>
[[nodiscard]] inline ::Path<ArenaT> FailedDirectory(const ::Path<ArenaT>& spoolDirectory){
    return spoolDirectory / PackageNames::s_FailedDirectoryName;
}

template<typename ArenaT>
[[nodiscard]] inline ::Path<ArenaT> RequestPendingDirectory(ArenaT& arena, const CrashRequest& request){
    return ::Path<ArenaT>(arena, request.spoolDirectory.data()) / PackageNames::s_PendingDirectoryName / request.crashId.data();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] StringView PlatformKindName(u32 platform)noexcept;
[[nodiscard]] StringView ReasonKindName(u32 reasonKind)noexcept;

// The caller owns g_State.mutex and has completed its normal-context mutation.
void PublishCrashStateSnapshot()noexcept;
void SnapshotCrashState(CrashReasonKind::Enum reasonKind, u32 reasonCode, CrashRequest& outRequest)noexcept;
void SuppressNextPlatformCrashCapture()noexcept;
[[nodiscard]] b8 TryConsumeSuppressedPlatformCrashCapture()noexcept;
void CaptureManualDumpContext(CrashDumpRequestOptions& outOptions, ManualDumpContextStorage& storage)noexcept;
[[nodiscard]] CrashRequest BuildCrashDumpRequest(CrashReasonKind::Enum reasonKind, u32 reasonCode, const CrashDumpRequestOptions& options)noexcept;
[[nodiscard]] CrashDumpResult RequestCrashDump(CrashReasonKind::Enum reasonKind, u32 reasonCode, const CrashDumpRequestOptions& options);
[[nodiscard]] CrashDumpTransportStatus::Enum RequestCrashHandler(const CrashRequest& request, u32 waitMilliseconds)noexcept;
void NotifyCrashHandler(CrashReasonKind::Enum reasonKind, u32 reasonCode, const CrashDumpRequestOptions& options = CrashDumpRequestOptions{})noexcept;

template<typename ArenaT>
[[nodiscard]] Expected<void, ErrorCode> EnsureCrashSpoolDirectories(const ::Path<ArenaT>& spoolDirectory);
[[nodiscard]] Alloc::PersistentArena& DumpArena();
void InitializeDumpArena();

#if defined(FOUNDATION_PLATFORM_WINDOWS)
template<typename ArenaT>
[[nodiscard]] Expected<void, PlatformFailure> StartDesktopHandler(const ::Path<ArenaT>& handlerExecutablePath, Alloc::ScratchArena& scratchArena, PlatformCleanupObservations& observations);
#else
template<typename ArenaT>
[[nodiscard]] Expected<void, PlatformFailure> StartDesktopHandler(const ::Path<ArenaT>& handlerExecutablePath, Alloc::ScratchArena& scratchArena, PlatformCleanupObservations& observations)noexcept;
#endif
[[nodiscard]] Expected<void, PlatformFailure> InstallPlatformHandlers()noexcept;
void UninstallPlatformResources(PlatformCleanupObservations& observations)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

