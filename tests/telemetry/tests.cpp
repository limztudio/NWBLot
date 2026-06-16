// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/test_context.h>
#include <tests/capturing_logger.h>

#include <core/telemetry/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestContext = NWB::Tests::TestContext;
using TestArena = NWB::Tests::TestArena<struct TelemetryTestsTag>;
namespace Telemetry = NWB::Core::Telemetry;


#define NWB_TELEMETRY_TEST_CHECK NWB_TEST_CHECK

static u32 s_ExistingDiagnosticCallbackCount = 0u;

static void ExistingDiagnosticCallback(const DiagnosticEventRecord&)noexcept{
    ++s_ExistingDiagnosticCallbackCount;
}


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

static void TestEventCodecRoundTrip(TestContext& context){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::FrameGraphOnly());

    const u8 payload[] = { 10u, 20u, 30u, 40u };
    NWB_TELEMETRY_TEST_CHECK(context, recorder.recordBinary(Telemetry::EventKind::FrameGraphFrame, 44u, payload, sizeof(payload), 3u));

    const Telemetry::EventRecord* source = recorder.view().eventAt(0u);
    NWB_TELEMETRY_TEST_CHECK(context, source != nullptr);
    if(!source)
        return;

    Telemetry::TelemetryBytes encoded(testArena.arena);
    NWB_TELEMETRY_TEST_CHECK(context, Telemetry::EncodeEvent(*source, encoded));
    NWB_TELEMETRY_TEST_CHECK(context, encoded.size() == sizeof(Telemetry::EncodedEventHeader) + sizeof(payload));

    Telemetry::EventRecord decoded(testArena.arena);
    const Telemetry::DecodeResult result = Telemetry::DecodeEvent(testArena.arena, encoded.data(), encoded.size(), decoded);
    NWB_TELEMETRY_TEST_CHECK(context, result.ok());
    NWB_TELEMETRY_TEST_CHECK(context, result.bytesRead == encoded.size());
    NWB_TELEMETRY_TEST_CHECK(context, decoded.header.valid());
    NWB_TELEMETRY_TEST_CHECK(context, decoded.header.kind == source->header.kind);
    NWB_TELEMETRY_TEST_CHECK(context, decoded.header.payloadFormat == source->header.payloadFormat);
    NWB_TELEMETRY_TEST_CHECK(context, decoded.header.streamId == source->header.streamId);
    NWB_TELEMETRY_TEST_CHECK(context, decoded.header.frameIndex == source->header.frameIndex);
    NWB_TELEMETRY_TEST_CHECK(context, decoded.header.payloadBytes == source->header.payloadBytes);
    NWB_TELEMETRY_TEST_CHECK(context, decoded.payload.size() == sizeof(payload));
    NWB_TELEMETRY_TEST_CHECK(context, decoded.payload[0u] == 10u);
    NWB_TELEMETRY_TEST_CHECK(context, decoded.payload[3u] == 40u);
}

static void TestEventCodecRejectsInvalidInput(TestContext& context){
    TestArena testArena;
    Telemetry::TelemetryBytes encoded(testArena.arena);

    Telemetry::EventHeader invalidKindHeader;
    invalidKindHeader.kind = Telemetry::EventKind::Unknown;
    invalidKindHeader.payloadFormat = Telemetry::PayloadFormat::Binary;
    invalidKindHeader.payloadBytes = 0u;
    NWB_TELEMETRY_TEST_CHECK(context, !Telemetry::EncodeEvent(invalidKindHeader, nullptr, 0u, encoded));

    Telemetry::EventHeader invalidPayloadHeader;
    invalidPayloadHeader.kind = Telemetry::EventKind::PerfFrame;
    invalidPayloadHeader.payloadFormat = Telemetry::PayloadFormat::None;
    invalidPayloadHeader.payloadBytes = 1u;
    const u8 payload = 5u;
    NWB_TELEMETRY_TEST_CHECK(context, !Telemetry::EncodeEvent(invalidPayloadHeader, &payload, sizeof(payload), encoded));

    Telemetry::EventHeader validHeader;
    validHeader.kind = Telemetry::EventKind::PerfFrame;
    validHeader.payloadFormat = Telemetry::PayloadFormat::Binary;
    validHeader.payloadBytes = sizeof(payload);
    NWB_TELEMETRY_TEST_CHECK(context, !Telemetry::EncodeEvent(validHeader, nullptr, sizeof(payload), encoded));

    validHeader.payloadBytes = 0u;
    NWB_TELEMETRY_TEST_CHECK(context, Telemetry::EncodeEvent(validHeader, nullptr, 0u, encoded));
    encoded[0u] = 0u;

    Telemetry::EventRecord decoded(testArena.arena);
    Telemetry::DecodeResult result = Telemetry::DecodeEvent(testArena.arena, encoded.data(), encoded.size(), decoded);
    NWB_TELEMETRY_TEST_CHECK(context, result.status == Telemetry::DecodeStatus::InvalidHeader);

    result = Telemetry::DecodeEvent(testArena.arena, encoded.data(), sizeof(Telemetry::EncodedEventHeader) - 1u, decoded);
    NWB_TELEMETRY_TEST_CHECK(context, result.status == Telemetry::DecodeStatus::TruncatedHeader);
}

