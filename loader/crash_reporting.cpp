// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "crash_reporting.h"

#include <loader/runtime_constants.adhd.h>

#include <core/common/log.h>
#include <core/crash/module.h>
#include <core/crash/package_names.h>
#include <core/graphics/rhi/gpu_crash_diagnostics.h>

#include <global/filesystem/executable_paths.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


namespace LoaderDetail{


using CrashArena = Core::Alloc::PersistentArena;


b8 CrashReporting::Install(CrashArena& crashArena){
    auto executableDirectory = ExecutablePaths(crashArena).directory();
    if(!executableDirectory)
        executableDirectory = ::Path<CrashArena>(crashArena, s_FallbackExecutableDirectory);

    auto executableName = ExecutablePaths(crashArena).name();
    if(!executableName)
        executableName = ::Path<CrashArena>(crashArena, s_FallbackExecutableName);

    AString<CrashArena> applicationName = PathToString<char>(crashArena, *executableName);

    NWB::Core::Crash::CrashConfig crashConfig(crashArena);
    crashConfig.applicationName = AStringView(applicationName.data(), applicationName.size());
    crashConfig.buildId = s_UnknownBuildLabel;
    crashConfig.version = s_UnknownBuildLabel;
    crashConfig.spoolDirectory = *executableDirectory / NWB::Core::Crash::PackageNames::s_DefaultRootDirectoryName;
    crashConfig.dumpDetailMode = NWB::Core::Crash::DumpDetailMode::Small;

    if(!NWB::Core::Crash::InstallCrashHandler(crashConfig))
        return false;

    NWB::Core::RegisterGpuCrashSink([](void*, const NWB::Core::GpuCrashReport& report){
        const AStringView binaryDump = report.binaryDump ? AStringView(reinterpret_cast<const char*>(report.binaryDump), report.binaryDumpSize) : AStringView();
        const auto dumpKind = report.binaryDumpKind == NWB::Core::GpuCrashDumpKind::RadeonGpuDetective ? NWB::Core::Crash::GpuCrashDumpKind::RadeonGpuDetective : NWB::Core::Crash::GpuCrashDumpKind::Aftermath;
        const NWB::Core::Crash::CrashDumpResult dumpResult = NWB::Core::Crash::CaptureGpuCrashDump(AStringView(report.details.data(), report.details.size()), binaryDump, dumpKind);
        if(!dumpResult.requestAccepted() || dumpResult.status == NWB::Core::Crash::CrashDumpStatus::PackageWriteFailed){
            NWB_LOGGER_WARNING(FOUNDATION_TEXT("Loader: GPU crash dump capture failed (status {})"), static_cast<u32>(dumpResult.status));
        }
    }, nullptr
    );
    return true;
}

void CrashReporting::Configure(const b8 crashReportingInstalled, const b8 useStandaloneLogger, const AStringView logAddress, const AStringView crashUploadToken, const b8 enableGpuDebug)noexcept(!FOUNDATION_OCCUR_WARNING){
    if(!crashReportingInstalled){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Loader: crash reporting unavailable - ERROR/FATAL/crash dumps will NOT be captured this run"));
        return;
    }

    const AStringView logServerUrl = useStandaloneLogger ? AStringView() : logAddress;
    if(!NWB::Core::Crash::SetCrashUploadDestination(logServerUrl, crashUploadToken))
        NWB_LOGGER_WARNING(FOUNDATION_TEXT("Loader: failed to set crash upload destination"));
    if(!NWB::Core::Crash::SetCrashMetadata(s_RuntimeCrashMetadataKey, s_LoaderCrashMetadataValue))
        NWB_LOGGER_WARNING(FOUNDATION_TEXT("Loader: failed to set 'runtime' crash metadata"));
    if(!NWB::Core::Crash::SetCrashMetadata(s_GpuDebugCrashMetadataKey, enableGpuDebug ? s_EnabledText : s_DisabledText))
        NWB_LOGGER_WARNING(FOUNDATION_TEXT("Loader: failed to set 'gpu_debug' crash metadata"));
}



};


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

