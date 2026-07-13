
#include <tests/test_context.h>
#include <gtest/gtest.h>
#include <tests/capturing_logger.h>

#include <core/telemetry/module.h>
#include <core/telemetry/frame_graph_registry.h>
#include <logger/telemetry/ingest.h>
#include <logger/telemetry/report.h>

#include <global/filesystem/operations.h>

#include <thread>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
using TestArena = NWB::Tests::TestArena<struct TelemetryTestsTag>;
namespace Telemetry = NWB::Core::Telemetry;
namespace Log = NWB::Log;

static u32 s_ExistingDiagnosticCallbackCount = 0u;

static void ExistingDiagnosticCallback(const DiagnosticEventRecord&)noexcept{
    ++s_ExistingDiagnosticCallbackCount;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Telemetry, CaptureFlags){
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
    static_assert(all.perfEnabled());
    static_assert(all.frameGraphEnabled());

    EXPECT_TRUE(Telemetry::CaptureAllowsEventKind(all, Telemetry::EventKind::TextLog));
    EXPECT_TRUE(Telemetry::CaptureAllowsEventKind(perf, Telemetry::EventKind::MemoryFrame));
    EXPECT_TRUE(Telemetry::CaptureAllowsEventKind(all, Telemetry::EventKind::FrameGraphFrame));
    EXPECT_FALSE(Telemetry::CaptureAllowsEventKind(frameGraph, Telemetry::EventKind::PerfFrame));
    EXPECT_FALSE(Telemetry::CaptureAllowsEventKind(frameGraph, Telemetry::EventKind::MemoryFrame));
    EXPECT_TRUE(Telemetry::CaptureAllowsEventKind(frameGraph, Telemetry::EventKind::FrameGraphFrame));
}

TEST(Telemetry, RecorderFiltersAndCopiesPayload){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::FrameGraphOnly());

    const u8 perfPayload[] = { 1u, 2u };
    EXPECT_FALSE(recorder.recordBinary(Telemetry::EventKind::PerfFrame, 12u, perfPayload, sizeof(perfPayload)));
    EXPECT_EQ(recorder.eventCount(), 0u);

    u8 frameGraphPayload[] = { 4u, 5u, 6u };
    EXPECT_TRUE(recorder.recordBinary(Telemetry::EventKind::FrameGraphFrame, 13u, frameGraphPayload, sizeof(frameGraphPayload), 7u));
    frameGraphPayload[0u] = 99u;

    const Telemetry::EventView view = recorder.view();
    EXPECT_TRUE(view.valid());
    EXPECT_EQ(view.eventCount(), 1u);

    const Telemetry::EventRecord* record = view.eventAt(0u);
    ASSERT_NE(record, nullptr);
    EXPECT_TRUE(record->header.valid());
    EXPECT_EQ(record->header.kind, Telemetry::EventKind::FrameGraphFrame);
    EXPECT_EQ(record->header.streamId, 7u);
    EXPECT_EQ(record->header.frameIndex, 13u);
    EXPECT_EQ(record->header.payloadBytes, 3u);
    EXPECT_EQ(record->payload.size(), 3u);
    EXPECT_EQ(record->payload[0u], 4u);
    EXPECT_EQ(record->payload[1u], 5u);
    EXPECT_EQ(record->payload[2u], 6u);

    EXPECT_EQ(view.eventAt(1u), nullptr);
}

TEST(Telemetry, RecorderClearAndDisabledState){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());

    const u32 payload = 42u;
    EXPECT_TRUE(recorder.recordBinary(Telemetry::EventKind::PerfFrame, 1u, &payload, sizeof(payload)));
    EXPECT_EQ(recorder.eventCount(), 1u);

    recorder.setCaptureOptions(Telemetry::CaptureOptions::Disabled());
    EXPECT_FALSE(recorder.enabled());
    EXPECT_EQ(recorder.eventCount(), 0u);
    EXPECT_FALSE(recorder.recordBinary(Telemetry::EventKind::PerfFrame, 2u, &payload, sizeof(payload)));
}

TEST(Telemetry, EventCodecRoundTrip){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::FrameGraphOnly());

    const u8 payload[] = { 10u, 20u, 30u, 40u };
    EXPECT_TRUE(recorder.recordBinary(Telemetry::EventKind::FrameGraphFrame, 44u, payload, sizeof(payload), 3u));

    const Telemetry::EventRecord* source = recorder.view().eventAt(0u);
    ASSERT_NE(source, nullptr);

    Telemetry::TelemetryBytes encoded(testArena.arena);
    EXPECT_TRUE(Telemetry::EncodeEvent(*source, encoded));
    EXPECT_EQ(encoded.size(), sizeof(Telemetry::EncodedEventHeader) + sizeof(payload));

    Telemetry::EventRecord decoded(testArena.arena);
    const Telemetry::DecodeResult result = Telemetry::DecodeEvent(testArena.arena, encoded.data(), encoded.size(), decoded);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.bytesRead, encoded.size());
    EXPECT_TRUE(decoded.header.valid());
    EXPECT_EQ(decoded.header.kind, source->header.kind);
    EXPECT_EQ(decoded.header.streamId, source->header.streamId);
    EXPECT_EQ(decoded.header.frameIndex, source->header.frameIndex);
    EXPECT_EQ(decoded.header.payloadBytes, source->header.payloadBytes);
    EXPECT_EQ(decoded.payload.size(), sizeof(payload));
    EXPECT_EQ(decoded.payload[0u], 10u);
    EXPECT_EQ(decoded.payload[3u], 40u);
}

TEST(Telemetry, EventCodecRejectsInvalidInput){
    TestArena testArena;
    Telemetry::TelemetryBytes encoded(testArena.arena);

    Telemetry::EventHeader invalidKindHeader;
    invalidKindHeader.kind = Telemetry::EventKind::Unknown;
    invalidKindHeader.payloadBytes = 0u;
    EXPECT_FALSE(Telemetry::EncodeEvent(invalidKindHeader, nullptr, 0u, encoded));

    const u8 payload = 5u;

    Telemetry::EventHeader validHeader;
    validHeader.kind = Telemetry::EventKind::PerfFrame;
    validHeader.payloadBytes = sizeof(payload);
    EXPECT_FALSE(Telemetry::EncodeEvent(validHeader, nullptr, sizeof(payload), encoded));

    validHeader.payloadBytes = 0u;
    EXPECT_TRUE(Telemetry::EncodeEvent(validHeader, nullptr, 0u, encoded));
    encoded[0u] = 0u;

    Telemetry::EventRecord decoded(testArena.arena);
    Telemetry::DecodeResult result = Telemetry::DecodeEvent(testArena.arena, encoded.data(), encoded.size(), decoded);
    EXPECT_EQ(result.status, Telemetry::DecodeStatus::InvalidHeader);

    result = Telemetry::DecodeEvent(testArena.arena, encoded.data(), sizeof(Telemetry::EncodedEventHeader) - 1u, decoded);
    EXPECT_EQ(result.status, Telemetry::DecodeStatus::TruncatedHeader);
}

