// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/test_context.h>

#include <core/telemetry/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestContext = NWB::Tests::TestContext;
using TestArena = NWB::Tests::TestArena<struct TelemetryTestsTag>;
namespace Telemetry = NWB::Core::Telemetry;


#define NWB_TELEMETRY_TEST_CHECK NWB_TEST_CHECK


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void TestCaptureFlags(TestContext& context){
    constexpr Telemetry::CaptureOptions disabled = Telemetry::CaptureOptions::Disabled();
    constexpr Telemetry::CaptureOptions frameGraph = Telemetry::CaptureOptions::FrameGraphOnly();
    constexpr Telemetry::CaptureOptions perf = Telemetry::CaptureOptions::PerfOnly();
    constexpr Telemetry::CaptureOptions all = Telemetry::CaptureOptions::All();

    static_assert(!disabled.enabled());
    static_assert(frameGraph.enabled());
    static_assert(frameGraph.frameGraphEnabled());
    static_assert(!frameGraph.perfEnabled());
    static_assert(perf.perfEnabled());
    static_assert(all.textLogEnabled());
    static_assert(all.diagnosticEnabled());
    static_assert(all.crashEnabled());
    static_assert(all.perfEnabled());
    static_assert(all.frameGraphEnabled());
    static_assert(all.customEnabled());

    NWB_TELEMETRY_TEST_CHECK(context, Telemetry::CaptureAllowsEventKind(all, Telemetry::EventKind::TextLog));
    NWB_TELEMETRY_TEST_CHECK(context, Telemetry::CaptureAllowsEventKind(all, Telemetry::EventKind::FrameGraphFrame));
    NWB_TELEMETRY_TEST_CHECK(context, !Telemetry::CaptureAllowsEventKind(frameGraph, Telemetry::EventKind::PerfFrame));
    NWB_TELEMETRY_TEST_CHECK(context, Telemetry::CaptureAllowsEventKind(frameGraph, Telemetry::EventKind::FrameGraphFrame));
}

static void TestRecorderFiltersAndCopiesPayload(TestContext& context){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::FrameGraphOnly());

    const u8 perfPayload[] = { 1u, 2u };
    NWB_TELEMETRY_TEST_CHECK(context, !recorder.recordBinary(Telemetry::EventKind::PerfFrame, 12u, perfPayload, sizeof(perfPayload)));
    NWB_TELEMETRY_TEST_CHECK(context, recorder.eventCount() == 0u);

    u8 frameGraphPayload[] = { 4u, 5u, 6u };
    NWB_TELEMETRY_TEST_CHECK(context, recorder.recordBinary(Telemetry::EventKind::FrameGraphFrame, 13u, frameGraphPayload, sizeof(frameGraphPayload), 7u));
    frameGraphPayload[0u] = 99u;

    const Telemetry::EventView view = recorder.view();
    NWB_TELEMETRY_TEST_CHECK(context, view.valid());
    NWB_TELEMETRY_TEST_CHECK(context, view.eventCount() == 1u);

    const Telemetry::EventRecord* record = view.eventAt(0u);
    NWB_TELEMETRY_TEST_CHECK(context, record != nullptr);
    if(record){
        NWB_TELEMETRY_TEST_CHECK(context, record->header.valid());
        NWB_TELEMETRY_TEST_CHECK(context, record->header.kind == Telemetry::EventKind::FrameGraphFrame);
        NWB_TELEMETRY_TEST_CHECK(context, record->header.payloadFormat == Telemetry::PayloadFormat::Binary);
        NWB_TELEMETRY_TEST_CHECK(context, record->header.streamId == 7u);
        NWB_TELEMETRY_TEST_CHECK(context, record->header.frameIndex == 13u);
        NWB_TELEMETRY_TEST_CHECK(context, record->header.payloadBytes == 3u);
        NWB_TELEMETRY_TEST_CHECK(context, record->payload.size() == 3u);
        NWB_TELEMETRY_TEST_CHECK(context, record->payload[0u] == 4u);
        NWB_TELEMETRY_TEST_CHECK(context, record->payload[1u] == 5u);
        NWB_TELEMETRY_TEST_CHECK(context, record->payload[2u] == 6u);
    }

    NWB_TELEMETRY_TEST_CHECK(context, view.eventAt(1u) == nullptr);
}

static void TestRecorderClearAndDisabledState(TestContext& context){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());

    const u32 payload = 42u;
    NWB_TELEMETRY_TEST_CHECK(context, recorder.recordBinary(Telemetry::EventKind::PerfFrame, 1u, &payload, sizeof(payload)));
    NWB_TELEMETRY_TEST_CHECK(context, recorder.eventCount() == 1u);

    recorder.setCaptureOptions(Telemetry::CaptureOptions::Disabled());
    NWB_TELEMETRY_TEST_CHECK(context, !recorder.enabled());
    NWB_TELEMETRY_TEST_CHECK(context, recorder.eventCount() == 0u);
    NWB_TELEMETRY_TEST_CHECK(context, !recorder.recordBinary(Telemetry::EventKind::PerfFrame, 2u, &payload, sizeof(payload)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_TEST_ENTRY_POINT("telemetry", [](NWB::Tests::TestContext& context){
    __hidden_tests::TestCaptureFlags(context);
    __hidden_tests::TestRecorderFiltersAndCopiesPayload(context);
    __hidden_tests::TestRecorderClearAndDisabledState(context);
})


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