static void TestEventCodecReportsTruncatedPayload(TestContext& context){
    TestArena testArena;
    Telemetry::TelemetryBytes encoded(testArena.arena);

    const u8 payload[] = { 1u, 2u, 3u };
    Telemetry::EventHeader header;
    header.kind = Telemetry::EventKind::PerfFrame;
    header.payloadFormat = Telemetry::PayloadFormat::Binary;
    header.payloadBytes = sizeof(payload);
    NWB_TELEMETRY_TEST_CHECK(context, Telemetry::EncodeEvent(header, payload, sizeof(payload), encoded));

    Telemetry::EventRecord decoded(testArena.arena);
    const Telemetry::DecodeResult result = Telemetry::DecodeEvent(testArena.arena, encoded.data(), encoded.size() - 1u, decoded);
    NWB_TELEMETRY_TEST_CHECK(context, result.status == Telemetry::DecodeStatus::TruncatedPayload);
}

static void TestEventStreamCodecRoundTrip(TestContext& context){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    const u32 perfPayload = 99u;
    const char frameGraphPayload[] = "{frame:1}";
    NWB_TELEMETRY_TEST_CHECK(context, recorder.recordBinary(Telemetry::EventKind::PerfFrame, 101u, &perfPayload, sizeof(perfPayload), 2u));
    NWB_TELEMETRY_TEST_CHECK(context, recorder.record(
        Telemetry::EventKind::FrameGraphFrame,
        Telemetry::PayloadFormat::Json,
        102u,
        frameGraphPayload,
        sizeof(frameGraphPayload) - 1u,
        3u
    ));

    Telemetry::TelemetryBytes encoded(testArena.arena);
    NWB_TELEMETRY_TEST_CHECK(context, Telemetry::EncodeEventStream(recorder.view(), encoded));
    NWB_TELEMETRY_TEST_CHECK(
        context,
        encoded.size() == sizeof(Telemetry::EncodedStreamHeader)
            + (sizeof(Telemetry::EncodedEventHeader) * 2u)
            + sizeof(perfPayload)
            + sizeof(frameGraphPayload) - 1u
    );

    Telemetry::Recorder decoded(testArena.arena);
    const Telemetry::DecodeResult result = Telemetry::DecodeEventStream(testArena.arena, encoded.data(), encoded.size(), decoded);
    NWB_TELEMETRY_TEST_CHECK(context, result.ok());
    NWB_TELEMETRY_TEST_CHECK(context, result.bytesRead == encoded.size());
    NWB_TELEMETRY_TEST_CHECK(context, decoded.eventCount() == recorder.eventCount());

    for(usize i = 0u; i < recorder.eventCount(); ++i){
        const Telemetry::EventRecord* source = recorder.view().eventAt(i);
        const Telemetry::EventRecord* parsed = decoded.view().eventAt(i);
        NWB_TELEMETRY_TEST_CHECK(context, source != nullptr);
        NWB_TELEMETRY_TEST_CHECK(context, parsed != nullptr);
        if(!source || !parsed)
            continue;

        NWB_TELEMETRY_TEST_CHECK(context, parsed->header.kind == source->header.kind);
        NWB_TELEMETRY_TEST_CHECK(context, parsed->header.payloadFormat == source->header.payloadFormat);
        NWB_TELEMETRY_TEST_CHECK(context, parsed->header.streamId == source->header.streamId);
        NWB_TELEMETRY_TEST_CHECK(context, parsed->header.frameIndex == source->header.frameIndex);
        NWB_TELEMETRY_TEST_CHECK(context, parsed->header.timestampNanoseconds == source->header.timestampNanoseconds);
        NWB_TELEMETRY_TEST_CHECK(context, parsed->header.payloadBytes == source->header.payloadBytes);
        NWB_TELEMETRY_TEST_CHECK(context, parsed->payload.size() == source->payload.size());
        if(!source->payload.empty())
            NWB_TELEMETRY_TEST_CHECK(context, NWB_MEMCMP(parsed->payload.data(), source->payload.data(), source->payload.size()) == 0);
    }
}

static void TestEventStreamCodecHandlesEmptyStreams(TestContext& context){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);

    Telemetry::TelemetryBytes encoded(testArena.arena);
    NWB_TELEMETRY_TEST_CHECK(context, Telemetry::EncodeEventStream(recorder.view(), encoded));
    NWB_TELEMETRY_TEST_CHECK(context, encoded.size() == sizeof(Telemetry::EncodedStreamHeader));

    Telemetry::Recorder decoded(testArena.arena);
    const Telemetry::DecodeResult result = Telemetry::DecodeEventStream(testArena.arena, encoded.data(), encoded.size(), decoded);
    NWB_TELEMETRY_TEST_CHECK(context, result.ok());
    NWB_TELEMETRY_TEST_CHECK(context, result.bytesRead == encoded.size());
    NWB_TELEMETRY_TEST_CHECK(context, decoded.eventCount() == 0u);
}

