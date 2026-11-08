// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <core/crash/crash_types.adhd.h>

#include <core/alloc/module.h>

#include <global/filesystem.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace DumpDetailMode{
    enum Enum : u8{
        Small = NWB::Core::Crash::CrashTypes::DumpDetailMode::Small,
        Full = NWB::Core::Crash::CrashTypes::DumpDetailMode::Full,
    };
};

namespace CrashDumpStatus{
    enum Enum : u8{
        NotInstalled = NWB::Core::Crash::CrashTypes::CrashDumpStatus::NotInstalled,
        RequestFailed = NWB::Core::Crash::CrashTypes::CrashDumpStatus::RequestFailed,
        RequestQueued = NWB::Core::Crash::CrashTypes::CrashDumpStatus::RequestQueued,
        RequestTimedOut = NWB::Core::Crash::CrashTypes::CrashDumpStatus::RequestTimedOut,
        PackageWriteFailed = NWB::Core::Crash::CrashTypes::CrashDumpStatus::PackageWriteFailed,
        PackageWritten = NWB::Core::Crash::CrashTypes::CrashDumpStatus::PackageWritten,
        AttachmentWriteFailed = NWB::Core::Crash::CrashTypes::CrashDumpStatus::AttachmentWriteFailed,
    };
};

namespace GpuCrashDumpKind{
    enum Enum : u8{
        None = NWB::Core::Crash::CrashTypes::GpuCrashDumpKind::None,
        Aftermath = NWB::Core::Crash::CrashTypes::GpuCrashDumpKind::Aftermath,
        RadeonGpuDetective = NWB::Core::Crash::CrashTypes::GpuCrashDumpKind::RadeonGpuDetective,
    };
};

inline constexpr u32 s_DefaultMaxDiagnosticDumpsPerProcess = NWB::Core::Crash::CrashTypes::s_DefaultMaxDiagnosticDumpsPerProcess;
inline constexpr u32 s_DefaultMaxDiagnosticDumpsPerSite = NWB::Core::Crash::CrashTypes::s_DefaultMaxDiagnosticDumpsPerSite;
inline constexpr usize s_DefaultMaxPendingCrashPackages = NWB::Core::Crash::CrashTypes::s_DefaultMaxPendingCrashPackages;
inline constexpr usize s_DefaultMaxUploadedCrashPackages = NWB::Core::Crash::CrashTypes::s_DefaultMaxUploadedCrashPackages;
inline constexpr usize s_DefaultMaxFailedCrashPackages = NWB::Core::Crash::CrashTypes::s_DefaultMaxFailedCrashPackages;
inline constexpr usize s_DefaultMaxUploadingCrashPackages = NWB::Core::Crash::CrashTypes::s_DefaultMaxUploadingCrashPackages;

using CrashCapturePolicy = NWB::Core::Crash::CrashTypes::CrashCapturePolicy;
using CrashSpoolRetentionConfig = NWB::Core::Crash::CrashTypes::CrashSpoolRetentionConfig;

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

