// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <core/alloc/module.h>

#include <global/filesystem.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace DumpDetailMode{
    enum Enum : u8{
        Small,
        Full,
    };
};

namespace CrashDumpStatus{
    enum Enum : u8{
        NotInstalled,
        RequestFailed,
        RequestQueued,
        RequestTimedOut,
        PackageWriteFailed,
        PackageWritten,
    };
};

inline constexpr u32 s_DefaultMaxDiagnosticDumpsPerProcess = 8u;
inline constexpr u32 s_DefaultMaxDiagnosticDumpsPerSite = 1u;
inline constexpr usize s_DefaultMaxPendingCrashPackages = 128u;
inline constexpr usize s_DefaultMaxUploadedCrashPackages = 256u;
inline constexpr usize s_DefaultMaxFailedCrashPackages = 64u;
inline constexpr usize s_DefaultMaxUploadingCrashPackages = 64u;

struct CrashCapturePolicy{
    bool captureAssertions = true;
    bool captureFatalAssertions = true;
    bool captureLoggerErrors = true;
    bool captureLoggerFatals = true;
    u32 maxDiagnosticDumpsPerProcess = s_DefaultMaxDiagnosticDumpsPerProcess;
    u32 maxDiagnosticDumpsPerSite = s_DefaultMaxDiagnosticDumpsPerSite;
};

struct CrashSpoolRetentionConfig{
    usize maxPendingPackages = s_DefaultMaxPendingCrashPackages;
    usize maxUploadedPackages = s_DefaultMaxUploadedCrashPackages;
    usize maxFailedPackages = s_DefaultMaxFailedCrashPackages;
    usize maxUploadingPackages = s_DefaultMaxUploadingCrashPackages;
};

struct CrashDumpResult{
    CrashDumpStatus::Enum status = CrashDumpStatus::NotInstalled;

    [[nodiscard]] bool requestAccepted()const{
        return status != CrashDumpStatus::NotInstalled && status != CrashDumpStatus::RequestFailed;
    }

    [[nodiscard]] bool packageWritten()const{
        return status == CrashDumpStatus::PackageWritten;
    }
};


template<typename ArenaT>
struct CrashConfigT{
    AStringView applicationName;
    AStringView buildId;
    AStringView version;
    ::Path<ArenaT> spoolDirectory;
    AStringView logServerUrl;
    AStringView crashUploadToken;
    AStringView handlerExecutablePath;
    CrashCapturePolicy capturePolicy;
    CrashSpoolRetentionConfig spoolRetention;
    DumpDetailMode::Enum dumpDetailMode = DumpDetailMode::Small;

    explicit CrashConfigT(ArenaT& arena)
        : spoolDirectory(arena)
    {}
};

using CrashConfig = CrashConfigT<Alloc::PersistentArena>;


template<typename ArenaT>
[[nodiscard]] bool InstallCrashHandler(ArenaT& arena, const CrashConfigT<ArenaT>& config);
void UninstallCrashHandler();

[[nodiscard]] bool SetCrashMetadata(AStringView key, AStringView value);
[[nodiscard]] bool AddCrashBreadcrumb(AStringView category, AStringView message);
[[nodiscard]] CrashDumpResult CaptureCrashDump(AStringView category = AStringView(), AStringView message = AStringView());

template<typename ArenaT>
[[nodiscard]] ::Path<ArenaT> DefaultCrashSpoolDirectory(ArenaT& arena);
template<typename ArenaT>
[[nodiscard]] ::Path<ArenaT> DefaultCrashHandlerExecutablePath(ArenaT& arena);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