TEST(Telemetry, EventCodecReportsTruncatedPayload){
    TestArena testArena;
    Telemetry::TelemetryBytes encoded(testArena.arena);

    const u8 payload[] = { 1u, 2u, 3u };
    Telemetry::EventHeader header;
    header.kind = Telemetry::EventKind::PerfFrame;
    header.payloadBytes = sizeof(payload);
    EXPECT_TRUE(Telemetry::EncodeEvent(header, payload, sizeof(payload), encoded));

    Telemetry::EventRecord decoded(testArena.arena);
    const Telemetry::DecodeResult result = Telemetry::DecodeEvent(testArena.arena, encoded.data(), encoded.size() - 1u, decoded);
    EXPECT_EQ(result.status, Telemetry::DecodeStatus::TruncatedPayload);
}

TEST(Telemetry, EventStreamCodecRoundTrip){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    const u32 perfPayload = 99u;
    const char frameGraphPayload[] = "{frame:1}";
    EXPECT_TRUE(recorder.recordBinary(Telemetry::EventKind::PerfFrame, 101u, &perfPayload, sizeof(perfPayload), 2u));
    EXPECT_TRUE(recorder.recordBinary(
        Telemetry::EventKind::FrameGraphFrame,
        102u,
        frameGraphPayload,
        sizeof(frameGraphPayload) - 1u,
        3u
    ));

    Telemetry::TelemetryBytes encoded(testArena.arena);
    EXPECT_TRUE(Telemetry::EncodeEventStream(recorder.view(), encoded));
    EXPECT_EQ(encoded.size(), sizeof(Telemetry::EncodedStreamHeader)
            + (sizeof(Telemetry::EncodedEventHeader) * 2u)
            + sizeof(perfPayload)
            + sizeof(frameGraphPayload) - 1u);

    Telemetry::Recorder decoded(testArena.arena);
    const Telemetry::DecodeResult result = Telemetry::DecodeEventStream(testArena.arena, encoded.data(), encoded.size(), decoded);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.bytesRead, encoded.size());
    ASSERT_EQ(decoded.eventCount(), recorder.eventCount());

    for(usize i = 0u; i < recorder.eventCount(); ++i){
        const Telemetry::EventRecord* source = recorder.view().eventAt(i);
        const Telemetry::EventRecord* parsed = decoded.view().eventAt(i);
        ASSERT_NE(source, nullptr);
        ASSERT_NE(parsed, nullptr);

        EXPECT_EQ(parsed->header.kind, source->header.kind);
        EXPECT_EQ(parsed->header.streamId, source->header.streamId);
        EXPECT_EQ(parsed->header.frameIndex, source->header.frameIndex);
        EXPECT_EQ(parsed->header.timestampNanoseconds, source->header.timestampNanoseconds);
        EXPECT_EQ(parsed->header.payloadBytes, source->header.payloadBytes);
        ASSERT_EQ(parsed->payload.size(), source->payload.size());
        if(!source->payload.empty())
            EXPECT_EQ(NWB_MEMCMP(parsed->payload.data(), source->payload.data(), source->payload.size()), 0);
    }
}

TEST(Telemetry, EventStreamCodecHandlesEmptyStreams){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);

    Telemetry::TelemetryBytes encoded(testArena.arena);
    EXPECT_TRUE(Telemetry::EncodeEventStream(recorder.view(), encoded));
    EXPECT_EQ(encoded.size(), sizeof(Telemetry::EncodedStreamHeader));

    Telemetry::Recorder decoded(testArena.arena);
    const Telemetry::DecodeResult result = Telemetry::DecodeEventStream(testArena.arena, encoded.data(), encoded.size(), decoded);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.bytesRead, encoded.size());
    EXPECT_EQ(decoded.eventCount(), 0u);
}

TEST(Telemetry, EventStreamCodecRejectsInvalidInput){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());

    const u8 payload[] = { 7u, 8u };
    EXPECT_TRUE(recorder.recordBinary(Telemetry::EventKind::PerfFrame, 1u, payload, sizeof(payload)));

    Telemetry::TelemetryBytes encoded(testArena.arena);
    EXPECT_TRUE(Telemetry::EncodeEventStream(recorder.view(), encoded));

    Telemetry::Recorder decoded(testArena.arena);
    Telemetry::DecodeResult result = Telemetry::DecodeEventStream(testArena.arena, encoded.data(), sizeof(Telemetry::EncodedStreamHeader) - 1u, decoded);
    EXPECT_EQ(result.status, Telemetry::DecodeStatus::TruncatedHeader);

    Telemetry::TelemetryBytes corrupted(testArena.arena);
    corrupted = encoded;
    corrupted[0u] = 0u;
    result = Telemetry::DecodeEventStream(testArena.arena, corrupted.data(), corrupted.size(), decoded);
    EXPECT_EQ(result.status, Telemetry::DecodeStatus::InvalidHeader);

    result = Telemetry::DecodeEventStream(testArena.arena, encoded.data(), encoded.size() - 1u, decoded);
    EXPECT_EQ(result.status, Telemetry::DecodeStatus::TruncatedPayload);

    corrupted = encoded;
    Telemetry::EncodedStreamHeader streamHeader;
    NWB_MEMCPY(&streamHeader, sizeof(streamHeader), corrupted.data(), sizeof(streamHeader));
    streamHeader.eventCount = 0u;
    NWB_MEMCPY(corrupted.data(), corrupted.size(), &streamHeader, sizeof(streamHeader));
    result = Telemetry::DecodeEventStream(testArena.arena, corrupted.data(), corrupted.size(), decoded);
    EXPECT_EQ(result.status, Telemetry::DecodeStatus::InvalidHeader);
}

static ::Path<NWB::Core::Alloc::GlobalArena> TelemetryTestStorageDirectory(NWB::Core::Alloc::GlobalArena& arena){
    ::Path<NWB::Core::Alloc::GlobalArena> executableDirectory(arena);
    if(GetExecutableDirectory(executableDirectory))
        return executableDirectory / "telemetry_test_storage";

    return ::Path<NWB::Core::Alloc::GlobalArena>(arena, "telemetry_test_storage");
}

