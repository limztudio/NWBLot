// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "crash_symbolicate_internal.h"

#include <core/crash/package_names.h>

#include <logger/rgd/decode.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace LoggerCrashSymbolicateDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CrashNames = ::NWB::Core::Crash::PackageNames;

inline constexpr AStringView s_GpuDetectiveSectionHeader = "\n[gpu_detective]\n";
inline constexpr AStringView s_GpuDetectiveDecodeFailedStatus = "status=decode_failed\n";
inline constexpr AStringView s_ReportDetailPrefix = "detail=";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Expected decode failures retain the ingest report; unexpected exceptions remain terminal.
void AppendRadeonGpuDetectiveSummary(Core::Alloc::ScratchArena& arena, const CrashScratchPath& packageDirectory, const CrashSymbolicationConfig& config, CrashReportText& outReport){
    static_cast<void>(config);

    const CrashScratchPath rgdCapture = packageDirectory / CrashNames::s_GpuDetectiveCaptureFileName;
    const auto regularCapture = IsRegularFile(rgdCapture);
    if(!regularCapture){
        NWB_LOGGER_WARNING(NWB_TEXT("Log server: failed to inspect Radeon GPU Detective capture '{}': {}"), PathToString<tchar>(rgdCapture), StringConvert(regularCapture.error().message()));
        outReport += s_GpuDetectiveSectionHeader;
        outReport += "status=inspect_failed\n";
        return;
    }
    if(!*regularCapture)
        return; // An absent capture has no decode work.

    const AString<Core::Alloc::ScratchArena> capturePath = PathToString<char>(arena, rgdCapture);

    const auto decoded = Rgd::DecodeCrashDumpToText(AInteropString(capturePath.data(), capturePath.size()));
    if(!decoded){
        outReport += s_GpuDetectiveSectionHeader;
        outReport += s_GpuDetectiveDecodeFailedStatus;
        const StringView detail = decoded.error();
        if(!detail.empty()){
            outReport += s_ReportDetailPrefix;
            outReport.append(detail.data(), detail.size());
            outReport.push_back('\n');
        }
        return;
    }

    outReport += s_GpuDetectiveSectionHeader;
    outReport.append(decoded->data(), decoded->size());
    if(decoded->empty() || decoded->back() != '\n')
        outReport.push_back('\n');
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