static void TestEventStreamCodecRejectsInvalidInput(TestContext& context){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());

    const u8 payload[] = { 7u, 8u };
    NWB_TELEMETRY_TEST_CHECK(context, recorder.recordBinary(Telemetry::EventKind::PerfFrame, 1u, payload, sizeof(payload)));

    Telemetry::TelemetryBytes encoded(testArena.arena);
    NWB_TELEMETRY_TEST_CHECK(context, Telemetry::EncodeEventStream(recorder.view(), encoded));

    Telemetry::Recorder decoded(testArena.arena);
    Telemetry::DecodeResult result = Telemetry::DecodeEventStream(testArena.arena, encoded.data(), sizeof(Telemetry::EncodedStreamHeader) - 1u, decoded);
    NWB_TELEMETRY_TEST_CHECK(context, result.status == Telemetry::DecodeStatus::TruncatedHeader);

    Telemetry::TelemetryBytes corrupted(testArena.arena);
    corrupted = encoded;
    corrupted[0u] = 0u;
    result = Telemetry::DecodeEventStream(testArena.arena, corrupted.data(), corrupted.size(), decoded);
    NWB_TELEMETRY_TEST_CHECK(context, result.status == Telemetry::DecodeStatus::InvalidHeader);

    result = Telemetry::DecodeEventStream(testArena.arena, encoded.data(), encoded.size() - 1u, decoded);
    NWB_TELEMETRY_TEST_CHECK(context, result.status == Telemetry::DecodeStatus::TruncatedPayload);

    corrupted = encoded;
    Telemetry::EncodedStreamHeader streamHeader;
    NWB_MEMCPY(&streamHeader, sizeof(streamHeader), corrupted.data(), sizeof(streamHeader));
    streamHeader.eventCount = 0u;
    NWB_MEMCPY(corrupted.data(), corrupted.size(), &streamHeader, sizeof(streamHeader));
    result = Telemetry::DecodeEventStream(testArena.arena, corrupted.data(), corrupted.size(), decoded);
    NWB_TELEMETRY_TEST_CHECK(context, result.status == Telemetry::DecodeStatus::InvalidHeader);
}

static void TestTextLogPayloadRoundTrip(TestContext& context){
    TestArena testArena;
    Telemetry::TelemetryBytes payload(testArena.arena);

    NWB_TELEMETRY_TEST_CHECK(context, Telemetry::BuildTextLogPayload(
        testArena.arena,
        NWB::Core::Common::LogType::Warning,
        NWB_TEXT("telemetry text log"),
        payload
    ));
    NWB_TELEMETRY_TEST_CHECK(context, payload.size() == sizeof(Telemetry::EncodedTextLogPayloadHeader) + sizeof("telemetry text log") - 1u);

    Telemetry::TextLogPayload parsed(testArena.arena);
    NWB_TELEMETRY_TEST_CHECK(context, Telemetry::ParseTextLogPayload(testArena.arena, payload.data(), payload.size(), parsed));
    NWB_TELEMETRY_TEST_CHECK(context, parsed.type == NWB::Core::Common::LogType::Warning);
    NWB_TELEMETRY_TEST_CHECK(context, parsed.messageUtf8 == "telemetry text log");

    payload[0u] = 0u;
    NWB_TELEMETRY_TEST_CHECK(context, !Telemetry::ParseTextLogPayload(testArena.arena, payload.data(), payload.size(), parsed));
}

static void TestRecordTextLogUsesTelemetryEvent(TestContext& context){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    NWB_TELEMETRY_TEST_CHECK(context, Telemetry::RecordTextLog(
        recorder,
        NWB::Core::Common::LogType::EssentialInfo,
        NWB_TEXT("captured text"),
        123u,
        9u
    ));

    const Telemetry::EventRecord* event = recorder.view().eventAt(0u);
    NWB_TELEMETRY_TEST_CHECK(context, event != nullptr);
    if(!event)
        return;

    NWB_TELEMETRY_TEST_CHECK(context, event->header.kind == Telemetry::EventKind::TextLog);
    NWB_TELEMETRY_TEST_CHECK(context, event->header.payloadFormat == Telemetry::PayloadFormat::Binary);
    NWB_TELEMETRY_TEST_CHECK(context, event->header.frameIndex == 123u);
    NWB_TELEMETRY_TEST_CHECK(context, event->header.streamId == 9u);

    Telemetry::TextLogPayload parsed(testArena.arena);
    NWB_TELEMETRY_TEST_CHECK(context, Telemetry::ParseTextLogPayload(testArena.arena, event->payload.data(), event->payload.size(), parsed));
    NWB_TELEMETRY_TEST_CHECK(context, parsed.type == NWB::Core::Common::LogType::EssentialInfo);
    NWB_TELEMETRY_TEST_CHECK(context, parsed.messageUtf8 == "captured text");
}

