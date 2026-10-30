// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <tests/common/test_context.h>

#include <core/alloc/scratch.h>
#include <core/telemetry/codec.h>
#include <core/telemetry/diagnostic.h>
#include <core/telemetry/event.h>
#include <core/telemetry/frame_graph.h>
#include <core/telemetry/perf.h>
#include <core/telemetry/recorder.h>
#include <core/telemetry/session.h>
#include <core/telemetry/text_log.h>

#include <logger/telemetry/report.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TelemetryTestDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;

inline constexpr Name s_TelemetryTestScratch("tests/telemetry/scratch");

struct TestArena : NWB::Tests::TestArena<struct TelemetryTestsTag>{
    NWB::Core::Alloc::ScratchArena scratchArena{ s_TelemetryTestScratch };
};
namespace Telemetry = NWB::Core::Telemetry;
namespace Log = NWB::Log;

NWB::Core::Perf::TimingStats MakeTestTimingStats()noexcept;

NWB::Core::Perf::MemorySnapshot MakeTestMemorySnapshot(const Name& scopeName)noexcept;

NWB::Core::Perf::MemoryDelta MakeTestMemoryDelta(const i64 deallocationCount = 0)noexcept;

b8 ContainsText(const AStringView text, const AStringView needle)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

