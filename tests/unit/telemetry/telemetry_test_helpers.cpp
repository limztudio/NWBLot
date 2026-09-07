// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "telemetry_test_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TelemetryTestDetail{


::Path<NWB::Core::Alloc::GlobalArena> TelemetryTestStorageDirectory(NWB::Core::Alloc::GlobalArena& arena){
    ::Path<NWB::Core::Alloc::GlobalArena> executableDirectory(arena);
    if(GetExecutableDirectory(executableDirectory))
        return executableDirectory / "telemetry_test_storage";

    return ::Path<NWB::Core::Alloc::GlobalArena>(arena, "telemetry_test_storage");
}

NWB::Core::Perf::TimingStats MakeTestTimingStats(){
    NWB::Core::Perf::TimingStats stats;
    stats.seconds = 0.125;
    stats.sampleCount = 3u;
    stats.publishFrameIndex = 77u;
    stats.firstSampleFrameIndex = 70u;
    stats.lastSampleFrameIndex = 76u;
    return stats;
}

NWB::Core::Perf::MemorySnapshot MakeTestMemorySnapshot(const Name& scopeName){
    NWB::Core::Perf::MemorySnapshot snapshot;
    snapshot.scopeName = scopeName;
    snapshot.frameIndex = 88u;
    snapshot.reservedBytes = 4096u;
    snapshot.usedBytes = 1536u;
    snapshot.peakUsedBytes = 2048u;
    snapshot.allocationCount = 7u;
    snapshot.reallocationCount = 2u;
    snapshot.deallocationCount = 1u;
    return snapshot;
}

NWB::Core::Perf::MemoryDelta MakeTestMemoryDelta(){
    NWB::Core::Perf::MemoryDelta delta;
    delta.previousFrameIndex = 87u;
    delta.currentFrameIndex = 88u;
    delta.reservedBytes = 512;
    delta.usedBytes = -128;
    delta.peakUsedBytes = 256;
    delta.allocationCount = 2;
    delta.reallocationCount = 1;
    delta.deallocationCount = 0;
    delta.hasSamples = true;
    return delta;
}

::ArenaMemoryStats MakeTestArenaStats(
    const u64 reservedBytes,
    const u64 usedBytes,
    const u64 peakUsedBytes,
    const u64 allocationCount,
    const u64 reallocationCount,
    const u64 deallocationCount
){
    ::ArenaMemoryStats stats;
    stats.reservedBytes = reservedBytes;
    stats.usedBytes = usedBytes;
    stats.peakUsedBytes = peakUsedBytes;
    stats.allocationCount = allocationCount;
    stats.reallocationCount = reallocationCount;
    stats.deallocationCount = deallocationCount;
    return stats;
}

void BuildTestPerfReport(
    NWB::Core::Perf::TimingRecorder& cpuTiming,
    NWB::Core::Perf::TimingRecorder& gpuTiming,
    NWB::Core::Perf::MemoryRecorder& memory,
    NWB::Core::Perf::SessionReport& report
){
    cpuTiming.setEnabled(true);
    gpuTiming.setEnabled(true);
    memory.setEnabled(true);

    const Name cpuScopeName("perf/cpu/update");
    const Name gpuScopeName("perf/gpu/frame");
    const Name memoryScopeName("perf/memory/project");
    const NWB::Core::Perf::TimingScopeId cpuScope = cpuTiming.registerScope(cpuScopeName);
    const NWB::Core::Perf::TimingScopeId gpuScope = gpuTiming.registerScope(gpuScopeName);
    const NWB::Core::Perf::MemoryScopeId memoryScope = memory.registerScope(memoryScopeName);

    cpuTiming.recordSample(cpuScope, 0.010, 100u);
    cpuTiming.recordSample(cpuScope, 0.015, 101u);
    cpuTiming.publishFrame(102u);

    gpuTiming.recordSample(gpuScope, 0.020, 100u);
    gpuTiming.publishFrame(102u);

    memory.recordSnapshot(memoryScope, MakeTestArenaStats(4096u, 1024u, 1536u, 4u, 1u, 0u), 101u);
    memory.recordSnapshot(memoryScope, MakeTestArenaStats(8192u, 2048u, 3072u, 7u, 2u, 1u), 102u);

    report.capture = NWB::Core::Perf::CaptureOptions::All();
    report.frameIndex = 102u;
    report.cpuTiming = NWB::Core::Perf::TimingView(cpuTiming);
    report.gpuTiming = NWB::Core::Perf::TimingView(gpuTiming);
    report.memory = NWB::Core::Perf::MemoryView(memory);
}

bool ContainsText(const AStringView text, const AStringView needle){
    return text.find(needle) != AStringView::npos;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

