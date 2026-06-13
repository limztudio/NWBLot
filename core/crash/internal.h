// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"

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
inline constexpr u32 s_RequestVersion = 2u;
inline constexpr usize s_MaxMetadata = 32u;
inline constexpr usize s_MaxBreadcrumbs = 128u;
inline constexpr usize s_MaxGpuCrashProviders = 8u;
inline constexpr usize s_MaxShortText = 64u;
inline constexpr usize s_MaxMediumText = 256u;
inline constexpr usize s_MaxPathText = 1024u;
inline constexpr usize s_MaxUrlText = 1024u;


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

struct CrashRequest{
    u32 magic = s_RequestMagic;
    u32 version = s_RequestVersion;
    u32 platform = PlatformKind::Unknown;
    u32 reasonKind = CrashReasonKind::Unknown;
    u32 reasonCode = 0u;
    u32 processId = 0u;
    u32 threadId = 0u;
    u32 dumpDetailMode = DumpDetailMode::Small;
    u8 enableGpuDumps = 0u;
    char crashId[s_MaxShortText] = {};
    char applicationName[s_MaxShortText] = {};
    char versionText[s_MaxShortText] = {};
    char buildId[s_MaxMediumText] = {};
    char abi[s_MaxShortText] = {};
    char spoolDirectory[s_MaxPathText] = {};
    char logServerUrl[s_MaxUrlText] = {};
    u32 metadataCount = 0u;
    u32 breadcrumbCount = 0u;
    FixedMetadata metadata[s_MaxMetadata] = {};
    FixedBreadcrumb breadcrumbs[s_MaxBreadcrumbs] = {};
};

struct CrashDumpRequestOptions{
    u32 waitMilliseconds = 0u;
    bool writePackageInProcess = false;
    bool uploadImmediately = false;
};

struct CrashState{
    Futex mutex;
    bool installed = false;
    bool handlerStarted = false;
    bool enableGpuDumps = false;
    DumpDetailMode::Enum dumpDetailMode = DumpDetailMode::Small;
    char applicationName[s_MaxShortText] = {};
    char versionText[s_MaxShortText] = {};
    char buildId[s_MaxMediumText] = {};
    char logServerUrl[s_MaxUrlText] = {};
    char spoolDirectoryText[s_MaxPathText] = {};
    char handlerExecutablePathText[s_MaxPathText] = {};
    FixedMetadata metadata[s_MaxMetadata] = {};
    FixedBreadcrumb breadcrumbs[s_MaxBreadcrumbs] = {};
    GpuCrashProvider gpuProviders[s_MaxGpuCrashProviders] = {};
    usize nextBreadcrumb = 0u;
    usize gpuProviderCount = 0u;
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
[[nodiscard]] bool RequestCrashDump(CrashReasonKind::Enum reasonKind, u32 reasonCode, const CrashDumpRequestOptions& options);
[[nodiscard]] bool RequestCrashHandler(CrashReasonKind::Enum reasonKind, u32 reasonCode, u32 waitMilliseconds)noexcept;
void NotifyCrashHandler(CrashReasonKind::Enum reasonKind, u32 reasonCode)noexcept;

template<typename ArenaT>
[[nodiscard]] bool EnsureCrashSpoolDirectories(const ::Path<ArenaT>& spoolDirectory);
[[nodiscard]] Alloc::PersistentArena& DumpArena();
[[nodiscard]] bool WriteCrashPackage(const CrashRequest& request);
[[nodiscard]] bool UploadCrashPackage(const CrashRequest& request);
[[nodiscard]] bool FlushPendingCrashReportsImpl(Alloc::GlobalArena& arena);

template<typename ArenaT>
[[nodiscard]] bool StartDesktopHandler(const ::Path<ArenaT>& handlerExecutablePath);
void InstallPlatformHandlers();
void UninstallPlatformResources();


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

