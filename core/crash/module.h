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


struct CrashConfig{
    AStringView applicationName;
    AStringView buildId;
    AStringView version;
    ::Path<Alloc::PersistentArena> spoolDirectory;
    AStringView logServerUrl;
    AStringView crashUploadToken;
    AStringView handlerExecutablePath;
    CrashCapturePolicy capturePolicy;
    CrashSpoolRetentionConfig spoolRetention;
    DumpDetailMode::Enum dumpDetailMode = DumpDetailMode::Small;

    explicit CrashConfig(Alloc::PersistentArena& arena)
        : spoolDirectory(arena)
    {}
};


[[nodiscard]] bool InstallCrashHandler(Alloc::PersistentArena& arena, const CrashConfig& config);
void UninstallCrashHandler();

[[nodiscard]] bool SetCrashMetadata(AStringView key, AStringView value);
[[nodiscard]] bool AddCrashBreadcrumb(AStringView category, AStringView message);
[[nodiscard]] CrashDumpResult CaptureCrashDump(AStringView category = AStringView(), AStringView message = AStringView());

[[nodiscard]] ::Path<Alloc::PersistentArena> DefaultCrashSpoolDirectory(Alloc::PersistentArena& arena);
[[nodiscard]] ::Path<Alloc::PersistentArena> DefaultCrashHandlerExecutablePath(Alloc::PersistentArena& arena);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

