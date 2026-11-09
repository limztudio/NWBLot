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
        AttachmentWriteFailed,
    };
};

namespace GpuCrashDumpKind{
    enum Enum : u8{
        None,
        Aftermath,
        RadeonGpuDetective,
    };
};

inline constexpr u32 s_DefaultMaxDiagnosticDumpsPerProcess = 8u;
inline constexpr u32 s_DefaultMaxDiagnosticDumpsPerSite = 1u;
inline constexpr usize s_DefaultMaxPendingCrashPackages = 128u;
inline constexpr usize s_DefaultMaxUploadedCrashPackages = 256u;
inline constexpr usize s_DefaultMaxFailedCrashPackages = 64u;
inline constexpr usize s_DefaultMaxUploadingCrashPackages = 64u;

struct CrashCapturePolicy{
    b8 captureAssertions = true;
    b8 captureFatalAssertions = true;
    b8 captureLoggerErrors = true;
    b8 captureLoggerFatals = true;
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


public:
    [[nodiscard]] b8 requestAccepted()const noexcept{
        return status != CrashDumpStatus::NotInstalled && status != CrashDumpStatus::RequestFailed && status != CrashDumpStatus::AttachmentWriteFailed;
    }

    [[nodiscard]] b8 packageWritten()const noexcept{
        return status == CrashDumpStatus::PackageWritten;
    }
};


template<typename ArenaT>
struct CrashConfigT{
    // Text fields and paths must fit the crash snapshot and contain no embedded nulls.
    AStringView applicationName;
    AStringView buildId;
    AStringView version;
    ::Path<ArenaT> spoolDirectory;
    AStringView logServerUrl;
    AStringView crashUploadToken;
    AStringView handlerExecutablePath;
    CrashSpoolRetentionConfig spoolRetention;
    CrashCapturePolicy capturePolicy;
    DumpDetailMode::Enum dumpDetailMode = DumpDetailMode::Small;


public:
    explicit CrashConfigT(ArenaT& arena)noexcept(noexcept(::Path<ArenaT>(arena)))
        : spoolDirectory(arena)
    {}
};

using CrashConfig = CrashConfigT<Alloc::PersistentArena>;


template<typename ArenaT>
[[nodiscard]] ::Path<ArenaT> DefaultCrashSpoolDirectory(ArenaT& arena);
template<typename ArenaT>
[[nodiscard]] ::Path<ArenaT> DefaultCrashHandlerExecutablePath(ArenaT& arena);

template<typename ArenaT>
[[nodiscard]] b8 InstallCrashHandler(const CrashConfigT<ArenaT>& config);
// Hook registration/removal belongs to quiescent process startup/shutdown; native terminate/filter APIs provide no compare-exchange.
// Windows preserves detached upload completion after its bounded shutdown wait.
// POSIX alternate-stack restoration requires the installing thread; failed native restorations remain retriable.
void UninstallCrashHandler()noexcept;

// Rejects embedded nulls and text that does not fit the crash snapshot; failure keeps the prior value.
[[nodiscard]] b8 SetCrashMetadata(AStringView key, AStringView value)noexcept;
// Updates the crash-upload destination on an already-installed handler.
// This lets capture be armed first (before the CLI is parsed) and the upload destination supplied later, once it is known.
// Rejects embedded nulls and text that does not fit the crash snapshot without changing the destination.
[[nodiscard]] b8 SetCrashUploadDestination(AStringView logServerUrl, AStringView crashUploadToken)noexcept;
void AddCrashBreadcrumb(AStringView category, AStringView message)noexcept;
[[nodiscard]] CrashDumpResult CaptureCrashDump(AStringView category = AStringView(), AStringView message = AStringView());
// A nonempty binary dump requires a supported dump kind.
[[nodiscard]] CrashDumpResult CaptureGpuCrashDump(AStringView message, AStringView binaryDump = AStringView(), GpuCrashDumpKind::Enum dumpKind = GpuCrashDumpKind::Aftermath);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

