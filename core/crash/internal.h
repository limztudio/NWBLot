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
inline constexpr u32 s_RequestVersion = 1u;
inline constexpr usize s_MaxMetadata = 32u;
inline constexpr usize s_MaxBreadcrumbs = 128u;
inline constexpr usize s_MaxGpuCrashProviders = 8u;
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
inline constexpr tchar s_AckEventArgument[] = NWB_TEXT("--ack-event");
inline constexpr const char* s_RequestFdArgument = "--request-fd";
inline constexpr const char* s_DefaultBreadcrumbCategory = "general";
inline constexpr const char* s_ManualDumpCategory = "manual_dump";


template<typename ArenaT>
using CrashStringT = AString<ArenaT>;
template<typename ArenaT>
using CrashBytesT = Vector<u8, ArenaT>;
using CrashString = CrashStringT<Alloc::GlobalArena>;
using CrashBytes = CrashBytesT<Alloc::GlobalArena>;


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
    };
};

namespace CrashUploadPolicy{
    enum Enum : u32{
        None,
        ImmediateAfterWrite,
    };
};

namespace CrashDumpTransportStatus{
    enum Enum : u8{
        Failed,
        Sent,
        Acknowledged,
        TimedOut,
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
    u32 uploadPolicy = CrashUploadPolicy::ImmediateAfterWrite;
    CrashSpoolRetentionConfig spoolRetention;
    u64 exceptionPointers = 0u;
    u64 faultAddress = 0u;
    u64 instructionPointer = 0u;
    u64 stackPointer = 0u;
    u64 framePointer = 0u;
    u32 triggerLine = 0u;
    u32 callstackFrameCount = 0u;
    u32 callstackFramesToSkip = 0u;
    u8 enableGpuDumps = 0u;
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

struct CrashUploadSnapshot{
    CrashSpoolRetentionConfig spoolRetention;
    char spoolDirectory[s_MaxPathText] = {};
    char logServerUrl[s_MaxUrlText] = {};
    char crashUploadToken[s_MaxMediumText] = {};
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
    u64 triggerInstructionPointer = 0u;
    u32 triggerLine = 0u;
    u32 callstackFramesToSkip = 0u;
    bool writePackageInProcess = false;
    bool uploadAfterWrite = true;
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
    bool enableGpuDumps = false;
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
    GpuCrashProvider gpuProviders[s_MaxGpuCrashProviders] = {};
    usize nextBreadcrumb = 0u;
    usize gpuProviderCount = 0u;
    u32 diagnosticCaptureCount = 0u;
    Atomic<u64> breadcrumbOrder{ 1u };
    Atomic<u64> crashSequence{ 1u };

#if defined(NWB_PLATFORM_WINDOWS)
    HANDLE requestWriteHandle = INVALID_HANDLE_VALUE;
    HANDLE crashHandledEvent = nullptr;
    PROCESS_INFORMATION handlerProcessInfo = {};
    LPTOP_LEVEL_EXCEPTION_FILTER previousExceptionFilter = nullptr;
#elif defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
    int requestWriteFd = -1;
    pid_t handlerPid = -1;
    stack_t previousSignalStack = {};
#elif defined(NWB_PLATFORM_ANDROID)
    int emergencyWriteFd = -1;
    stack_t previousSignalStack = {};
#endif
};


extern CrashState g_State;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] const char* PlatformKindName(u32 platform)noexcept;
[[nodiscard]] const char* ReasonKindName(u32 reasonKind)noexcept;

void SnapshotCrashState(CrashRequest& outRequest, CrashReasonKind::Enum reasonKind, u32 reasonCode)noexcept;
void CaptureManualDumpContext(CrashDumpRequestOptions& outOptions, ManualDumpContextStorage& storage)noexcept;
[[nodiscard]] CrashDumpResult RequestCrashDump(CrashReasonKind::Enum reasonKind, u32 reasonCode, const CrashDumpRequestOptions& options);
[[nodiscard]] CrashDumpTransportStatus::Enum RequestCrashHandler(const CrashRequest& request, u32 waitMilliseconds)noexcept;
void NotifyCrashHandler(CrashReasonKind::Enum reasonKind, u32 reasonCode, const CrashDumpRequestOptions& options = CrashDumpRequestOptions{})noexcept;

template<typename ArenaT>
[[nodiscard]] bool EnsureCrashSpoolDirectories(const ::Path<ArenaT>& spoolDirectory);
[[nodiscard]] Alloc::PersistentArena& DumpArena();
[[nodiscard]] bool WriteCrashPackage(const CrashRequest& request);
[[nodiscard]] bool UploadCrashPackage(const CrashRequest& request);
[[nodiscard]] CrashDumpResult CrashPackageResult(const CrashRequest& request);
[[nodiscard]] bool ApplyCrashSpoolRetention(
    Alloc::GlobalArena& arena,
    const ::Path<Alloc::GlobalArena>& spoolDirectory,
    const CrashSpoolRetentionConfig& retention,
    AStringView protectedPendingPackageName = AStringView()
);
[[nodiscard]] bool FlushPendingCrashReportsImpl(Alloc::GlobalArena& arena, const CrashUploadSnapshot& snapshot);

template<typename ArenaT>
[[nodiscard]] bool StartDesktopHandler(const ::Path<ArenaT>& handlerExecutablePath);
void InstallPlatformHandlers();
void UninstallPlatformResources();


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