TEST(Telemetry, CaptureSessionCaptureScopeRecordsLogAndDiagnostic){
    TestArena testArena;
    Telemetry::CaptureSession session(testArena.arena);
    session.setCaptureOptions(Telemetry::CaptureOptions::All());
    session.setFrameIndex(610u);
    session.setStreamId(28u);

    NWB::Tests::CapturingLogger previousLogger;
    {
        NWB::Core::Common::LoggerRegistrationGuard previousRegistration(previousLogger);
        {
            Telemetry::CaptureSessionCaptureScope captureScope(session);

            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("scope text"));
            CaptureDiagnosticEvent(DiagnosticEventRecord{
                .event = DiagnosticEventName::s_Error,
                .category = "scope_diagnostic",
                .message = "scope diagnostic",
                .file = "scope.cpp",
                .line = 67u,
            });
        }

        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("after scope"));
    }

    CaptureDiagnosticEvent(DiagnosticEventRecord{
        .event = DiagnosticEventName::s_Error,
        .category = "scope_diagnostic",
        .message = "ignored after scope",
    });

    EXPECT_EQ(previousLogger.messageCount(), 2u);
    EXPECT_TRUE(previousLogger.sawMessageContaining(NWB_TEXT("scope text")));
    EXPECT_TRUE(previousLogger.sawMessageContaining(NWB_TEXT("after scope")));
    EXPECT_EQ(session.eventCount(), 2u);

    const Telemetry::EventRecord* logEvent = session.view().eventAt(0u);
    const Telemetry::EventRecord* diagnosticEvent = session.view().eventAt(1u);
    ASSERT_NE(logEvent, nullptr);
    ASSERT_NE(diagnosticEvent, nullptr);

    EXPECT_EQ(logEvent->header.kind, Telemetry::EventKind::TextLog);
    EXPECT_EQ(diagnosticEvent->header.kind, Telemetry::EventKind::Diagnostic);
    EXPECT_EQ(logEvent->header.frameIndex, 610u);
    EXPECT_EQ(diagnosticEvent->header.frameIndex, 610u);
    EXPECT_EQ(logEvent->header.streamId, 28u);
    EXPECT_EQ(diagnosticEvent->header.streamId, 28u);

    Telemetry::TextLogPayload logPayload(testArena.arena);
    Telemetry::DiagnosticPayload diagnosticPayload(testArena.arena);
    EXPECT_TRUE(Telemetry::ParseTextLogPayload(testArena.arena, logEvent->payload.data(), logEvent->payload.size(), logPayload));
    EXPECT_TRUE(Telemetry::ParseDiagnosticPayload(testArena.arena, diagnosticEvent->payload.data(), diagnosticEvent->payload.size(), diagnosticPayload));
    EXPECT_EQ(logPayload.messageUtf8, "scope text");
    EXPECT_EQ(diagnosticPayload.category, "scope_diagnostic");
    EXPECT_EQ(diagnosticPayload.message, "scope diagnostic");
    EXPECT_EQ(diagnosticPayload.file, "scope.cpp");
    EXPECT_EQ(diagnosticPayload.line, 67u);
}

TEST(Telemetry, TextLogPayloadRoundTrip){
    TestArena testArena;
    Telemetry::TelemetryBytes payload(testArena.arena);

    EXPECT_TRUE(Telemetry::BuildTextLogPayload(
        testArena.arena,
        NWB::Core::Common::LogType::Warning,
        NWB_TEXT("telemetry text log"),
        payload
    ));
    EXPECT_EQ(payload.size(), sizeof(Telemetry::EncodedTextLogPayloadHeader) + sizeof("telemetry text log") - 1u);

    Telemetry::TextLogPayload parsed(testArena.arena);
    EXPECT_TRUE(Telemetry::ParseTextLogPayload(testArena.arena, payload.data(), payload.size(), parsed));
    EXPECT_EQ(parsed.type, NWB::Core::Common::LogType::Warning);
    EXPECT_EQ(parsed.messageUtf8, "telemetry text log");

    payload[0u] = 0u;
    EXPECT_FALSE(Telemetry::ParseTextLogPayload(testArena.arena, payload.data(), payload.size(), parsed));
}

TEST(Telemetry, RecordTextLogUsesTelemetryEvent){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    EXPECT_TRUE(Telemetry::RecordTextLog(
        recorder,
        NWB::Core::Common::LogType::EssentialInfo,
        NWB_TEXT("captured text"),
        123u,
        9u
    ));

    const Telemetry::EventRecord* event = recorder.view().eventAt(0u);
    ASSERT_NE(event, nullptr);

    EXPECT_EQ(event->header.kind, Telemetry::EventKind::TextLog);
    EXPECT_EQ(event->header.frameIndex, 123u);
    EXPECT_EQ(event->header.streamId, 9u);

    Telemetry::TextLogPayload parsed(testArena.arena);
    EXPECT_TRUE(Telemetry::ParseTextLogPayload(testArena.arena, event->payload.data(), event->payload.size(), parsed));
    EXPECT_EQ(parsed.type, NWB::Core::Common::LogType::EssentialInfo);
    EXPECT_EQ(parsed.messageUtf8, "captured text");
}

TEST(Telemetry, TextLogCaptureLoggerForwardsAndRecords){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    NWB::Tests::CapturingLogger forwardLogger;
    Telemetry::TextLogCaptureLogger logger(recorder, &forwardLogger);
    logger.setFrameIndex(321u);
    logger.setStreamId(4u);

    logger.enqueue(NWB::Core::Common::LogString(NWB_TEXT("bridged warning"), logger.arena()), NWB::Core::Common::LogType::Warning);

    EXPECT_EQ(forwardLogger.messageCount(), 1u);
    EXPECT_EQ(forwardLogger.lastType(), NWB::Core::Common::LogType::Warning);
    EXPECT_TRUE(forwardLogger.sawMessageContaining(NWB_TEXT("bridged warning")));
    EXPECT_EQ(recorder.eventCount(), 1u);

    const Telemetry::EventRecord* event = recorder.view().eventAt(0u);
    ASSERT_NE(event, nullptr);

    EXPECT_EQ(event->header.kind, Telemetry::EventKind::TextLog);
    EXPECT_EQ(event->header.frameIndex, 321u);
    EXPECT_EQ(event->header.streamId, 4u);

    Telemetry::TextLogPayload parsed(testArena.arena);
    EXPECT_TRUE(Telemetry::ParseTextLogPayload(testArena.arena, event->payload.data(), event->payload.size(), parsed));
    EXPECT_EQ(parsed.type, NWB::Core::Common::LogType::Warning);
    EXPECT_EQ(parsed.messageUtf8, "bridged warning");
}

TEST(Telemetry, DiagnosticPayloadRoundTrip){
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

    EXPECT_TRUE(Telemetry::BuildDiagnosticPayload(testArena.arena, source, payload));
    EXPECT_GT(payload.size(), sizeof(Telemetry::EncodedDiagnosticPayloadHeader));

    Telemetry::DiagnosticPayload parsed(testArena.arena);
    EXPECT_TRUE(Telemetry::ParseDiagnosticPayload(testArena.arena, payload.data(), payload.size(), parsed));
    EXPECT_EQ(parsed.event, DiagnosticEventName::s_Error);
    EXPECT_EQ(parsed.category, "unit_category");
    EXPECT_EQ(parsed.expression, "value != nullptr");
    EXPECT_EQ(parsed.message, "diagnostic message");
    EXPECT_EQ(parsed.file, "diagnostic_test.cpp");
    EXPECT_EQ(parsed.instructionPointer, 0x1234u);
    EXPECT_EQ(parsed.line, 77u);
    EXPECT_TRUE(parsed.terminatesProcess);

    payload[0u] = 0u;
    EXPECT_FALSE(Telemetry::ParseDiagnosticPayload(testArena.arena, payload.data(), payload.size(), parsed));
}

