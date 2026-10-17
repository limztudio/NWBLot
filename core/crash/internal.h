// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"
#include "package_names.h"

#include <core/alloc/scratch.h>

#include <global/exception.h>
#include <global/generic.h>
#include <global/sync.h>
#include <global/thread.h>
#include <global/type_borrow.h>

#if defined(NWB_PLATFORM_WINDOWS)
#include <windows.h>
#elif defined(NWB_PLATFORM_LINUX) || defined(NWB_PLATFORM_ANDROID)
#include <signal.h>
#include <sys/types.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Detail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_RequestMagic = 0x4E574243u; // NWBC
inline constexpr u32 s_AckMagic = 0x4E574241u; // NWBA
inline constexpr u32 s_RequestVersion = 2u;
inline constexpr usize s_MaxMetadata = 32u;
inline constexpr usize s_MaxBreadcrumbs = 128u;
inline constexpr usize s_MaxDiagnosticSites = 64u;
inline constexpr usize s_MaxCallstackFrames = 64u;
inline constexpr usize s_MaxShortText = 64u;
inline constexpr usize s_MaxMediumText = 256u;
inline constexpr usize s_MaxPathText = 1024u;
inline constexpr usize s_MaxUrlText = 1024u;
inline constexpr u32 s_ManualDumpExceptionCode = 0xE0425742u;
inline constexpr u32 s_PlatformCrashHandlerWaitMilliseconds = 3000u;
inline constexpr u32 s_ManualCrashDumpWaitMilliseconds = 10000u;
inline constexpr usize s_HandlerArgumentTextCapacity = 32u;

#ifndef NWB_CRASH_HANDLER_EXECUTABLE_NAME
#define NWB_CRASH_HANDLER_EXECUTABLE_NAME "crash_handler"
#endif

#if defined(NWB_PLATFORM_WINDOWS)
inline constexpr u32 s_WindowsContextArchitectureMask = 0x00FF0000u;
inline constexpr u32 s_WindowsContextArchitecture = CONTEXT_CONTROL & s_WindowsContextArchitectureMask;
inline constexpr u32 s_WindowsExtendedContextFlag = CONTEXT_XSTATE & ~s_WindowsContextArchitectureMask;
inline constexpr TStringView s_HandlerExecutableFileName = NWB_TEXT(NWB_CRASH_HANDLER_EXECUTABLE_NAME) NWB_TEXT(".exe");
#else
inline constexpr TStringView s_HandlerExecutableFileName = NWB_TEXT(NWB_CRASH_HANDLER_EXECUTABLE_NAME);
#endif

inline constexpr TStringView s_RequestHandleArgument = NWB_TEXT("--request-handle");
inline constexpr TStringView s_AckHandleArgument = NWB_TEXT("--ack-handle");
inline constexpr TStringView s_AckEventArgument = NWB_TEXT("--ack-event");
inline constexpr StringView s_RequestFdArgument = "--request-fd";
inline constexpr StringView s_AckFdArgument = "--ack-fd";
inline constexpr StringView s_DefaultBreadcrumbCategory = "general";
inline constexpr StringView s_ManualDumpCategory = "manual_dump";
inline constexpr StringView s_GpuCrashCategory = "gpu_crash";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
using CrashStringT = AString<ArenaT>;
template<typename ArenaT>
using CrashBytesT = Vector<u8, ArenaT>;


namespace PlatformKind{
    enum Enum : u32{
        Unknown,
        Windows,
        Linux,
        Android,
    };
};

namespace CrashReasonKind{
    enum Enum : u32{
        Unknown,
        WindowsException,
        PosixSignal,
        Terminate,
        ManualDump,
        GpuCrash,
    };
};

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


struct FixedMetadata{
    char key[s_MaxShortText] = {};
    char value[s_MaxMediumText] = {};
    u8 used = 0u;
};

struct FixedBreadcrumb{
    u64 order = 0u;
    char category[s_MaxShortText] = {};
    char message[s_MaxMediumText] = {};
    u8 used = 0u;
};

struct FixedDiagnosticSite{
    u64 hash = 0u;
    u32 captureCount = 0u;
    u8 used = 0u;
};

