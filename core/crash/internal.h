// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"
#include "package_names.h"

#include <global/sync.h>

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
inline constexpr u32 s_RequestVersion = 1u;
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
inline constexpr tchar s_HandlerExecutableFileName[] = NWB_TEXT(NWB_CRASH_HANDLER_EXECUTABLE_NAME) NWB_TEXT(".exe");
#else
inline constexpr tchar s_HandlerExecutableFileName[] = NWB_TEXT(NWB_CRASH_HANDLER_EXECUTABLE_NAME);
#endif

inline constexpr tchar s_RequestHandleArgument[] = NWB_TEXT("--request-handle");
inline constexpr tchar s_AckHandleArgument[] = NWB_TEXT("--ack-handle");
inline constexpr tchar s_AckEventArgument[] = NWB_TEXT("--ack-event");
inline constexpr const char* s_RequestFdArgument = "--request-fd";
inline constexpr const char* s_AckFdArgument = "--ack-fd";
inline constexpr const char* s_DefaultBreadcrumbCategory = "general";
inline constexpr const char* s_ManualDumpCategory = "manual_dump";
inline constexpr const char* s_GpuCrashCategory = "gpu_crash";


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
};

struct CrashAck{
    u32 magic = s_AckMagic;
    u32 version = s_RequestVersion;
    u8 packageWritten = 0u;
    char crashId[s_MaxShortText] = {};
};

struct CrashDumpRequestOptions{
    u32 waitMilliseconds = 0u;
    u64 exceptionPointers = 0u;
    u64 faultAddress = 0u;
    u64 instructionPointer = 0u;
    u64 stackPointer = 0u;
    u64 framePointer = 0u;
    u32 callstackFrameCount = 0u;
    u64 callstackFrames[s_MaxCallstackFrames] = {};
    AStringView event;
    AStringView triggerCategory;
    AStringView triggerExpression;
    AStringView triggerMessage;
    AStringView triggerFile;
    AStringView gpuReport;
    AStringView gpuDump;
    u64 triggerInstructionPointer = 0u;
    u32 triggerLine = 0u;
    u32 callstackFramesToSkip = 0u;
};

struct ManualDumpContextStorage{
#if defined(NWB_PLATFORM_WINDOWS)
    CONTEXT context = {};
    EXCEPTION_RECORD exceptionRecord = {};
    EXCEPTION_POINTERS exceptionPointers = {};
#endif
};

struct CrashState{
    Futex mutex;
    bool installed = false;
    bool handlerStarted = false;
    CrashCapturePolicy capturePolicy;
    CrashSpoolRetentionConfig spoolRetention;
    DumpDetailMode::Enum dumpDetailMode = DumpDetailMode::Small;
    char applicationName[s_MaxShortText] = {};
    char versionText[s_MaxShortText] = {};
    char buildId[s_MaxMediumText] = {};
    char logServerUrl[s_MaxUrlText] = {};
    char crashUploadToken[s_MaxMediumText] = {};
    char spoolDirectoryText[s_MaxPathText] = {};
    char handlerExecutablePathText[s_MaxPathText] = {};
    FixedMetadata metadata[s_MaxMetadata] = {};
    FixedBreadcrumb breadcrumbs[s_MaxBreadcrumbs] = {};
    FixedDiagnosticSite diagnosticSites[s_MaxDiagnosticSites] = {};
    usize nextBreadcrumb = 0u;
    u32 diagnosticCaptureCount = 0u;
    Atomic<u64> breadcrumbOrder{ 1u };
    Atomic<u64> crashSequence{ 1u };
    Atomic<u32> suppressedPlatformCrashCaptures{ 0u };
    // Crash-transport channel guard: a single CAS claims the request pipe/event so concurrent faulting threads
    // never interleave their fixed-POD writes. Deliberately distinct from `mutex` (which the assert/diagnostic
    // path may already hold when a hard fault occurs), and an atomic (not a Futex) so it is signal/SEH-safe.
    Atomic<u32> transportInFlight{ 0u };

#if defined(NWB_PLATFORM_WINDOWS)
    HANDLE requestWriteHandle = INVALID_HANDLE_VALUE;
    HANDLE ackReadHandle = INVALID_HANDLE_VALUE;
    HANDLE crashHandledEvent = nullptr;
    PROCESS_INFORMATION handlerProcessInfo = {};
    LPTOP_LEVEL_EXCEPTION_FILTER previousExceptionFilter = nullptr;
#elif defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
    int requestWriteFd = -1;
    int ackReadFd = -1;
    pid_t handlerPid = -1;
    stack_t previousSignalStack = {};
#elif defined(NWB_PLATFORM_ANDROID)
    int emergencyWriteFd = -1;
    stack_t previousSignalStack = {};
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
    return ::Path<ArenaT>(arena, request.spoolDirectory) / PackageNames::s_PendingDirectoryName / request.crashId;
}

template<typename ArenaT>
[[nodiscard]] inline ::Path<ArenaT> RequestBucketDirectory(ArenaT& arena, const CrashRequest& request, const char* bucketName){
    return ::Path<ArenaT>(arena, request.spoolDirectory) / bucketName / request.crashId;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] const char* PlatformKindName(u32 platform)noexcept;
[[nodiscard]] const char* ReasonKindName(u32 reasonKind)noexcept;

void SnapshotCrashState(CrashRequest& outRequest, CrashReasonKind::Enum reasonKind, u32 reasonCode)noexcept;
void SuppressNextPlatformCrashCapture()noexcept;
[[nodiscard]] bool TryConsumeSuppressedPlatformCrashCapture()noexcept;
void CaptureManualDumpContext(CrashDumpRequestOptions& outOptions, ManualDumpContextStorage& storage)noexcept;
[[nodiscard]] CrashDumpResult RequestCrashDump(CrashReasonKind::Enum reasonKind, u32 reasonCode, const CrashDumpRequestOptions& options);
[[nodiscard]] CrashDumpTransportStatus::Enum RequestCrashHandler(const CrashRequest& request, u32 waitMilliseconds)noexcept;
void NotifyCrashHandler(CrashReasonKind::Enum reasonKind, u32 reasonCode, const CrashDumpRequestOptions& options = CrashDumpRequestOptions{})noexcept;

template<typename ArenaT>
[[nodiscard]] bool EnsureCrashSpoolDirectories(const ::Path<ArenaT>& spoolDirectory);
[[nodiscard]] Alloc::PersistentArena& DumpArena();

template<typename ArenaT>
[[nodiscard]] bool StartDesktopHandler(const ::Path<ArenaT>& handlerExecutablePath);
void InstallPlatformHandlers();
void UninstallPlatformResources();


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