static void TestTextLogCaptureLoggerForwardsAndRecords(TestContext& context){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    NWB::Tests::CapturingLogger forwardLogger;
    Telemetry::TextLogCaptureLogger logger(recorder, &forwardLogger);
    logger.setFrameIndex(321u);
    logger.setStreamId(4u);

    logger.enqueue(NWB::Core::Common::LogString(NWB_TEXT("bridged warning"), logger.arena()), NWB::Core::Common::LogType::Warning);

    NWB_TELEMETRY_TEST_CHECK(context, forwardLogger.messageCount() == 1u);
    NWB_TELEMETRY_TEST_CHECK(context, forwardLogger.lastType() == NWB::Core::Common::LogType::Warning);
    NWB_TELEMETRY_TEST_CHECK(context, forwardLogger.sawMessageContaining(NWB_TEXT("bridged warning")));
    NWB_TELEMETRY_TEST_CHECK(context, recorder.eventCount() == 1u);

    const Telemetry::EventRecord* event = recorder.view().eventAt(0u);
    NWB_TELEMETRY_TEST_CHECK(context, event != nullptr);
    if(!event)
        return;

    NWB_TELEMETRY_TEST_CHECK(context, event->header.kind == Telemetry::EventKind::TextLog);
    NWB_TELEMETRY_TEST_CHECK(context, event->header.frameIndex == 321u);
    NWB_TELEMETRY_TEST_CHECK(context, event->header.streamId == 4u);

    Telemetry::TextLogPayload parsed(testArena.arena);
    NWB_TELEMETRY_TEST_CHECK(context, Telemetry::ParseTextLogPayload(testArena.arena, event->payload.data(), event->payload.size(), parsed));
    NWB_TELEMETRY_TEST_CHECK(context, parsed.type == NWB::Core::Common::LogType::Warning);
    NWB_TELEMETRY_TEST_CHECK(context, parsed.messageUtf8 == "bridged warning");
}

static void TestDiagnosticPayloadRoundTrip(TestContext& context){
    TestArena testArena;
    Telemetry::TelemetryBytes payload(testArena.arena);

    const DiagnosticEventRecord source{
        .event = DiagnosticEventName::s_Error,
        .category = "unit_category",
        .expression = "value != nullptr",
        .message = "diagnostic message",
        .file = "diagnostic_test.cpp",
        .instructionPointer = 0x1234u,
        .line = 77u,
        .terminatesProcess = true,
    };

    NWB_TELEMETRY_TEST_CHECK(context, Telemetry::BuildDiagnosticPayload(testArena.arena, source, payload));
    NWB_TELEMETRY_TEST_CHECK(context, payload.size() > sizeof(Telemetry::EncodedDiagnosticPayloadHeader));

    Telemetry::DiagnosticPayload parsed(testArena.arena);
    NWB_TELEMETRY_TEST_CHECK(context, Telemetry::ParseDiagnosticPayload(testArena.arena, payload.data(), payload.size(), parsed));
    NWB_TELEMETRY_TEST_CHECK(context, parsed.event == DiagnosticEventName::s_Error);
    NWB_TELEMETRY_TEST_CHECK(context, parsed.category == "unit_category");
    NWB_TELEMETRY_TEST_CHECK(context, parsed.expression == "value != nullptr");
    NWB_TELEMETRY_TEST_CHECK(context, parsed.message == "diagnostic message");
    NWB_TELEMETRY_TEST_CHECK(context, parsed.file == "diagnostic_test.cpp");
    NWB_TELEMETRY_TEST_CHECK(context, parsed.instructionPointer == 0x1234u);
    NWB_TELEMETRY_TEST_CHECK(context, parsed.line == 77u);
    NWB_TELEMETRY_TEST_CHECK(context, parsed.terminatesProcess);

    payload[0u] = 0u;
    NWB_TELEMETRY_TEST_CHECK(context, !Telemetry::ParseDiagnosticPayload(testArena.arena, payload.data(), payload.size(), parsed));
}

static void TestRecordDiagnosticUsesTelemetryEvent(TestContext& context){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    const DiagnosticEventRecord source{
        .event = DiagnosticEventName::s_Assert,
        .category = DiagnosticEventCategory::s_Assert,
        .expression = "condition",
        .message = "assert payload",
        .file = "assert.cpp",
        .instructionPointer = 42u,
        .line = 12u,
    };

    NWB_TELEMETRY_TEST_CHECK(context, Telemetry::RecordDiagnostic(recorder, source, 222u, 6u));

    const Telemetry::EventRecord* event = recorder.view().eventAt(0u);
    NWB_TELEMETRY_TEST_CHECK(context, event != nullptr);
    if(!event)
        return;

    NWB_TELEMETRY_TEST_CHECK(context, event->header.kind == Telemetry::EventKind::Diagnostic);
    NWB_TELEMETRY_TEST_CHECK(context, event->header.payloadFormat == Telemetry::PayloadFormat::Binary);
    NWB_TELEMETRY_TEST_CHECK(context, event->header.frameIndex == 222u);
    NWB_TELEMETRY_TEST_CHECK(context, event->header.streamId == 6u);

    Telemetry::DiagnosticPayload parsed(testArena.arena);
    NWB_TELEMETRY_TEST_CHECK(context, Telemetry::ParseDiagnosticPayload(testArena.arena, event->payload.data(), event->payload.size(), parsed));
    NWB_TELEMETRY_TEST_CHECK(context, parsed.event == DiagnosticEventName::s_Assert);
    NWB_TELEMETRY_TEST_CHECK(context, parsed.category == DiagnosticEventCategory::s_Assert);
    NWB_TELEMETRY_TEST_CHECK(context, parsed.message == "assert payload");
}