struct CrashRequest{
    u32 magic = s_RequestMagic;
    u32 version = s_RequestVersion;
    u32 platform = PlatformKind::Unknown;
    u32 reasonKind = CrashReasonKind::Unknown;
    u32 reasonCode = 0u;
    u32 processId = 0u;
    u32 threadId = 0u;
    u32 dumpDetailMode = DumpDetailMode::Small;
    u64 exceptionPointers = 0u;
    u64 faultAddress = 0u;
    u64 instructionPointer = 0u;
    u64 stackPointer = 0u;
    u64 framePointer = 0u;
    u32 triggerLine = 0u;
    u32 callstackFrameCount = 0u;
    u32 callstackFramesToSkip = 0u;
    CrashSpoolRetentionConfig spoolRetention;
    char crashId[s_MaxShortText] = {};
    char applicationName[s_MaxShortText] = {};
    char versionText[s_MaxShortText] = {};
    char buildId[s_MaxMediumText] = {};
    char abi[s_MaxShortText] = {};
    char spoolDirectory[s_MaxPathText] = {};
    char logServerUrl[s_MaxUrlText] = {};
    char crashUploadToken[s_MaxMediumText] = {};
    char event[s_MaxShortText] = {};
    char triggerCategory[s_MaxShortText] = {};
    char triggerExpression[s_MaxMediumText] = {};
    char triggerMessage[s_MaxMediumText] = {};
    char triggerFile[s_MaxPathText] = {};
    u32 metadataCount = 0u;
    u32 breadcrumbCount = 0u;
    FixedMetadata metadata[s_MaxMetadata] = {};
    FixedBreadcrumb breadcrumbs[s_MaxBreadcrumbs] = {};
    u64 callstackFrames[s_MaxCallstackFrames] = {};
#if defined(NWB_PLATFORM_WINDOWS)
    u32 manualContextSize = 0u;
    u32 manualExceptionRecordSize = 0u;
    u32 manualContextArchitecture = 0u;
    CONTEXT manualContext = {};
    EXCEPTION_RECORD manualExceptionRecord = {};
#endif
};

static_assert(IsTriviallyCopyable_V<CrashRequest>, "crash request transport requires an owning fixed native value");
#if defined(NWB_PLATFORM_WINDOWS)
static_assert(alignof(CrashRequest) >= alignof(CONTEXT), "crash request must preserve native context alignment");
#endif

struct CrashAck{
    u32 magic = s_AckMagic;
    u32 version = s_RequestVersion;
    u8 packageWritten = 0u;
    char crashId[s_MaxShortText] = {};
};

// This fixed-size native protocol is shared only by the application and its same-build handler.
// Native context admission does not provide mixed-binary or cross-architecture message framing.
[[nodiscard]] inline bool IsCurrentCrashRequest(const CrashRequest& request)noexcept{
    if(request.magic != s_RequestMagic || request.version != s_RequestVersion)
        return false;
#if defined(NWB_PLATFORM_WINDOWS)
    if(request.manualContextSize == 0u)
        return request.manualExceptionRecordSize == 0u && request.manualContextArchitecture == 0u;
    if(request.platform != PlatformKind::Windows || request.exceptionPointers != 0u)
        return false;
    if(request.manualContextSize != sizeof(CONTEXT) || request.manualExceptionRecordSize != sizeof(EXCEPTION_RECORD) || request.manualContextArchitecture != s_WindowsContextArchitecture)
        return false;
    if((request.manualContext.ContextFlags & s_WindowsContextArchitectureMask) != s_WindowsContextArchitecture || (request.manualContext.ContextFlags & CONTEXT_CONTROL) != CONTEXT_CONTROL)
        return false;
    if((request.manualContext.ContextFlags & s_WindowsExtendedContextFlag) != 0u)
        return false;
    return request.manualExceptionRecord.ExceptionCode == s_ManualDumpExceptionCode && request.manualExceptionRecord.ExceptionRecord == nullptr && request.manualExceptionRecord.NumberParameters == 0u;
#else
    return true;
#endif
}

[[nodiscard]] inline bool CrashAckMatchesRequest(const CrashAck& ack, const CrashRequest& request)noexcept{
    return ack.magic == s_AckMagic && ack.version == s_RequestVersion && AStringView(ack.crashId) == AStringView(request.crashId);
}

struct ManualDumpContextStorage;

struct CrashDumpRequestOptions{
    u32 waitMilliseconds = 0u;
    u32 callstackFrameCount = 0u;
    u64 exceptionPointers = 0u;
    u64 faultAddress = 0u;
    u64 instructionPointer = 0u;
    u64 stackPointer = 0u;
    u64 framePointer = 0u;
    u64 callstackFrames[s_MaxCallstackFrames] = {};
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
#if defined(NWB_PLATFORM_WINDOWS)
    const ManualDumpContextStorage* manualContext = nullptr;
#endif
};

struct ManualDumpContextStorage final : NoCopy{
#if defined(NWB_PLATFORM_WINDOWS)
    CONTEXT context = {};
    EXCEPTION_RECORD exceptionRecord = {};
    EXCEPTION_POINTERS exceptionPointers = {};
#endif


public:
    ManualDumpContextStorage()noexcept = default;
    ManualDumpContextStorage(const ManualDumpContextStorage&) = delete;
    ManualDumpContextStorage(ManualDumpContextStorage&&) = delete;
#if defined(NWB_PLATFORM_WINDOWS)
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
    FixedMetadata metadata[s_MaxMetadata] = {};
    FixedBreadcrumb breadcrumbs[s_MaxBreadcrumbs] = {};
    usize nextBreadcrumb = 0u;
    char applicationName[s_MaxShortText] = {};
    char versionText[s_MaxShortText] = {};
    char buildId[s_MaxMediumText] = {};
    char spoolDirectoryText[s_MaxPathText] = {};
    char logServerUrl[s_MaxUrlText] = {};
    char crashUploadToken[s_MaxMediumText] = {};
    DumpDetailMode::Enum dumpDetailMode = DumpDetailMode::Small;
};

