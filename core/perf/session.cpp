// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "session.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_PERF_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void Session::setCaptureOptions(const CaptureOptions& options){
    m_enabled = options.enabled;
    m_cpuTimingEnabled = options.cpuTiming;
    m_gpuTimingEnabled = options.gpuTiming;
    m_memoryEnabled = options.memory;
    applyEnabledState();
}

void Session::clear(){
    m_cpuTiming.clear();
    m_gpuTiming.clear();
    m_memory.clear();
    m_frameIndex = 0u;
}

void Session::beginFrame(const u64 frameIndex){
    m_frameIndex = frameIndex;
}

void Session::publishFrame(){
    m_cpuTiming.publishFrame(m_frameIndex);
}

CaptureOptions Session::captureOptions()const{
    CaptureOptions options;
    options.enabled = m_enabled;
    options.cpuTiming = m_cpuTimingEnabled;
    options.gpuTiming = m_gpuTimingEnabled;
    options.memory = m_memoryEnabled;
    return options;
}

SessionReport Session::report()const{
    SessionReport report;
    report.capture = captureOptions();
    report.frameIndex = m_frameIndex;
    report.cpuTiming = cpuTimingView();
    report.gpuTiming = gpuTimingView();
    report.memory = memoryView();
    return report;
}

MemoryScopeId Session::registerMemoryScope(const Name& scopeName){
    return m_memory.registerScope(scopeName);
}

void Session::applyEnabledState(){
    const CaptureOptions capture = captureOptions();
    m_cpuTiming.setEnabled(capture.cpuTimingActive());
    m_gpuTiming.setEnabled(capture.gpuTimingActive());
    m_memory.setEnabled(capture.memoryActive());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_PERF_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

