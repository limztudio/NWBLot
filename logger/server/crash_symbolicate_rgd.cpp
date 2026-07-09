// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "crash_symbolicate_internal.h"

#include <global/core/crash/package_names.h>

#include <nwb_rgd_decode.h>

#include <string>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_logger_crash_symbolicate{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CrashNames = ::NWB::Core::Crash::PackageNames;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// AMD Radeon GPU Detective decodes a '.rgd' GPU-crash capture that the engine writes into the package on a TDR.
// The decode runs in-process through the vendored RGD backend (nwb::rgd_backend), with no rgd.exe subprocess
// or external Radeon Developer Tool Suite install to resolve. It mirrors the Aftermath path: a
// best-effort decode that runs only when a capture is present in the package, appended under its own report
// section, and it never fails the surrounding ingest (ProcessCrashUpload rejects on a thrown exception, and
// DecodeCrashDumpToText contains all exceptions on its side of the boundary).
void AppendRadeonGpuDetectiveSummary(LogArena& arena, const Path& packageDirectory, const CrashSymbolicationConfig& config, CrashReportText& outReport){
    static_cast<void>(config);

    const Path rgdCapture = packageDirectory / CrashNames::s_GpuDetectiveCaptureFileName;
    if(!PathIsRegularFile(rgdCapture))
        return; // no Radeon GPU Detective capture in this package: nothing to decode (silent skip).

    const AString<LogArena> capturePath = PathToString<char>(arena, rgdCapture);

    std::string decoded;
    if(!nwb_rgd::DecodeCrashDumpToText(std::string(capturePath.data(), capturePath.size()), decoded)){
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