TEST(Telemetry, RecordDiagnosticUsesTelemetryEvent){
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

    EXPECT_TRUE(Telemetry::RecordDiagnostic(recorder, source, 222u, 6u));

    const Telemetry::EventRecord* event = recorder.view().eventAt(0u);
    ASSERT_NE(event, nullptr);

    EXPECT_EQ(event->header.kind, Telemetry::EventKind::Diagnostic);
    EXPECT_EQ(event->header.frameIndex, 222u);
    EXPECT_EQ(event->header.streamId, 6u);

    Telemetry::DiagnosticPayload parsed(testArena.arena);
    EXPECT_TRUE(Telemetry::ParseDiagnosticPayload(testArena.arena, event->payload.data(), event->payload.size(), parsed));
    EXPECT_EQ(parsed.event, DiagnosticEventName::s_Assert);
    EXPECT_EQ(parsed.category, DiagnosticEventCategory::s_Assert);
    EXPECT_EQ(parsed.message, "assert payload");
}

TEST(Telemetry, DiagnosticCaptureGuardRecordsGlobalDiagnostic){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    {
        Telemetry::DiagnosticCaptureGuard guard(recorder);
        EXPECT_TRUE(guard.installed());
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

    EXPECT_EQ(recorder.eventCount(), 1u);

    const Telemetry::EventRecord* event = recorder.view().eventAt(0u);
    ASSERT_NE(event, nullptr);

    EXPECT_EQ(event->header.kind, Telemetry::EventKind::Diagnostic);
    EXPECT_EQ(event->header.frameIndex, 333u);
    EXPECT_EQ(event->header.streamId, 8u);

    Telemetry::DiagnosticPayload parsed(testArena.arena);
    EXPECT_TRUE(Telemetry::ParseDiagnosticPayload(testArena.arena, event->payload.data(), event->payload.size(), parsed));
    EXPECT_EQ(parsed.event, DiagnosticEventName::s_Error);
    EXPECT_EQ(parsed.category, "telemetry_guard");
    EXPECT_EQ(parsed.message, "captured diagnostic");
    EXPECT_EQ(parsed.file, "guard.cpp");
    EXPECT_EQ(parsed.line, 44u);
}

TEST(Telemetry, DiagnosticCaptureGuardManualCaptureReturnsStatus){
    TestArena testArena;
    Telemetry::Recorder disabledRecorder(testArena.arena);
    Telemetry::DiagnosticCaptureGuard disabledGuard(disabledRecorder);
    EXPECT_FALSE(disabledGuard.capture(DiagnosticEventRecord{
        .event = DiagnosticEventName::s_Error,
        .message = "disabled diagnostic",
    }));

    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());
    Telemetry::DiagnosticCaptureGuard guard(recorder);
    guard.setFrameIndex(444u);
    guard.setStreamId(5u);

    EXPECT_TRUE(guard.capture(DiagnosticEventRecord{
        .event = DiagnosticEventName::s_Error,
        .category = "manual_capture",
        .message = "manual diagnostic",
        .file = "manual.cpp",
        .line = 55u,
    }));
    EXPECT_EQ(recorder.eventCount(), 1u);

    const Telemetry::EventRecord* event = recorder.view().eventAt(0u);
    ASSERT_NE(event, nullptr);

    EXPECT_EQ(event->header.frameIndex, 444u);
    EXPECT_EQ(event->header.streamId, 5u);

    Telemetry::DiagnosticPayload parsed(testArena.arena);
    EXPECT_TRUE(Telemetry::ParseDiagnosticPayload(testArena.arena, event->payload.data(), event->payload.size(), parsed));
    EXPECT_EQ(parsed.category, "manual_capture");
    EXPECT_EQ(parsed.message, "manual diagnostic");
}

TEST(Telemetry, DiagnosticCaptureGuardDoesNotReplaceExistingCallback){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    s_ExistingDiagnosticCallbackCount = 0u;
    SetDiagnosticEventCallback(ExistingDiagnosticCallback);
    {
        Telemetry::DiagnosticCaptureGuard guard(recorder);
        EXPECT_FALSE(guard.installed());
        CaptureDiagnosticEvent(DiagnosticEventRecord{
            .event = DiagnosticEventName::s_Error,
            .message = "existing callback should keep ownership",
        });
    }
    ClearDiagnosticEventCallback(ExistingDiagnosticCallback);

    EXPECT_EQ(s_ExistingDiagnosticCallbackCount, 1u);
    EXPECT_EQ(recorder.eventCount(), 0u);
}

TEST(Telemetry, RecorderAcceptsConcurrentRecords){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    constexpr u32 threadCount = 4u;
    constexpr u32 eventsPerThread = 64u;
    std::thread threads[threadCount];
    for(u32 threadIndex = 0u; threadIndex < threadCount; ++threadIndex){
        threads[threadIndex] = std::thread([&recorder, threadIndex](){
            for(u32 eventIndex = 0u; eventIndex < eventsPerThread; ++eventIndex){
                if(!Telemetry::RecordTextLog(
                    recorder,
                    NWB::Core::Common::LogType::Info,
                    NWB_TEXT("concurrent telemetry record"),
                    eventIndex,
                    threadIndex
                ))
                    return;
            }
        });
    }

    for(std::thread& thread : threads)
        thread.join();

    EXPECT_EQ(recorder.eventCount(), threadCount * eventsPerThread);
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

class PendingNameFrameGraphContributor final : public Telemetry::IFrameGraphContributor{
public:
    virtual bool appendFrameGraph(Telemetry::FrameGraphBuilder& builder)override{
        const Telemetry::FrameGraphNodeHandle source = builder.addPass(Name("source"), "Source");
        const Telemetry::FrameGraphNodeHandle target = builder.addResource(Name("target"), "Target");
        const Telemetry::FrameGraphNodeHandle duplicateTarget = builder.addResource(Name("target"), "Duplicate Target");
        if(!target.valid() || !duplicateTarget.valid())
            return false;

        builder.dependsOnByName(source, Name("target"), 7u);
        builder.dependsOnByName(source, Name("missing"), 9u);
        return true;
    }
};

TEST(Telemetry, FrameGraphRegistryResolvesPendingNameEdges){
    TestArena testArena;
    Telemetry::CaptureSession session(testArena.arena);
    session.setCaptureOptions(Telemetry::CaptureOptions::FrameGraphOnly());

    Telemetry::FrameGraphRegistry registry(testArena.arena);
    PendingNameFrameGraphContributor contributor;
    registry.registerContributor(contributor);

    EXPECT_TRUE(registry.record(session));
    EXPECT_EQ(session.eventCount(), 1u);

    const Telemetry::EventRecord* event = session.view().eventAt(0u);
    ASSERT_NE(event, nullptr);

    Telemetry::FrameGraphPayload parsed(testArena.arena);
    EXPECT_TRUE(Telemetry::ParseFrameGraphPayload(testArena.arena, event->payload.data(), event->payload.size(), parsed));
    ASSERT_EQ(parsed.nodes.size(), 3u);
    ASSERT_EQ(parsed.edges.size(), 1u);
    EXPECT_EQ(parsed.edges[0u].fromNodeIndex, 0u);
    EXPECT_EQ(parsed.edges[0u].toNodeIndex, 1u);
    EXPECT_EQ(parsed.edges[0u].kind, Telemetry::FrameGraphEdgeKind::DependsOn);
    EXPECT_EQ(parsed.edges[0u].flags, 7u);
}

TEST(Telemetry, FrameGraphPayloadRoundTrip){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestFrameGraph(testArena.arena, nodes, edges);

    Telemetry::TelemetryBytes payload(testArena.arena);
    EXPECT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 905u, nodes, edges, payload));
    EXPECT_EQ(payload.size(), sizeof(Telemetry::EncodedFrameGraphPayloadHeader)
            + (sizeof(Telemetry::EncodedFrameGraphNode) * nodes.size())
            + (sizeof(Telemetry::EncodedFrameGraphEdge) * edges.size())
            + sizeof("GBuffer Pass")
            + sizeof("Albedo Texture")
            + sizeof("Lighting Pass"));

    Telemetry::FrameGraphPayload parsed(testArena.arena);
    EXPECT_TRUE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));
    EXPECT_EQ(parsed.frameIndex, 905u);
    ASSERT_EQ(parsed.nodes.size(), 3u);
    ASSERT_EQ(parsed.edges.size(), 2u);
    EXPECT_EQ(parsed.nodes[0u].name, Name("gbuffer"));
    EXPECT_EQ(parsed.nodes[0u].label, "GBuffer Pass");
    EXPECT_EQ(parsed.nodes[0u].kind, Telemetry::FrameGraphNodeKind::Pass);
    EXPECT_EQ(parsed.nodes[0u].flags, 1u);
    EXPECT_EQ(parsed.nodes[1u].name, Name("albedo"));
    EXPECT_EQ(parsed.nodes[1u].label, "Albedo Texture");
    EXPECT_EQ(parsed.nodes[1u].kind, Telemetry::FrameGraphNodeKind::Resource);
    EXPECT_EQ(parsed.nodes[2u].name, Name("lighting"));
    EXPECT_EQ(parsed.nodes[2u].label, "Lighting Pass");
    EXPECT_EQ(parsed.edges[0u].fromNodeIndex, 0u);
    EXPECT_EQ(parsed.edges[0u].toNodeIndex, 1u);
    EXPECT_EQ(parsed.edges[0u].kind, Telemetry::FrameGraphEdgeKind::Writes);
    EXPECT_EQ(parsed.edges[1u].fromNodeIndex, 1u);
    EXPECT_EQ(parsed.edges[1u].toNodeIndex, 2u);
    EXPECT_EQ(parsed.edges[1u].kind, Telemetry::FrameGraphEdgeKind::Reads);
    EXPECT_EQ(parsed.edges[1u].flags, 2u);

    payload[0u] = 0u;
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));
}