struct CrashSnapshotImage{
    CrashStateSnapshot value;
    Atomic<u64> access{ 0u };
};

struct CrashState{
    CrashSpoolRetentionConfig spoolRetention;
    FixedBreadcrumb breadcrumbs[s_MaxBreadcrumbs] = {};
    FixedDiagnosticSite diagnosticSites[s_MaxDiagnosticSites] = {};
    usize nextBreadcrumb = 0u;
    Atomic<u64> breadcrumbOrder{ 1u };
    Atomic<u64> crashSequence{ 1u };
    CrashSnapshotImage snapshotImages[2u];
    Atomic<u32> publishedSnapshotImage{ 0u };

#if defined(NWB_PLATFORM_WINDOWS)
    HANDLE requestWriteHandle = INVALID_HANDLE_VALUE;
    HANDLE ackReadHandle = INVALID_HANDLE_VALUE;
    HANDLE crashHandledEvent = nullptr;
    PROCESS_INFORMATION handlerProcessInfo = {};
    LPTOP_LEVEL_EXCEPTION_FILTER previousExceptionFilter = nullptr;
    bool exceptionFilterInstalled = false;
#elif defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
    stack_t previousSignalStack = {};
    struct sigaction previousSignalActions[6u] = {};
    int installedSignals[6u] = {};
    bool signalActionInstalled[6u] = {};
    bool signalStackInstalled = false;
    ThreadId signalStackOwner{};
#elif defined(NWB_PLATFORM_ANDROID)
    stack_t previousSignalStack = {};
    struct sigaction previousSignalActions[6u] = {};
    int installedSignals[6u] = {};
    bool signalActionInstalled[6u] = {};
    bool signalStackInstalled = false;
    ThreadId signalStackOwner{};
#endif

    ::TerminateHandler previousTerminateHandler = nullptr;
    bool terminateHandlerInstalled = false;
    bool platformRestorePending = false;

    CrashCapturePolicy capturePolicy;
    Futex mutex;
    u32 diagnosticCaptureCount = 0u;
    Atomic<u32> suppressedPlatformCrashCaptures{ 0u };
    // Crash-transport channel guard: a single CAS claims the request pipe/event so concurrent faulting threads never interleave their fixed-POD writes.
    // Deliberately distinct from `mutex` (which the assert/diagnostic path may already hold when a hard fault occurs), and an atomic (not a Futex) so it is signal/SEH-safe.
    Atomic<u32> transportInFlight{ 0u };

#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
    int requestWriteFd = -1;
    int ackReadFd = -1;
    pid_t handlerPid = -1;
#elif defined(NWB_PLATFORM_ANDROID)
    int emergencyWriteFd = -1;
#endif

    FixedMetadata metadata[s_MaxMetadata] = {};
    char logServerUrl[s_MaxUrlText] = {};
    char spoolDirectoryText[s_MaxPathText] = {};
    char handlerExecutablePathText[s_MaxPathText] = {};
    char buildId[s_MaxMediumText] = {};
    char crashUploadToken[s_MaxMediumText] = {};
    char applicationName[s_MaxShortText] = {};
    char versionText[s_MaxShortText] = {};
    DumpDetailMode::Enum dumpDetailMode = DumpDetailMode::Small;
    bool installed = false;
    bool handlerStarted = false;
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
    return ::Path<ArenaT>(arena, request.spoolDirectory) / PackageNames::s_PendingDirectoryName / request.crashId;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] StringView PlatformKindName(u32 platform)noexcept;
[[nodiscard]] StringView ReasonKindName(u32 reasonKind)noexcept;

// The caller owns g_State.mutex and has completed its normal-context mutation.
void PublishCrashStateSnapshot()noexcept;
void SnapshotCrashState(CrashRequest& outRequest, CrashReasonKind::Enum reasonKind, u32 reasonCode)noexcept;
void SuppressNextPlatformCrashCapture()noexcept;
[[nodiscard]] bool TryConsumeSuppressedPlatformCrashCapture()noexcept;
void CaptureManualDumpContext(CrashDumpRequestOptions& outOptions, ManualDumpContextStorage& storage)noexcept;
[[nodiscard]] CrashRequest BuildCrashDumpRequest(CrashReasonKind::Enum reasonKind, u32 reasonCode, const CrashDumpRequestOptions& options)noexcept;
[[nodiscard]] CrashDumpResult RequestCrashDump(CrashReasonKind::Enum reasonKind, u32 reasonCode, const CrashDumpRequestOptions& options);
[[nodiscard]] CrashDumpTransportStatus::Enum RequestCrashHandler(const CrashRequest& request, u32 waitMilliseconds)noexcept;
void NotifyCrashHandler(CrashReasonKind::Enum reasonKind, u32 reasonCode, const CrashDumpRequestOptions& options = CrashDumpRequestOptions{})noexcept;

template<typename ArenaT>
[[nodiscard]] Expected<void, ErrorCode> EnsureCrashSpoolDirectories(const ::Path<ArenaT>& spoolDirectory);
[[nodiscard]] Alloc::PersistentArena& DumpArena();
void InitializeDumpArena();

#if defined(NWB_PLATFORM_WINDOWS)
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