static void TestDiagnosticCaptureGuardRecordsGlobalDiagnostic(TestContext& context){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    {
        Telemetry::DiagnosticCaptureGuard guard(recorder);
        NWB_TELEMETRY_TEST_CHECK(context, guard.installed());
        guard.setFrameIndex(333u);
        guard.setStreamId(8u);
        CaptureDiagnosticEvent(DiagnosticEventRecord{
            .event = DiagnosticEventName::s_Error,
            .category = "telemetry_guard",
            .message = "captured diagnostic",
            .file = "guard.cpp",
            .line = 44u,
        });
    }

    CaptureDiagnosticEvent(DiagnosticEventRecord{
        .event = DiagnosticEventName::s_Error,
        .category = "telemetry_guard",
        .message = "ignored after guard",
    });

    NWB_TELEMETRY_TEST_CHECK(context, recorder.eventCount() == 1u);

    const Telemetry::EventRecord* event = recorder.view().eventAt(0u);
    NWB_TELEMETRY_TEST_CHECK(context, event != nullptr);
    if(!event)
        return;

    NWB_TELEMETRY_TEST_CHECK(context, event->header.kind == Telemetry::EventKind::Diagnostic);
    NWB_TELEMETRY_TEST_CHECK(context, event->header.frameIndex == 333u);
    NWB_TELEMETRY_TEST_CHECK(context, event->header.streamId == 8u);

    Telemetry::DiagnosticPayload parsed(testArena.arena);
    NWB_TELEMETRY_TEST_CHECK(context, Telemetry::ParseDiagnosticPayload(testArena.arena, event->payload.data(), event->payload.size(), parsed));
    NWB_TELEMETRY_TEST_CHECK(context, parsed.event == DiagnosticEventName::s_Error);
    NWB_TELEMETRY_TEST_CHECK(context, parsed.category == "telemetry_guard");
    NWB_TELEMETRY_TEST_CHECK(context, parsed.message == "captured diagnostic");
    NWB_TELEMETRY_TEST_CHECK(context, parsed.file == "guard.cpp");
    NWB_TELEMETRY_TEST_CHECK(context, parsed.line == 44u);
}

static void TestDiagnosticCaptureGuardManualCaptureReturnsStatus(TestContext& context){
    TestArena testArena;
    Telemetry::Recorder disabledRecorder(testArena.arena);
    Telemetry::DiagnosticCaptureGuard disabledGuard(disabledRecorder);
    NWB_TELEMETRY_TEST_CHECK(context, !disabledGuard.capture(DiagnosticEventRecord{
        .event = DiagnosticEventName::s_Error,
        .message = "disabled diagnostic",
    }));

    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());
    Telemetry::DiagnosticCaptureGuard guard(recorder);
    guard.setFrameIndex(444u);
    guard.setStreamId(5u);

    NWB_TELEMETRY_TEST_CHECK(context, guard.capture(DiagnosticEventRecord{
        .event = DiagnosticEventName::s_Error,
        .category = "manual_capture",
        .message = "manual diagnostic",
        .file = "manual.cpp",
        .line = 55u,
    }));
    NWB_TELEMETRY_TEST_CHECK(context, recorder.eventCount() == 1u);

    const Telemetry::EventRecord* event = recorder.view().eventAt(0u);
    NWB_TELEMETRY_TEST_CHECK(context, event != nullptr);
    if(!event)
        return;

    NWB_TELEMETRY_TEST_CHECK(context, event->header.frameIndex == 444u);
    NWB_TELEMETRY_TEST_CHECK(context, event->header.streamId == 5u);

    Telemetry::DiagnosticPayload parsed(testArena.arena);
    NWB_TELEMETRY_TEST_CHECK(context, Telemetry::ParseDiagnosticPayload(testArena.arena, event->payload.data(), event->payload.size(), parsed));
    NWB_TELEMETRY_TEST_CHECK(context, parsed.category == "manual_capture");
    NWB_TELEMETRY_TEST_CHECK(context, parsed.message == "manual diagnostic");
}

static void TestDiagnosticCaptureGuardDoesNotReplaceExistingCallback(TestContext& context){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    s_ExistingDiagnosticCallbackCount = 0u;
    SetDiagnosticEventCallback(ExistingDiagnosticCallback);
    {
        Telemetry::DiagnosticCaptureGuard guard(recorder);
        NWB_TELEMETRY_TEST_CHECK(context, !guard.installed());
        CaptureDiagnosticEvent(DiagnosticEventRecord{
            .event = DiagnosticEventName::s_Error,
            .message = "existing callback should keep ownership",
        });
    }
    ClearDiagnosticEventCallback(ExistingDiagnosticCallback);

    NWB_TELEMETRY_TEST_CHECK(context, s_ExistingDiagnosticCallbackCount == 1u);
    NWB_TELEMETRY_TEST_CHECK(context, recorder.eventCount() == 0u);
}

