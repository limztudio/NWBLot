// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "telemetry_test_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TelemetryTestDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

NWB::Core::Perf::MemoryDelta MakeTestMemoryDelta(const i64 deallocationCount){
    NWB::Core::Perf::MemoryDelta delta;
    delta.previousFrameIndex = 87u;
    delta.currentFrameIndex = 88u;
    delta.reservedBytes = 512;
    delta.usedBytes = -128;
    delta.peakUsedBytes = 256;
    delta.allocationCount = 2;
    delta.reallocationCount = 1;
    delta.deallocationCount = deallocationCount;
    delta.hasSamples = true;
    return delta;
}

bool ContainsText(const AStringView text, const AStringView needle){
    return text.find(needle) != AStringView::npos;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

