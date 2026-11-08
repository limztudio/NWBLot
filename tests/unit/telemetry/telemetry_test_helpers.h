// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <tests/common/test_context.h>
#include <tests/unit/telemetry/perf_case_types.adhd.h>

#include <logger/telemetry/report.h>

#include <core/alloc/scratch.h>
#include <core/telemetry/codec.h>
#include <core/telemetry/diagnostic.h>
#include <core/telemetry/event.h>
#include <core/telemetry/frame_graph.h>
#include <core/telemetry/perf.h>
#include <core/telemetry/recorder.h>
#include <core/telemetry/session.h>
#include <core/telemetry/text_log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TelemetryTestDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;

inline constexpr Name s_TelemetryTestScratch = NWB::Tests::TelemetryCases::s_TelemetryScratch;

struct TestArena : NWB::Tests::TestArena<struct TelemetryTestsTag>{
    NWB::Core::Alloc::ScratchArena scratchArena{ s_TelemetryTestScratch };
};
namespace Telemetry = NWB::Core::Telemetry;
namespace Log = NWB::Log;

NWB::Core::Perf::TimingStats MakeTestTimingStats()noexcept;

NWB::Core::Perf::MemorySnapshot MakeTestMemorySnapshot(const Name& scopeName)noexcept;

NWB::Core::Perf::MemoryDelta MakeTestMemoryDelta(const i64 deallocationCount = 0)noexcept;

Telemetry::EncodedFrameGraphRuntimeStatistics EncodeTestFrameGraphRuntimeStatistics(const Telemetry::FrameGraphRuntimeStatistics& statistics, const u32 nodeIndex, const u16 reserved)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