static void BuildTestFrameGraph(
    Telemetry::TelemetryArena& arena,
    Telemetry::FrameGraphNodeDescs& nodes,
    Telemetry::FrameGraphEdgeDescs& edges
){
    nodes = Telemetry::FrameGraphNodeDescs(arena);
    edges = Telemetry::FrameGraphEdgeDescs(arena);

    nodes.push_back(Telemetry::FrameGraphNodeDesc{
        .name = Name("gbuffer"),
        .label = AStringView("GBuffer Pass"),
        .kind = Telemetry::FrameGraphNodeKind::Pass,
        .flags = 1u,
    });
    nodes.push_back(Telemetry::FrameGraphNodeDesc{
        .name = Name("albedo"),
        .label = AStringView("Albedo Texture"),
        .kind = Telemetry::FrameGraphNodeKind::Resource,
    });
    nodes.push_back(Telemetry::FrameGraphNodeDesc{
        .name = Name("lighting"),
        .label = AStringView("Lighting Pass"),
        .kind = Telemetry::FrameGraphNodeKind::Pass,
    });

    edges.push_back(Telemetry::FrameGraphEdgeDesc{
        .fromNodeIndex = 0u,
        .toNodeIndex = 1u,
        .kind = Telemetry::FrameGraphEdgeKind::Writes,
    });
    edges.push_back(Telemetry::FrameGraphEdgeDesc{
        .fromNodeIndex = 1u,
        .toNodeIndex = 2u,
        .kind = Telemetry::FrameGraphEdgeKind::Reads,
        .flags = 2u,
    });
}

static void TestFrameGraphPayloadRoundTrip(TestContext& context){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestFrameGraph(testArena.arena, nodes, edges);

    Telemetry::TelemetryBytes payload(testArena.arena);
    NWB_TELEMETRY_TEST_CHECK(context, Telemetry::BuildFrameGraphPayload(testArena.arena, 905u, nodes, edges, payload));
    NWB_TELEMETRY_TEST_CHECK(
        context,
        payload.size() == sizeof(Telemetry::EncodedFrameGraphPayloadHeader)
            + (sizeof(Telemetry::EncodedFrameGraphNode) * nodes.size())
            + (sizeof(Telemetry::EncodedFrameGraphEdge) * edges.size())
            + sizeof("GBuffer Pass")
            + sizeof("Albedo Texture")
            + sizeof("Lighting Pass")
    );

    Telemetry::FrameGraphPayload parsed(testArena.arena);
    NWB_TELEMETRY_TEST_CHECK(context, Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));
    NWB_TELEMETRY_TEST_CHECK(context, parsed.frameIndex == 905u);
    NWB_TELEMETRY_TEST_CHECK(context, parsed.nodes.size() == 3u);
    NWB_TELEMETRY_TEST_CHECK(context, parsed.edges.size() == 2u);
    if(parsed.nodes.size() == 3u){
        NWB_TELEMETRY_TEST_CHECK(context, parsed.nodes[0u].name == Name("gbuffer"));
        NWB_TELEMETRY_TEST_CHECK(context, parsed.nodes[0u].label == "GBuffer Pass");
        NWB_TELEMETRY_TEST_CHECK(context, parsed.nodes[0u].kind == Telemetry::FrameGraphNodeKind::Pass);
        NWB_TELEMETRY_TEST_CHECK(context, parsed.nodes[0u].flags == 1u);
        NWB_TELEMETRY_TEST_CHECK(context, parsed.nodes[1u].name == Name("albedo"));
        NWB_TELEMETRY_TEST_CHECK(context, parsed.nodes[1u].label == "Albedo Texture");
        NWB_TELEMETRY_TEST_CHECK(context, parsed.nodes[1u].kind == Telemetry::FrameGraphNodeKind::Resource);
        NWB_TELEMETRY_TEST_CHECK(context, parsed.nodes[2u].name == Name("lighting"));
        NWB_TELEMETRY_TEST_CHECK(context, parsed.nodes[2u].label == "Lighting Pass");
    }
    if(parsed.edges.size() == 2u){
        NWB_TELEMETRY_TEST_CHECK(context, parsed.edges[0u].fromNodeIndex == 0u);
        NWB_TELEMETRY_TEST_CHECK(context, parsed.edges[0u].toNodeIndex == 1u);
        NWB_TELEMETRY_TEST_CHECK(context, parsed.edges[0u].kind == Telemetry::FrameGraphEdgeKind::Writes);
        NWB_TELEMETRY_TEST_CHECK(context, parsed.edges[1u].fromNodeIndex == 1u);
        NWB_TELEMETRY_TEST_CHECK(context, parsed.edges[1u].toNodeIndex == 2u);
        NWB_TELEMETRY_TEST_CHECK(context, parsed.edges[1u].kind == Telemetry::FrameGraphEdgeKind::Reads);
        NWB_TELEMETRY_TEST_CHECK(context, parsed.edges[1u].flags == 2u);
    }

    payload[0u] = 0u;
    NWB_TELEMETRY_TEST_CHECK(context, !Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));
}