TEST(Telemetry, FrameGraphPayloadRejectsInvalidInput){
    TestArena testArena;
    Telemetry::TelemetryBytes payload(testArena.arena);
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestFrameGraph(testArena.arena, nodes, edges);

    nodes[0u].kind = Telemetry::FrameGraphNodeKind::Unknown;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(testArena.arena, 1u, nodes, edges, payload));

    BuildTestFrameGraph(testArena.arena, nodes, edges);
    nodes[0u].label = AStringView("bad\0label", 9u);
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(testArena.arena, 1u, nodes, edges, payload));

    BuildTestFrameGraph(testArena.arena, nodes, edges);
    edges[0u].toNodeIndex = 99u;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(testArena.arena, 1u, nodes, edges, payload));
}

TEST(Telemetry, RecordFrameGraphUsesTelemetryEvent){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestFrameGraph(testArena.arena, nodes, edges);

    EXPECT_TRUE(Telemetry::RecordFrameGraph(recorder, 909u, nodes, edges, 14u));

    const Telemetry::EventRecord* event = recorder.view().eventAt(0u);
    ASSERT_NE(event, nullptr);

    EXPECT_EQ(event->header.kind, Telemetry::EventKind::FrameGraphFrame);
    EXPECT_EQ(event->header.frameIndex, 909u);
    EXPECT_EQ(event->header.streamId, 14u);

    Telemetry::FrameGraphPayload parsed(testArena.arena);
    EXPECT_TRUE(Telemetry::ParseFrameGraphPayload(testArena.arena, event->payload.data(), event->payload.size(), parsed));
    EXPECT_EQ(parsed.frameIndex, 909u);
    EXPECT_EQ(parsed.nodes.size(), 3u);
    EXPECT_EQ(parsed.edges.size(), 2u);
}

