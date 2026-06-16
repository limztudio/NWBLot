// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "project_entry.h"

#include <core/graphics/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ProjectRuntimeContext::setGpuTimingEnabled(const bool enabled){
    setPerfCapture(enabled ? Core::Perf::CaptureOptions::GpuTimingOnly() : Core::Perf::CaptureOptions::Disabled());
}

void ProjectRuntimeContext::setPerfCapture(const Core::Perf::CaptureOptions& options){
    if(perfCapture)
        perfCapture(options);
}

bool ProjectRuntimeContext::flushPerfSamples(){
    return perfSampleFlush ? perfSampleFlush() : false;
}

Core::Perf::SessionReport ProjectRuntimeContext::perfReport()const{
    return perfReportReader ? perfReportReader() : Core::Perf::SessionReport{};
}

void ProjectRuntimeContext::setTelemetryCapture(const Core::Telemetry::CaptureOptions& options){
    if(telemetryCapture)
        telemetryCapture(options);
}

void ProjectRuntimeContext::setTelemetryArchivePath(const Core::Telemetry::TelemetryPath& path){
    if(telemetryArchivePath)
        telemetryArchivePath(path);
}

void ProjectRuntimeContext::setTelemetryArchiveOptions(const Core::Telemetry::ArchiveSinkOptions& options){
    if(telemetryArchiveOptions)
        telemetryArchiveOptions(options);
}

Core::Telemetry::ArchiveResult ProjectRuntimeContext::flushTelemetryArchive(const bool clearAfterWrite){
    if(telemetryArchiveFlush)
        return telemetryArchiveFlush(clearAfterWrite);

    Core::Telemetry::ArchiveResult result;
    result.status = Core::Telemetry::ArchiveStatus::Disabled;
    return result;
}

bool ProjectRuntimeContext::flushTelemetryUpload(const bool clearAfterUpload){
    return telemetryUploadFlush ? telemetryUploadFlush(clearAfterUpload) : false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