static void TestFrameGraphPayloadRejectsInvalidInput(TestContext& context){
    TestArena testArena;
    Telemetry::TelemetryBytes payload(testArena.arena);
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestFrameGraph(testArena.arena, nodes, edges);

    nodes[0u].kind = Telemetry::FrameGraphNodeKind::Unknown;
    NWB_TELEMETRY_TEST_CHECK(context, !Telemetry::BuildFrameGraphPayload(testArena.arena, 1u, nodes, edges, payload));

    BuildTestFrameGraph(testArena.arena, nodes, edges);
    nodes[0u].label = AStringView("bad\0label", 9u);
    NWB_TELEMETRY_TEST_CHECK(context, !Telemetry::BuildFrameGraphPayload(testArena.arena, 1u, nodes, edges, payload));

    BuildTestFrameGraph(testArena.arena, nodes, edges);
    edges[0u].toNodeIndex = 99u;
    NWB_TELEMETRY_TEST_CHECK(context, !Telemetry::BuildFrameGraphPayload(testArena.arena, 1u, nodes, edges, payload));
}

static void TestRecordFrameGraphUsesTelemetryEvent(TestContext& context){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestFrameGraph(testArena.arena, nodes, edges);

    NWB_TELEMETRY_TEST_CHECK(context, Telemetry::RecordFrameGraph(recorder, 909u, nodes, edges, 14u));

    const Telemetry::EventRecord* event = recorder.view().eventAt(0u);
    NWB_TELEMETRY_TEST_CHECK(context, event != nullptr);
    if(!event)
        return;

    NWB_TELEMETRY_TEST_CHECK(context, event->header.kind == Telemetry::EventKind::FrameGraphFrame);
    NWB_TELEMETRY_TEST_CHECK(context, event->header.payloadFormat == Telemetry::PayloadFormat::Binary);
    NWB_TELEMETRY_TEST_CHECK(context, event->header.frameIndex == 909u);
    NWB_TELEMETRY_TEST_CHECK(context, event->header.streamId == 14u);

    Telemetry::FrameGraphPayload parsed(testArena.arena);
    NWB_TELEMETRY_TEST_CHECK(context, Telemetry::ParseFrameGraphPayload(testArena.arena, event->payload.data(), event->payload.size(), parsed));
    NWB_TELEMETRY_TEST_CHECK(context, parsed.frameIndex == 909u);
    NWB_TELEMETRY_TEST_CHECK(context, parsed.nodes.size() == 3u);
    NWB_TELEMETRY_TEST_CHECK(context, parsed.edges.size() == 2u);
}

static NWB::Core::Perf::TimingStats MakeTestTimingStats(){
    NWB::Core::Perf::TimingStats stats;
    stats.seconds = 0.125;
    stats.sampleCount = 3u;
    stats.publishFrameIndex = 77u;
    stats.firstSampleFrameIndex = 70u;
    stats.lastSampleFrameIndex = 76u;
    return stats;
}

static void TestPerfTimingPayloadRoundTrip(TestContext& context){
    TestArena testArena;
    const Name scopeName("renderer/frame");
    const NWB::Core::Perf::TimingStats stats = MakeTestTimingStats();

    Telemetry::TelemetryBytes payload(testArena.arena);
    NWB_TELEMETRY_TEST_CHECK(context, Telemetry::BuildPerfTimingPayload(
        testArena.arena,
        Telemetry::PerfTimingSource::Gpu,
        scopeName,
        "Renderer Frame",
        stats,
        payload
    ));
    NWB_TELEMETRY_TEST_CHECK(context, payload.size() == sizeof(Telemetry::EncodedPerfTimingPayloadHeader) + sizeof("Renderer Frame") - 1u);

    Telemetry::PerfTimingPayload parsed(testArena.arena);
    NWB_TELEMETRY_TEST_CHECK(context, Telemetry::ParsePerfTimingPayload(testArena.arena, payload.data(), payload.size(), parsed));
    NWB_TELEMETRY_TEST_CHECK(context, parsed.source == Telemetry::PerfTimingSource::Gpu);
    NWB_TELEMETRY_TEST_CHECK(context, parsed.scopeName == scopeName);
    NWB_TELEMETRY_TEST_CHECK(context, parsed.scopeText == "Renderer Frame");
    NWB_TELEMETRY_TEST_CHECK(context, parsed.stats.seconds == stats.seconds);
    NWB_TELEMETRY_TEST_CHECK(context, parsed.stats.sampleCount == stats.sampleCount);
    NWB_TELEMETRY_TEST_CHECK(context, parsed.stats.publishFrameIndex == stats.publishFrameIndex);
    NWB_TELEMETRY_TEST_CHECK(context, parsed.stats.firstSampleFrameIndex == stats.firstSampleFrameIndex);
    NWB_TELEMETRY_TEST_CHECK(context, parsed.stats.lastSampleFrameIndex == stats.lastSampleFrameIndex);

    payload[0u] = 0u;
    NWB_TELEMETRY_TEST_CHECK(context, !Telemetry::ParsePerfTimingPayload(testArena.arena, payload.data(), payload.size(), parsed));
}