TEST(Telemetry, CaptureSessionRecordsFrameGraphWithContext){
    TestArena testArena;
    Telemetry::CaptureSession session(testArena.arena);
    session.setCaptureOptions(Telemetry::CaptureOptions::All());
    session.setFrameIndex(910u);
    session.setStreamId(15u);

    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestFrameGraph(testArena.arena, nodes, edges);

    EXPECT_TRUE(session.recordFrameGraph(nodes, edges));

    const Telemetry::EventRecord* event = session.view().eventAt(0u);
    ASSERT_NE(event, nullptr);

    EXPECT_EQ(event->header.kind, Telemetry::EventKind::FrameGraphFrame);
    EXPECT_EQ(event->header.frameIndex, 910u);
    EXPECT_EQ(event->header.streamId, 15u);

    Telemetry::FrameGraphPayload parsed(testArena.arena);
    EXPECT_TRUE(Telemetry::ParseFrameGraphPayload(testArena.arena, event->payload.data(), event->payload.size(), parsed));
    EXPECT_EQ(parsed.frameIndex, 910u);
    EXPECT_EQ(parsed.nodes.size(), 3u);
    EXPECT_EQ(parsed.edges.size(), 2u);
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

TEST(Telemetry, PerfTimingPayloadRoundTrip){
    TestArena testArena;
    const Name scopeName("renderer/frame");
    const NWB::Core::Perf::TimingStats stats = MakeTestTimingStats();

    Telemetry::TelemetryBytes payload(testArena.arena);
    EXPECT_TRUE(Telemetry::BuildPerfTimingPayload(
        testArena.arena,
        Telemetry::PerfTimingSource::Gpu,
        scopeName,
        "Renderer Frame",
        stats,
        payload
    ));
    EXPECT_EQ(payload.size(), sizeof(Telemetry::EncodedPerfTimingPayloadHeader) + sizeof("Renderer Frame") - 1u);

    Telemetry::PerfTimingPayload parsed(testArena.arena);
    EXPECT_TRUE(Telemetry::ParsePerfTimingPayload(testArena.arena, payload.data(), payload.size(), parsed));
    EXPECT_EQ(parsed.source, Telemetry::PerfTimingSource::Gpu);
    EXPECT_EQ(parsed.scopeName, scopeName);
    EXPECT_EQ(parsed.scopeText, "Renderer Frame");
    EXPECT_EQ(parsed.stats.seconds, stats.seconds);
    EXPECT_EQ(parsed.stats.sampleCount, stats.sampleCount);
    EXPECT_EQ(parsed.stats.publishFrameIndex, stats.publishFrameIndex);
    EXPECT_EQ(parsed.stats.firstSampleFrameIndex, stats.firstSampleFrameIndex);
    EXPECT_EQ(parsed.stats.lastSampleFrameIndex, stats.lastSampleFrameIndex);

    payload[0u] = 0u;
    EXPECT_FALSE(Telemetry::ParsePerfTimingPayload(testArena.arena, payload.data(), payload.size(), parsed));
}

TEST(Telemetry, PerfTimingPayloadRejectsInvalidInput){
    TestArena testArena;
    Telemetry::TelemetryBytes payload(testArena.arena);
    NWB::Core::Perf::TimingStats stats = MakeTestTimingStats();

    EXPECT_FALSE(Telemetry::BuildPerfTimingPayload(
        testArena.arena,
        Telemetry::PerfTimingSource::Unknown,
        Name("renderer/frame"),
        "Renderer Frame",
        stats,
        payload
    ));

    stats.sampleCount = 0u;
    EXPECT_FALSE(Telemetry::BuildPerfTimingPayload(
        testArena.arena,
        Telemetry::PerfTimingSource::Cpu,
        Name("renderer/frame"),
        "Renderer Frame",
        stats,
        payload
    ));
}

TEST(Telemetry, RecordPerfTimingUsesTelemetryEvent){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    const Name scopeName("cpu/update");
    const NWB::Core::Perf::TimingStats stats = MakeTestTimingStats();
    EXPECT_TRUE(Telemetry::RecordPerfTiming(recorder, Telemetry::PerfTimingSource::Cpu, scopeName, "cpu/update", stats, 11u));

    const Telemetry::EventRecord* event = recorder.view().eventAt(0u);
    ASSERT_NE(event, nullptr);

    EXPECT_EQ(event->header.kind, Telemetry::EventKind::PerfFrame);
    EXPECT_EQ(event->header.frameIndex, stats.publishFrameIndex);
    EXPECT_EQ(event->header.streamId, 11u);

    Telemetry::PerfTimingPayload parsed(testArena.arena);
    EXPECT_TRUE(Telemetry::ParsePerfTimingPayload(testArena.arena, event->payload.data(), event->payload.size(), parsed));
    EXPECT_EQ(parsed.source, Telemetry::PerfTimingSource::Cpu);
    EXPECT_EQ(parsed.scopeName, scopeName);
    EXPECT_EQ(parsed.scopeText, "cpu/update");
    EXPECT_EQ(parsed.stats.sampleCount, stats.sampleCount);
}

static NWB::Core::Perf::MemorySnapshot MakeTestMemorySnapshot(const Name& scopeName){
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

static NWB::Core::Perf::MemoryDelta MakeTestMemoryDelta(){
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

TEST(Telemetry, PerfMemoryPayloadRoundTrip){
    TestArena testArena;
    const Name scopeName("memory/project_arena");
    const NWB::Core::Perf::MemorySnapshot snapshot = MakeTestMemorySnapshot(scopeName);
    const NWB::Core::Perf::MemoryDelta delta = MakeTestMemoryDelta();

    Telemetry::TelemetryBytes payload(testArena.arena);
    EXPECT_TRUE(Telemetry::BuildPerfMemoryPayload(
        testArena.arena,
        scopeName,
        "Project Arena",
        snapshot,
        delta,
        payload
    ));
    EXPECT_EQ(payload.size(), sizeof(Telemetry::EncodedPerfMemoryPayloadHeader) + sizeof("Project Arena") - 1u);

    Telemetry::PerfMemoryPayload parsed(testArena.arena);
    EXPECT_TRUE(Telemetry::ParsePerfMemoryPayload(testArena.arena, payload.data(), payload.size(), parsed));
    EXPECT_EQ(parsed.scopeName, scopeName);
    EXPECT_EQ(parsed.scopeText, "Project Arena");
    EXPECT_EQ(parsed.snapshot.scopeName, scopeName);
    EXPECT_EQ(parsed.snapshot.frameIndex, snapshot.frameIndex);
    EXPECT_EQ(parsed.snapshot.reservedBytes, snapshot.reservedBytes);
    EXPECT_EQ(parsed.snapshot.usedBytes, snapshot.usedBytes);
    EXPECT_EQ(parsed.snapshot.peakUsedBytes, snapshot.peakUsedBytes);
    EXPECT_EQ(parsed.snapshot.allocationCount, snapshot.allocationCount);
    EXPECT_EQ(parsed.snapshot.reallocationCount, snapshot.reallocationCount);
    EXPECT_EQ(parsed.snapshot.deallocationCount, snapshot.deallocationCount);
    EXPECT_TRUE(parsed.delta.hasSamples);
    EXPECT_EQ(parsed.delta.previousFrameIndex, delta.previousFrameIndex);
    EXPECT_EQ(parsed.delta.currentFrameIndex, snapshot.frameIndex);
    EXPECT_EQ(parsed.delta.reservedBytes, delta.reservedBytes);
    EXPECT_EQ(parsed.delta.usedBytes, delta.usedBytes);
    EXPECT_EQ(parsed.delta.peakUsedBytes, delta.peakUsedBytes);
    EXPECT_EQ(parsed.delta.allocationCount, delta.allocationCount);
    EXPECT_EQ(parsed.delta.reallocationCount, delta.reallocationCount);
    EXPECT_EQ(parsed.delta.deallocationCount, delta.deallocationCount);

    payload[0u] = 0u;
    EXPECT_FALSE(Telemetry::ParsePerfMemoryPayload(testArena.arena, payload.data(), payload.size(), parsed));
}

TEST(Telemetry, PerfMemoryPayloadRejectsInvalidInput){
    TestArena testArena;
    Telemetry::TelemetryBytes payload(testArena.arena);
    const Name scopeName("memory/project_arena");
    NWB::Core::Perf::MemorySnapshot snapshot = MakeTestMemorySnapshot(scopeName);
    NWB::Core::Perf::MemoryDelta delta = MakeTestMemoryDelta();

    EXPECT_FALSE(Telemetry::BuildPerfMemoryPayload(
        testArena.arena,
        NAME_NONE,
        "Project Arena",
        snapshot,
        delta,
        payload
    ));

    EXPECT_FALSE(Telemetry::BuildPerfMemoryPayload(
        testArena.arena,
        scopeName,
        AStringView(),
        snapshot,
        delta,
        payload
    ));

    snapshot.scopeName = Name("memory/other_arena");
    EXPECT_FALSE(Telemetry::BuildPerfMemoryPayload(
        testArena.arena,
        scopeName,
        "Project Arena",
        snapshot,
        delta,
        payload
    ));

    snapshot = MakeTestMemorySnapshot(scopeName);
    delta.currentFrameIndex = 99u;
    EXPECT_FALSE(Telemetry::BuildPerfMemoryPayload(
        testArena.arena,
        scopeName,
        "Project Arena",
        snapshot,
        delta,
        payload
    ));
}

TEST(Telemetry, RecordPerfMemoryUsesTelemetryEvent){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    const Name scopeName("memory/project_arena");
    const NWB::Core::Perf::MemorySnapshot snapshot = MakeTestMemorySnapshot(scopeName);
    const NWB::Core::Perf::MemoryDelta delta = MakeTestMemoryDelta();
    EXPECT_TRUE(Telemetry::RecordPerfMemory(recorder, scopeName, "memory/project_arena", snapshot, delta, 12u));

    const Telemetry::EventRecord* event = recorder.view().eventAt(0u);
    ASSERT_NE(event, nullptr);

    EXPECT_EQ(event->header.kind, Telemetry::EventKind::MemoryFrame);
    EXPECT_EQ(event->header.frameIndex, snapshot.frameIndex);
    EXPECT_EQ(event->header.streamId, 12u);

    Telemetry::PerfMemoryPayload parsed(testArena.arena);
    EXPECT_TRUE(Telemetry::ParsePerfMemoryPayload(testArena.arena, event->payload.data(), event->payload.size(), parsed));
    EXPECT_EQ(parsed.scopeName, scopeName);
    EXPECT_EQ(parsed.scopeText, "memory/project_arena");
    EXPECT_EQ(parsed.snapshot.usedBytes, snapshot.usedBytes);
    EXPECT_TRUE(parsed.delta.hasSamples);
    EXPECT_EQ(parsed.delta.usedBytes, delta.usedBytes);
}

static NWB::Core::Alloc::ArenaMemoryStats MakeTestArenaStats(
    const u64 reservedBytes,
    const u64 usedBytes,
    const u64 peakUsedBytes,
    const u64 allocationCount,
    const u64 reallocationCount,
    const u64 deallocationCount
){
    NWB::Core::Alloc::ArenaMemoryStats stats;
    stats.reservedBytes = reservedBytes;
    stats.usedBytes = usedBytes;
    stats.peakUsedBytes = peakUsedBytes;
    stats.allocationCount = allocationCount;
    stats.reallocationCount = reallocationCount;
    stats.deallocationCount = deallocationCount;
    return stats;
}

static void BuildTestPerfReport(
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

TEST(Telemetry, PerfViewsExposeScopes){
    TestArena testArena;
    NWB::Core::Perf::TimingRecorder cpuTiming(testArena.arena);
    NWB::Core::Perf::TimingRecorder gpuTiming(testArena.arena);
    NWB::Core::Perf::MemoryRecorder memory(testArena.arena);
    NWB::Core::Perf::SessionReport report;
    BuildTestPerfReport(cpuTiming, gpuTiming, memory, report);

    EXPECT_EQ(report.cpuTiming.scopeCount(), 1u);
    EXPECT_EQ(report.gpuTiming.scopeCount(), 1u);
    EXPECT_EQ(report.memory.scopeCount(), 1u);
    EXPECT_EQ(report.cpuTiming.scopeNameAt(0u), Name("perf/cpu/update"));
    EXPECT_EQ(report.gpuTiming.scopeNameAt(0u), Name("perf/gpu/frame"));
    EXPECT_EQ(report.memory.scopeNameAt(0u), Name("perf/memory/project"));
    EXPECT_TRUE(report.cpuTiming.scopeAt(0u).valid());
    EXPECT_FALSE(report.cpuTiming.scopeAt(1u).valid());
    EXPECT_EQ(report.cpuTiming.statsAt(0u).sampleCount, 2u);
    EXPECT_EQ(report.gpuTiming.statsAt(0u).sampleCount, 1u);
    EXPECT_EQ(report.memory.snapshotAt(0u).usedBytes, 2048u);
    EXPECT_EQ(report.memory.deltaAt(0u).usedBytes, 1024u);
}

TEST(Telemetry, RecordPerfSessionReportUsesTelemetryEvents){
    TestArena testArena;
    NWB::Core::Perf::TimingRecorder cpuTiming(testArena.arena);
    NWB::Core::Perf::TimingRecorder gpuTiming(testArena.arena);
    NWB::Core::Perf::MemoryRecorder memory(testArena.arena);
    NWB::Core::Perf::SessionReport report;
    BuildTestPerfReport(cpuTiming, gpuTiming, memory, report);

    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    const Telemetry::PerfSessionRecordResult result = Telemetry::RecordPerfSessionReport(recorder, report, 17u);
    EXPECT_TRUE(result.recordedAny());
    EXPECT_EQ(result.cpuTimingEvents, 1u);
    EXPECT_EQ(result.gpuTimingEvents, 1u);
    EXPECT_EQ(result.memoryEvents, 1u);
    EXPECT_EQ(result.eventCount(), 3u);
    EXPECT_EQ(recorder.eventCount(), 3u);

    const Telemetry::EventRecord* cpuEvent = recorder.view().eventAt(0u);
    const Telemetry::EventRecord* gpuEvent = recorder.view().eventAt(1u);
    const Telemetry::EventRecord* memoryEvent = recorder.view().eventAt(2u);
    ASSERT_NE(cpuEvent, nullptr);
    ASSERT_NE(gpuEvent, nullptr);
    ASSERT_NE(memoryEvent, nullptr);

    EXPECT_EQ(cpuEvent->header.kind, Telemetry::EventKind::PerfFrame);
    EXPECT_EQ(gpuEvent->header.kind, Telemetry::EventKind::PerfFrame);
    EXPECT_EQ(memoryEvent->header.kind, Telemetry::EventKind::MemoryFrame);
    EXPECT_EQ(cpuEvent->header.streamId, 17u);
    EXPECT_EQ(gpuEvent->header.streamId, 17u);
    EXPECT_EQ(memoryEvent->header.streamId, 17u);

    Telemetry::PerfTimingPayload cpuPayload(testArena.arena);
    Telemetry::PerfTimingPayload gpuPayload(testArena.arena);
    Telemetry::PerfMemoryPayload memoryPayload(testArena.arena);
    EXPECT_TRUE(Telemetry::ParsePerfTimingPayload(testArena.arena, cpuEvent->payload.data(), cpuEvent->payload.size(), cpuPayload));
    EXPECT_TRUE(Telemetry::ParsePerfTimingPayload(testArena.arena, gpuEvent->payload.data(), gpuEvent->payload.size(), gpuPayload));
    EXPECT_TRUE(Telemetry::ParsePerfMemoryPayload(testArena.arena, memoryEvent->payload.data(), memoryEvent->payload.size(), memoryPayload));
    EXPECT_EQ(cpuPayload.source, Telemetry::PerfTimingSource::Cpu);
    EXPECT_EQ(gpuPayload.source, Telemetry::PerfTimingSource::Gpu);
    EXPECT_EQ(cpuPayload.scopeName, Name("perf/cpu/update"));
    EXPECT_EQ(gpuPayload.scopeName, Name("perf/gpu/frame"));
    EXPECT_EQ(memoryPayload.scopeName, Name("perf/memory/project"));
    EXPECT_EQ(memoryPayload.snapshot.frameIndex, 102u);
    EXPECT_TRUE(memoryPayload.delta.hasSamples);
}

TEST(Telemetry, CaptureSessionRecordsPerfReport){
    TestArena testArena;
    NWB::Core::Perf::TimingRecorder cpuTiming(testArena.arena);
    NWB::Core::Perf::TimingRecorder gpuTiming(testArena.arena);
    NWB::Core::Perf::MemoryRecorder memory(testArena.arena);
    NWB::Core::Perf::SessionReport report;
    BuildTestPerfReport(cpuTiming, gpuTiming, memory, report);

    Telemetry::CaptureSession session(testArena.arena);
    session.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());

    const Telemetry::PerfSessionRecordResult result = session.recordPerfReport(report, 23u);
    EXPECT_EQ(result.eventCount(), 3u);
    EXPECT_EQ(session.eventCount(), 3u);

    const Telemetry::EventRecord* cpuEvent = session.view().eventAt(0u);
    const Telemetry::EventRecord* gpuEvent = session.view().eventAt(1u);
    const Telemetry::EventRecord* memoryEvent = session.view().eventAt(2u);
    ASSERT_NE(cpuEvent, nullptr);
    ASSERT_NE(gpuEvent, nullptr);
    ASSERT_NE(memoryEvent, nullptr);

    EXPECT_EQ(cpuEvent->header.kind, Telemetry::EventKind::PerfFrame);
    EXPECT_EQ(gpuEvent->header.kind, Telemetry::EventKind::PerfFrame);
    EXPECT_EQ(memoryEvent->header.kind, Telemetry::EventKind::MemoryFrame);
    EXPECT_EQ(cpuEvent->header.streamId, 23u);
    EXPECT_EQ(gpuEvent->header.streamId, 23u);
    EXPECT_EQ(memoryEvent->header.streamId, 23u);
}

static bool ContainsText(const AStringView text, const AStringView needle){
    return text.find(needle) != AStringView::npos;
}

TEST(Telemetry, TelemetryReportSummarizesBenchmarkEvents){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    EXPECT_TRUE(Telemetry::RecordTextLog(
        recorder,
        NWB::Core::Common::LogType::Info,
        NWB_TEXT("benchmark report"),
        4u,
        1u
    ));

    const Name cpuScopeName("cpu/update");
    const NWB::Core::Perf::TimingStats stats = MakeTestTimingStats();
    EXPECT_TRUE(Telemetry::RecordPerfTiming(recorder, Telemetry::PerfTimingSource::Cpu, cpuScopeName, "cpu/update", stats, 2u));

    const Name memoryScopeName("memory/project_arena");
    const NWB::Core::Perf::MemorySnapshot snapshot = MakeTestMemorySnapshot(memoryScopeName);
    const NWB::Core::Perf::MemoryDelta delta = MakeTestMemoryDelta();
    EXPECT_TRUE(Telemetry::RecordPerfMemory(recorder, memoryScopeName, "memory/project_arena", snapshot, delta, 3u));

    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestFrameGraph(testArena.arena, nodes, edges);
    EXPECT_TRUE(Telemetry::RecordFrameGraph(recorder, 909u, nodes, edges, 4u));

    Log::TelemetryReport report(testArena.arena);
    EXPECT_TRUE(Log::BuildTelemetryReport(testArena.arena, recorder.view(), report));

    EXPECT_EQ(report.summary.eventCount, 4u);
    EXPECT_EQ(report.summary.eventKindCounts[static_cast<usize>(Telemetry::EventKind::TextLog)], 1u);
    EXPECT_EQ(report.summary.eventKindCounts[static_cast<usize>(Telemetry::EventKind::PerfFrame)], 1u);
    EXPECT_EQ(report.summary.eventKindCounts[static_cast<usize>(Telemetry::EventKind::MemoryFrame)], 1u);
    EXPECT_EQ(report.summary.eventKindCounts[static_cast<usize>(Telemetry::EventKind::FrameGraphFrame)], 1u);
    EXPECT_EQ(report.summary.parseFailureCount, 0u);
    EXPECT_TRUE(report.summary.hasFrameRange);
    EXPECT_EQ(report.summary.minFrameIndex, 4u);
    EXPECT_EQ(report.summary.maxFrameIndex, 909u);
    EXPECT_EQ(report.summary.cpuTimingEventCount, 1u);
    EXPECT_EQ(report.summary.cpuTimingSampleCount, stats.sampleCount);
    EXPECT_EQ(report.summary.cpuTimingSeconds, stats.seconds);
    EXPECT_EQ(report.summary.memoryEventCount, 1u);
    EXPECT_EQ(report.summary.maxMemoryUsedBytes, snapshot.usedBytes);
    EXPECT_EQ(report.summary.totalMemoryUsedDeltaBytes, delta.usedBytes);
    EXPECT_EQ(report.summary.frameGraphFrameCount, 1u);
    EXPECT_EQ(report.summary.frameGraphNodeCount, 3u);
    EXPECT_EQ(report.summary.frameGraphEdgeCount, 2u);
    EXPECT_TRUE(ContainsText(AStringView(report.json.data(), report.json.size()), "\"eventCount\": 4"));
    EXPECT_TRUE(ContainsText(AStringView(report.perfCsv.data(), report.perfCsv.size()), "source,scope,publish_frame"));
    EXPECT_TRUE(ContainsText(AStringView(report.perfCsv.data(), report.perfCsv.size()), "cpu,cpu/update"));
}

TEST(Telemetry, TelemetryIngestStoresRawAndReports){
    TestArena testArena;
    const ::Path<NWB::Core::Alloc::GlobalArena> storageDirectory = TelemetryTestStorageDirectory(testArena.arena) / "ingest";

    ErrorCode error;
    EXPECT_TRUE(RemoveAllIfExists(storageDirectory, error));

    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());
    EXPECT_TRUE(Telemetry::RecordTextLog(
        recorder,
        NWB::Core::Common::LogType::Info,
        NWB_TEXT("ingest log"),
        10u,
        1u
    ));

    const Name cpuScopeName("ingest/cpu");
    const NWB::Core::Perf::TimingStats stats = MakeTestTimingStats();
    EXPECT_TRUE(Telemetry::RecordPerfTiming(recorder, Telemetry::PerfTimingSource::Cpu, cpuScopeName, "ingest/cpu", stats, 2u));

    Telemetry::TelemetryBytes encoded(testArena.arena);
    EXPECT_TRUE(Telemetry::EncodeEventStream(recorder.view(), encoded));

    Log::TelemetryIngestConfig config(testArena.arena);
    config.storageDirectory = storageDirectory;
    const Log::TelemetryIngestResult result = Log::ProcessTelemetryUpload(testArena.arena, encoded.data(), encoded.size(), config);

    EXPECT_TRUE(result.ok());
    EXPECT_TRUE(result.decode.ok());
    EXPECT_EQ(result.decode.bytesRead, encoded.size());
    EXPECT_EQ(result.summary.eventCount, 2u);
    EXPECT_EQ(result.summary.cpuTimingEventCount, 1u);
    EXPECT_TRUE(FileExists(result.rawPath, error));
    EXPECT_FALSE(error);
    error.clear();
    EXPECT_TRUE(FileExists(result.jsonPath, error));
    EXPECT_FALSE(error);
    error.clear();
    EXPECT_TRUE(FileExists(result.perfCsvPath, error));
    EXPECT_FALSE(error);

    AString<NWB::Core::Alloc::GlobalArena> perfCsv(testArena.arena);
    EXPECT_TRUE(ReadTextFile(result.perfCsvPath, perfCsv));
    EXPECT_TRUE(ContainsText(AStringView(perfCsv.data(), perfCsv.size()), "cpu,ingest/cpu"));

    EXPECT_TRUE(RemoveAllIfExists(storageDirectory, error));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

