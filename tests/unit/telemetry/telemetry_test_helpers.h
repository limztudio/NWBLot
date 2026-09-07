// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <tests/common/test_context.h>
#include <core/telemetry/module.h>
#include <logger/telemetry/report.h>
#include <global/filesystem/operations.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TelemetryTestDetail{


using TestArena = NWB::Tests::TestArena<struct TelemetryTestsTag>;
namespace Telemetry = NWB::Core::Telemetry;
namespace Log = NWB::Log;

::Path<NWB::Core::Alloc::GlobalArena> TelemetryTestStorageDirectory(NWB::Core::Alloc::GlobalArena& arena);

NWB::Core::Perf::TimingStats MakeTestTimingStats();

NWB::Core::Perf::MemorySnapshot MakeTestMemorySnapshot(const Name& scopeName);

NWB::Core::Perf::MemoryDelta MakeTestMemoryDelta();

::ArenaMemoryStats MakeTestArenaStats(
    const u64 reservedBytes,
    const u64 usedBytes,
    const u64 peakUsedBytes,
    const u64 allocationCount,
    const u64 reallocationCount,
    const u64 deallocationCount
);

void BuildTestPerfReport(
    NWB::Core::Perf::TimingRecorder& cpuTiming,
    NWB::Core::Perf::TimingRecorder& gpuTiming,
    NWB::Core::Perf::MemoryRecorder& memory,
    NWB::Core::Perf::SessionReport& report
);

bool ContainsText(const AStringView text, const AStringView needle);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