static void TestPerfTimingPayloadRejectsInvalidInput(TestContext& context){
    TestArena testArena;
    Telemetry::TelemetryBytes payload(testArena.arena);
    NWB::Core::Perf::TimingStats stats = MakeTestTimingStats();

    NWB_TELEMETRY_TEST_CHECK(context, !Telemetry::BuildPerfTimingPayload(
        testArena.arena,
        Telemetry::PerfTimingSource::Unknown,
        Name("renderer/frame"),
        "Renderer Frame",
        stats,
        payload
    ));

    stats.sampleCount = 0u;
    NWB_TELEMETRY_TEST_CHECK(context, !Telemetry::BuildPerfTimingPayload(
        testArena.arena,
        Telemetry::PerfTimingSource::Cpu,
        Name("renderer/frame"),
        "Renderer Frame",
        stats,
        payload
    ));
}

static void TestRecordPerfTimingUsesTelemetryEvent(TestContext& context){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    const Name scopeName("cpu/update");
    const NWB::Core::Perf::TimingStats stats = MakeTestTimingStats();
    NWB_TELEMETRY_TEST_CHECK(context, Telemetry::RecordPerfTiming(recorder, Telemetry::PerfTimingSource::Cpu, scopeName, stats, 11u));

    const Telemetry::EventRecord* event = recorder.view().eventAt(0u);
    NWB_TELEMETRY_TEST_CHECK(context, event != nullptr);
    if(!event)
        return;

    NWB_TELEMETRY_TEST_CHECK(context, event->header.kind == Telemetry::EventKind::PerfFrame);
    NWB_TELEMETRY_TEST_CHECK(context, event->header.payloadFormat == Telemetry::PayloadFormat::Binary);
    NWB_TELEMETRY_TEST_CHECK(context, event->header.frameIndex == stats.publishFrameIndex);
    NWB_TELEMETRY_TEST_CHECK(context, event->header.streamId == 11u);

    Telemetry::PerfTimingPayload parsed(testArena.arena);
    NWB_TELEMETRY_TEST_CHECK(context, Telemetry::ParsePerfTimingPayload(testArena.arena, event->payload.data(), event->payload.size(), parsed));
    NWB_TELEMETRY_TEST_CHECK(context, parsed.source == Telemetry::PerfTimingSource::Cpu);
    NWB_TELEMETRY_TEST_CHECK(context, parsed.scopeName == scopeName);
    NWB_TELEMETRY_TEST_CHECK(context, parsed.stats.sampleCount == stats.sampleCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_TEST_ENTRY_POINT("telemetry", [](NWB::Tests::TestContext& context){
    __hidden_tests::TestCaptureFlags(context);
    __hidden_tests::TestRecorderFiltersAndCopiesPayload(context);
    __hidden_tests::TestRecorderClearAndDisabledState(context);
    __hidden_tests::TestEventCodecRoundTrip(context);
    __hidden_tests::TestEventCodecRejectsInvalidInput(context);
    __hidden_tests::TestEventCodecReportsTruncatedPayload(context);
    __hidden_tests::TestEventStreamCodecRoundTrip(context);
    __hidden_tests::TestEventStreamCodecHandlesEmptyStreams(context);
    __hidden_tests::TestEventStreamCodecRejectsInvalidInput(context);
    __hidden_tests::TestTextLogPayloadRoundTrip(context);
    __hidden_tests::TestRecordTextLogUsesTelemetryEvent(context);
    __hidden_tests::TestTextLogCaptureLoggerForwardsAndRecords(context);
    __hidden_tests::TestDiagnosticPayloadRoundTrip(context);
    __hidden_tests::TestRecordDiagnosticUsesTelemetryEvent(context);
    __hidden_tests::TestDiagnosticCaptureGuardRecordsGlobalDiagnostic(context);
    __hidden_tests::TestDiagnosticCaptureGuardManualCaptureReturnsStatus(context);
    __hidden_tests::TestDiagnosticCaptureGuardDoesNotReplaceExistingCallback(context);
    __hidden_tests::TestFrameGraphPayloadRoundTrip(context);
    __hidden_tests::TestFrameGraphPayloadRejectsInvalidInput(context);
    __hidden_tests::TestRecordFrameGraphUsesTelemetryEvent(context);
    __hidden_tests::TestPerfTimingPayloadRoundTrip(context);
    __hidden_tests::TestPerfTimingPayloadRejectsInvalidInput(context);
    __hidden_tests::TestRecordPerfTimingUsesTelemetryEvent(context);
})


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
