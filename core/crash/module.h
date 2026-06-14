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
        UploadFailed,
        Uploaded,
    };
};

struct CrashCapturePolicy{
    bool captureAssertions = true;
    bool captureFatalAssertions = true;
    bool captureLoggerErrors = true;
    bool captureLoggerFatals = true;
    u32 maxDiagnosticDumpsPerProcess = 8u;
    u32 maxDiagnosticDumpsPerSite = 1u;
};

struct CrashSpoolRetentionConfig{
    usize maxPendingPackages = 128u;
    usize maxUploadedPackages = 256u;
    usize maxFailedPackages = 64u;
    usize maxUploadingPackages = 64u;
};

struct CrashDumpResult{
    CrashDumpStatus::Enum status = CrashDumpStatus::NotInstalled;

    [[nodiscard]] bool requestAccepted()const{
        return status != CrashDumpStatus::NotInstalled && status != CrashDumpStatus::RequestFailed;
    }

    [[nodiscard]] bool packageWritten()const{
        return status == CrashDumpStatus::PackageWritten || status == CrashDumpStatus::UploadFailed || status == CrashDumpStatus::Uploaded;
    }

    [[nodiscard]] bool uploadAttempted()const{
        return status == CrashDumpStatus::UploadFailed || status == CrashDumpStatus::Uploaded;
    }

    [[nodiscard]] bool uploadSucceeded()const{
        return status == CrashDumpStatus::Uploaded;
    }
};


template<typename ArenaT>
struct CrashConfigT{
    AStringView applicationName;
    AStringView buildId;
    AStringView version;
    ::Path<ArenaT> spoolDirectory;
    AStringView logServerUrl;
    AStringView handlerExecutablePath;
    CrashCapturePolicy capturePolicy;
    CrashSpoolRetentionConfig spoolRetention;
    DumpDetailMode::Enum dumpDetailMode = DumpDetailMode::Small;
    bool enableGpuDumps = false;

    explicit CrashConfigT(ArenaT& arena)
        : spoolDirectory(arena)
    {}
};

using CrashConfig = CrashConfigT<Alloc::PersistentArena>;
using PersistentCrashConfig = CrashConfig;


using CrashPackagePath = ::Path<Alloc::PersistentArena>;

struct GpuCrashProvider{
    void* userData = nullptr;
    bool (*writeAttachment)(void* userData, const CrashPackagePath& packageDirectory, AStringView crashId) = nullptr;
};


template<typename ArenaT>
[[nodiscard]] bool InstallCrashHandler(ArenaT& arena, const CrashConfigT<ArenaT>& config);
void UninstallCrashHandler();

[[nodiscard]] bool SetCrashMetadata(AStringView key, AStringView value);
template<typename ArenaT>
[[nodiscard]] bool AddCrashBreadcrumb(ArenaT& arena, AStringView category, AStringView message);
[[nodiscard]] CrashDumpResult CaptureCrashDump(AStringView category = AStringView(), AStringView message = AStringView());
[[nodiscard]] bool FlushPendingCrashReports(Alloc::GlobalArena& arena);
[[nodiscard]] bool RegisterGpuCrashProvider(const GpuCrashProvider& provider);

template<typename ArenaT>
[[nodiscard]] ::Path<ArenaT> DefaultCrashSpoolDirectory(ArenaT& arena);
template<typename ArenaT>
[[nodiscard]] ::Path<ArenaT> DefaultCrashHandlerExecutablePath(ArenaT& arena);

int RunCrashHandlerProcess(isize argc, tchar** argv);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

