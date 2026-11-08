// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "crash_symbolicate_internal.h"

#include <logger/server/crash_values.adhd.h>

#include <core/crash/package_names.adhd.h>

#include <logger/rgd/decode.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace LoggerCrashSymbolicateDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CrashNames = ::NWB::Core::Crash::PackageNames;

inline constexpr AStringView s_GpuDetectiveSectionHeader = ::NWB::Log::CrashValues::s_CrashSymbolicateRgdGpuDetectiveSectionHeader;
inline constexpr AStringView s_GpuDetectiveDecodeFailedStatus = ::NWB::Log::CrashValues::s_CrashSymbolicateRgdGpuDetectiveDecodeFailedStatus;
inline constexpr AStringView s_ReportDetailPrefix = ::NWB::Log::CrashValues::s_CrashSymbolicateRgdReportDetailPrefix;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Expected decode failures retain the ingest report; unexpected exceptions remain terminal.
void AppendRadeonGpuDetectiveSummary(const CrashScratchPath& packageDirectory, Core::Alloc::ScratchArena& arena, CrashReportText& outReport){
    const CrashScratchPath rgdCapture = packageDirectory / CrashNames::s_GpuDetectiveCaptureFileName;
    const auto regularCapture = IsRegularFile(rgdCapture);
    if(!regularCapture){
        NWB_LOGGER_WARNING(NWB_TEXT("Log server: failed to inspect Radeon GPU Detective capture '{}': {}"), PathToString<tchar>(rgdCapture), ConvertString(regularCapture.error().message()));
        outReport += s_GpuDetectiveSectionHeader;
        outReport += "status=inspect_failed\n";
        return;
    }
    if(!*regularCapture)
        return; // An absent capture has no decode work.

    const AString<Core::Alloc::ScratchArena> capturePath = PathToString<char>(arena, rgdCapture);

    const auto decoded = Rgd::DecodeCrashDumpToText(capturePath);
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

