// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "crash_symbolicate_internal.h"

#include <core/crash/package_names.h>

#include <nwb_rgd_decode.h>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace LoggerCrashSymbolicateDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CrashNames = ::NWB::Core::Crash::PackageNames;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// RGD decodes an in-package '.rgd' capture in-process through the vendored backend (no subprocess or external
// install). Best-effort like Aftermath: runs only when present, appends its own section, never fails ingest.
void AppendRadeonGpuDetectiveSummary(LogArena& arena, const Path& packageDirectory, const CrashSymbolicationConfig& config, CrashReportText& outReport){
    static_cast<void>(config);

    const Path rgdCapture = packageDirectory / CrashNames::s_GpuDetectiveCaptureFileName;
    if(!PathIsRegularFile(rgdCapture))
        return; // no Radeon GPU Detective capture in this package: nothing to decode (silent skip).

    const AString<LogArena> capturePath = PathToString<char>(arena, rgdCapture);

    AInteropString decoded;
    if(!nwb_rgd::DecodeCrashDumpToText(AInteropString(capturePath.data(), capturePath.size()), decoded)){
        outReport += "\n[gpu_detective]\nstatus=decode_failed\n";
        if(!decoded.empty()){
            outReport += "detail=";
            outReport.append(decoded.data(), decoded.size());
            outReport.push_back('\n');
        }
        return;
    }

    outReport += "\n[gpu_detective]\n";
    outReport.append(decoded.data(), decoded.size());
    if(decoded.empty() || decoded.back() != '\n')
        outReport.push_back('\n');
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

