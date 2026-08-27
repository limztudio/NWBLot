// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>
#include <gtest/gtest.h>
#include <tests/common/capturing_logger.h>

#include <core/telemetry/module.h>
#include <core/telemetry/frame_graph_registry.h>
#include <logger/telemetry/ingest.h>
#include <logger/telemetry/report.h>

#include <global/filesystem/operations.h>
#include <global/thread.h>


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

#if !defined(NWB_FINAL)
static AtomicFlag s_DiagnosticCaptureGuardLoaded;
static AtomicFlag s_DiagnosticCaptureRelease;
static AtomicFlag s_DiagnosticCaptureGuardDestructionWaiting;

static void DiagnosticCaptureGuardLifetimeHook(const Telemetry::DiagnosticCaptureTestHookStage::Enum stage)noexcept{
    switch(stage){
    case Telemetry::DiagnosticCaptureTestHookStage::AfterGuardLoad:
        s_DiagnosticCaptureGuardLoaded.test_and_set(MemoryOrder::release);
        s_DiagnosticCaptureGuardLoaded.notify_all();
        while(!s_DiagnosticCaptureRelease.test(MemoryOrder::acquire))
            s_DiagnosticCaptureRelease.wait(false, MemoryOrder::acquire);
        return;
    case Telemetry::DiagnosticCaptureTestHookStage::WaitingForActiveCallback:
        s_DiagnosticCaptureGuardDestructionWaiting.test_and_set(MemoryOrder::release);
        s_DiagnosticCaptureGuardDestructionWaiting.notify_all();
        return;
    }
}
#endif


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

TEST(Telemetry, RecorderMovesOwnedPayload){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());

    Telemetry::TelemetryBytes payload(testArena.arena);
    payload.push_back(4u);
    payload.push_back(5u);
    payload.push_back(6u);
    const u8* const payloadData = payload.data();

    EXPECT_TRUE(recorder.recordPayload(Telemetry::EventKind::PerfFrame, 13u, Move(payload), 7u));

    const Telemetry::EventRecord* record = recorder.view().eventAt(0u);
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->payload.data(), payloadData);
    EXPECT_EQ(record->header.payloadBytes, 3u);
    EXPECT_EQ(record->payload.size(), 3u);
    EXPECT_EQ(record->payload[0u], 4u);
    EXPECT_EQ(record->payload[1u], 5u);
    EXPECT_EQ(record->payload[2u], 6u);
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
                .event = DiagnosticEventName::s_Error.data(),
                .category = "scope_diagnostic",
                .message = "scope diagnostic",
                .file = "scope.cpp",
                .line = 67u,
            });
        }

        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("after scope"));
    }

    CaptureDiagnosticEvent(DiagnosticEventRecord{
        .event = DiagnosticEventName::s_Error.data(),
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
        .event = DiagnosticEventName::s_Error.data(),
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
        .event = DiagnosticEventName::s_Assert.data(),
        .category = DiagnosticEventCategory::s_Assert.data(),
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
            .event = DiagnosticEventName::s_Error.data(),
            .category = "telemetry_guard",
            .message = "captured diagnostic",
            .file = "guard.cpp",
            .line = 44u,
        });
    }

    CaptureDiagnosticEvent(DiagnosticEventRecord{
        .event = DiagnosticEventName::s_Error.data(),
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
        .event = DiagnosticEventName::s_Error.data(),
        .message = "disabled diagnostic",
    }));

    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());
    Telemetry::DiagnosticCaptureGuard guard(recorder);
    guard.setFrameIndex(444u);
    guard.setStreamId(5u);

    EXPECT_TRUE(guard.capture(DiagnosticEventRecord{
        .event = DiagnosticEventName::s_Error.data(),
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
            .event = DiagnosticEventName::s_Error.data(),
            .message = "existing callback should keep ownership",
        });
    }
    ClearDiagnosticEventCallback(ExistingDiagnosticCallback);

    EXPECT_EQ(s_ExistingDiagnosticCallbackCount, 1u);
    EXPECT_EQ(recorder.eventCount(), 0u);
}

#if !defined(NWB_FINAL)
TEST(Telemetry, DiagnosticCaptureGuardDestructionWaitsForActiveCallback){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    auto guard = NWB::Core::MakeGlobalUnique<Telemetry::DiagnosticCaptureGuard>(testArena.arena, recorder);
    ASSERT_NE(guard, nullptr);
    ASSERT_TRUE(guard->installed());

    s_DiagnosticCaptureGuardLoaded.clear(MemoryOrder::release);
    s_DiagnosticCaptureRelease.clear(MemoryOrder::release);
    s_DiagnosticCaptureGuardDestructionWaiting.clear(MemoryOrder::release);
    Telemetry::SetDiagnosticCaptureTestHook(DiagnosticCaptureGuardLifetimeHook);

    Thread captureThread([](){
        CaptureDiagnosticEvent(DiagnosticEventRecord{
            .event = DiagnosticEventName::s_Error.data(),
            .category = "telemetry_guard",
            .message = "capture during destruction",
        });
    });

    while(!s_DiagnosticCaptureGuardLoaded.test(MemoryOrder::acquire))
        s_DiagnosticCaptureGuardLoaded.wait(false, MemoryOrder::acquire);

    AtomicFlag destructionFinished;
    Thread destructionThread([&guard, &destructionFinished](){
        guard.reset();
        destructionFinished.test_and_set(MemoryOrder::release);
        destructionFinished.notify_all();
    });

    while(!s_DiagnosticCaptureGuardDestructionWaiting.test(MemoryOrder::acquire))
        s_DiagnosticCaptureGuardDestructionWaiting.wait(false, MemoryOrder::acquire);

    EXPECT_FALSE(destructionFinished.test(MemoryOrder::acquire));

    s_DiagnosticCaptureRelease.test_and_set(MemoryOrder::release);
    s_DiagnosticCaptureRelease.notify_all();

    captureThread.join();
    destructionThread.join();
    Telemetry::SetDiagnosticCaptureTestHook(nullptr);

    EXPECT_TRUE(destructionFinished.test(MemoryOrder::acquire));
    EXPECT_EQ(recorder.eventCount(), 1u);
}
#endif

TEST(Telemetry, RecorderAcceptsConcurrentRecords){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    constexpr u32 threadCount = 4u;
    constexpr u32 eventsPerThread = 64u;
    Thread threads[threadCount];
    for(u32 threadIndex = 0u; threadIndex < threadCount; ++threadIndex){
        threads[threadIndex] = Thread([&recorder, threadIndex](){
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

    for(Thread& thread : threads)
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
        .queueAssignment = {},
        .compiledTask = {},
        .runtimeStatistics = {},
    });
    nodes.push_back(Telemetry::FrameGraphNodeDesc{
        .name = Name("albedo"),
        .label = AStringView("Albedo Texture"),
        .kind = Telemetry::FrameGraphNodeKind::Resource,
        .flags = 0u,
        .queueAssignment = {},
        .compiledTask = {},
        .runtimeStatistics = {},
    });
    nodes.push_back(Telemetry::FrameGraphNodeDesc{
        .name = Name("lighting"),
        .label = AStringView("Lighting Pass"),
        .kind = Telemetry::FrameGraphNodeKind::Pass,
        .flags = 0u,
        .queueAssignment = {},
        .compiledTask = {},
        .runtimeStatistics = {},
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

static Telemetry::FrameGraphQueueAssignment MakeChangedFrameGraphQueueAssignment(){
    Telemetry::FrameGraphQueueAssignment assignment;
    assignment.initialQueue = { .index = 1u, .deviceGeneration = 17u };
    assignment.plannedQueue = { .index = 3u, .deviceGeneration = 17u };
    assignment.acceptedQueue = assignment.plannedQueue;
    assignment.previousAcceptedQueue = { .index = 2u, .deviceGeneration = 17u };
    assignment.score = {
        .preference = 11,
        .overlap = 7,
        .queueLoad = 3,
        .incomingCrossings = 2,
        .outgoingCrossings = 1,
        .ownershipTransfers = 4,
        .total = 8,
    };
    assignment.queueClass = Telemetry::FrameGraphQueueClass::Compute;
    assignment.reason = Telemetry::FrameGraphQueueAssignmentReason::Fallback;
    assignment.modifiers = Telemetry::FrameGraphQueueAssignmentModifier::All;
    assignment.acceptance = Telemetry::FrameGraphQueueAssignmentAcceptance::Changed;
    assignment.dedicated = true;
    assignment.present = true;
    return assignment;
}

static Telemetry::FrameGraphQueueAssignment MakeNotAcceptedFrameGraphQueueAssignment(){
    Telemetry::FrameGraphQueueAssignment assignment;
    assignment.initialQueue = { .index = 4u, .deviceGeneration = 17u };
    assignment.plannedQueue = { .index = 5u, .deviceGeneration = 17u };
    assignment.previousAcceptedQueue = { .index = 2u, .deviceGeneration = 17u };
    assignment.score = {
        .preference = 5,
        .overlap = 6,
        .queueLoad = 1,
        .incomingCrossings = 2,
        .outgoingCrossings = 3,
        .ownershipTransfers = 4,
        .total = 1,
    };
    assignment.queueClass = Telemetry::FrameGraphQueueClass::Transfer;
    assignment.reason = Telemetry::FrameGraphQueueAssignmentReason::ScoredAny;
    assignment.modifiers = Telemetry::FrameGraphQueueAssignmentModifier::TimingFeedback;
    assignment.acceptance = Telemetry::FrameGraphQueueAssignmentAcceptance::NotAccepted;
    assignment.present = true;
    return assignment;
}

static Telemetry::FrameGraphCompiledTask MakeFrameGraphCompiledTask(
    const u64 planGeneration,
    const u32 packetIndex,
    const Telemetry::FrameGraphTaskPacketizationDecision::Enum packetizationDecision
){
    return Telemetry::FrameGraphCompiledTask{
        .planGeneration = planGeneration,
        .packetIndex = packetIndex,
        .packetizationDecision = packetizationDecision,
        .present = true,
    };
}

static Telemetry::EncodedFrameGraphRuntimeStatistics EncodeTestFrameGraphRuntimeStatistics(
    const Telemetry::FrameGraphRuntimeStatistics& statistics,
    const u32 nodeIndex,
    const u16 reserved
){
    return Telemetry::EncodedFrameGraphRuntimeStatistics{
        .nodeIndex = nodeIndex,
        .deviceGeneration = statistics.deviceGeneration,
        .reserved = reserved,
        .graphGeneration = statistics.graphGeneration,
        .planGeneration = statistics.planGeneration,
        .recordingAttemptGeneration = statistics.recordingAttemptGeneration,
        .compile = {
            .taskCount = statistics.compile.taskCount,
            .resourceCount = statistics.compile.resourceCount,
            .resourceUseCount = statistics.compile.resourceUseCount,
            .explicitDependencyCount = statistics.compile.explicitDependencyCount,
            .inferredDependencyCount = statistics.compile.inferredDependencyCount,
            .packetCount = statistics.compile.packetCount,
            .packetDependencyCount = statistics.compile.packetDependencyCount,
            .mergedTaskCount = statistics.compile.mergedTaskCount,
            .transitionBarrierCount = statistics.compile.transitionBarrierCount,
            .uavBarrierCount = statistics.compile.uavBarrierCount,
            .ownershipReleaseBarrierCount = statistics.compile.ownershipReleaseBarrierCount,
            .ownershipAcquireBarrierCount = statistics.compile.ownershipAcquireBarrierCount,
            .stateExportBarrierCount = statistics.compile.stateExportBarrierCount,
            .logicalOwnershipTransferCount = statistics.compile.logicalOwnershipTransferCount,
            .logicalOwnershipTransferSignatureCount = statistics.compile.logicalOwnershipTransferSignatureCount,
            .repeatedOwnershipTransferSignatureCount = statistics.compile.repeatedOwnershipTransferSignatureCount,
            .concurrentSharingCouldAvoidTransferCount = statistics.compile.concurrentSharingCouldAvoidTransferCount,
            .concurrentSharingAdviceResourceCount = statistics.compile.concurrentSharingAdviceResourceCount,
            .logicalOwnershipTransferInternalCount = statistics.compile.logicalOwnershipTransferInternalCount,
            .logicalOwnershipTransferExternalImportCount = statistics.compile.logicalOwnershipTransferExternalImportCount,
            .logicalOwnershipTransferExternalExportCount = statistics.compile.logicalOwnershipTransferExternalExportCount,
            .resourceSetCount = statistics.compile.resourceSetCount,
            .resourceSetMemberCount = statistics.compile.resourceSetMemberCount,
            .directResourceUseCount = statistics.compile.directResourceUseCount,
            .declaredResourceSetUseCount = statistics.compile.declaredResourceSetUseCount,
            .expandedResourceSetMemberUseCount = statistics.compile.expandedResourceSetMemberUseCount,
            .payloadObjectCount = statistics.compile.payloadObjectCount,
            .payloadObjectBytes = statistics.compile.payloadObjectBytes,
            .uploadBlobCount = statistics.compile.uploadBlobCount,
            .uploadBlobBytes = statistics.compile.uploadBlobBytes,
            .declarationSeconds = statistics.compile.declarationSeconds,
            .analysisSeconds = statistics.compile.analysisSeconds,
            .validationSeconds = statistics.compile.validationSeconds,
            .dependencyAnalysisSeconds = statistics.compile.dependencyAnalysisSeconds,
            .hazardAnalysisSeconds = statistics.compile.hazardAnalysisSeconds,
            .topologicalOrderSeconds = statistics.compile.topologicalOrderSeconds,
            .queueAssignmentSeconds = statistics.compile.queueAssignmentSeconds,
            .planningSeconds = statistics.compile.planningSeconds,
            .packetizationSeconds = statistics.compile.packetizationSeconds,
            .resourceStatePlanningSeconds = statistics.compile.resourceStatePlanningSeconds,
            .packetDependencyPlanningSeconds = statistics.compile.packetDependencyPlanningSeconds,
            .totalSeconds = statistics.compile.totalSeconds,
        },
        .recording = {
            .packetCount = statistics.recording.packetCount,
            .taskCount = statistics.recording.taskCount,
            .commandListCount = statistics.recording.commandListCount,
            .barrierCount = statistics.recording.barrierCount,
            .workerRoutedPacketCount = statistics.recording.workerRoutedPacketCount,
            .parallelPacketCount = statistics.recording.parallelPacketCount,
            .commandListAcquisitionSeconds = statistics.recording.commandListAcquisitionSeconds,
            .graphBarrierRecordingSeconds = statistics.recording.graphBarrierRecordingSeconds,
            .taskRecordSeconds = statistics.recording.taskRecordSeconds,
            .recordingSeconds = statistics.recording.recordingSeconds,
            .recordingElapsedSeconds = statistics.recording.recordingElapsedSeconds,
            .readyFrontierElapsedSeconds = statistics.recording.readyFrontierElapsedSeconds,
            .readyFrontierWorkerBusySeconds = statistics.recording.readyFrontierWorkerBusySeconds,
            .readyFrontierWorkerCapacitySeconds = statistics.recording.readyFrontierWorkerCapacitySeconds,
        },
        .submission = {
            .acceptedPacketCount = statistics.submission.acceptedPacketCount,
            .acceptedTaskCount = statistics.submission.acceptedTaskCount,
            .rejectedPacketCount = statistics.submission.rejectedPacketCount,
            .rejectedTaskCount = statistics.submission.rejectedTaskCount,
            .nativeSubmissionCount = statistics.submission.nativeSubmissionCount,
            .rejectedSubmissionCount = statistics.submission.rejectedSubmissionCount,
            .nativeCommandListCount = statistics.submission.nativeCommandListCount,
            .plannedWaitTokenCount = statistics.submission.plannedWaitTokenCount,
            .sameQueueWaitElisionCount = statistics.submission.sameQueueWaitElisionCount,
            .timelineWaitCount = statistics.submission.timelineWaitCount,
            .mergedTimelineWaitCount = statistics.submission.mergedTimelineWaitCount,
            .acceptedFrontierSubmissionCount = statistics.submission.acceptedFrontierSubmissionCount,
            .submissionSeconds = statistics.submission.submissionSeconds,
        },
    };
}

static Telemetry::FrameGraphRuntimeStatistics MakeFrameGraphRuntimeStatistics(){
    return Telemetry::FrameGraphRuntimeStatistics{
        .graphGeneration = 51u,
        .planGeneration = 52u,
        .recordingAttemptGeneration = 53u,
        .deviceGeneration = 17u,
        .compile = {
            .taskCount = 78u,
            .resourceCount = 2u,
            .resourceUseCount = 50u,
            .explicitDependencyCount = 4u,
            .inferredDependencyCount = 5u,
            .packetCount = 76u,
            .packetDependencyCount = 7u,
            .mergedTaskCount = 2u,
            .transitionBarrierCount = 9u,
            .uavBarrierCount = 10u,
            .ownershipReleaseBarrierCount = 11u,
            .ownershipAcquireBarrierCount = 12u,
            .stateExportBarrierCount = 13u,
            .logicalOwnershipTransferCount = 60u,
            .logicalOwnershipTransferSignatureCount = 15u,
            .repeatedOwnershipTransferSignatureCount = 14u,
            .concurrentSharingCouldAvoidTransferCount = 17u,
            .concurrentSharingAdviceResourceCount = 2u,
            .logicalOwnershipTransferInternalCount = 19u,
            .logicalOwnershipTransferExternalImportCount = 20u,
            .logicalOwnershipTransferExternalExportCount = 21u,
            .resourceSetCount = 22u,
            .resourceSetMemberCount = 23u,
            .directResourceUseCount = 24u,
            .declaredResourceSetUseCount = 25u,
            .expandedResourceSetMemberUseCount = 26u,
            .payloadObjectCount = 27u,
            .payloadObjectBytes = 28u,
            .uploadBlobCount = 29u,
            .uploadBlobBytes = 30u,
            .declarationSeconds = 0.001,
            .analysisSeconds = 0.002,
            .validationSeconds = 0.003,
            .dependencyAnalysisSeconds = 0.004,
            .hazardAnalysisSeconds = 0.005,
            .topologicalOrderSeconds = 0.006,
            .queueAssignmentSeconds = 0.007,
            .planningSeconds = 0.008,
            .packetizationSeconds = 0.009,
            .resourceStatePlanningSeconds = 0.010,
            .packetDependencyPlanningSeconds = 0.011,
            .totalSeconds = 0.012,
        },
        .recording = {
            .packetCount = 31u,
            .taskCount = 32u,
            .commandListCount = 33u,
            .barrierCount = 34u,
            .workerRoutedPacketCount = 30u,
            .parallelPacketCount = 29u,
            .commandListAcquisitionSeconds = 0.013,
            .graphBarrierRecordingSeconds = 0.014,
            .taskRecordSeconds = 0.015,
            .recordingSeconds = 0.016,
            .recordingElapsedSeconds = 0.017,
            .readyFrontierElapsedSeconds = 0.018,
            .readyFrontierWorkerBusySeconds = 0.019,
            .readyFrontierWorkerCapacitySeconds = 0.020,
        },
        .submission = {
            .acceptedPacketCount = 37u,
            .acceptedTaskCount = 38u,
            .rejectedPacketCount = 39u,
            .rejectedTaskCount = 40u,
            .nativeSubmissionCount = 30u,
            .rejectedSubmissionCount = 38u,
            .nativeCommandListCount = 32u,
            .plannedWaitTokenCount = 44u,
            .sameQueueWaitElisionCount = 12u,
            .timelineWaitCount = 14u,
            .mergedTimelineWaitCount = 18u,
            .acceptedFrontierSubmissionCount = 28u,
            .submissionSeconds = 0.021,
        },
        .present = true,
    };
}

using FrameGraphRuntimeStatisticsMutation = void(*)(Telemetry::FrameGraphRuntimeStatistics&);

static constexpr FrameGraphRuntimeStatisticsMutation s_FrameGraphRuntimeStatisticsCountMutations[] = {
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.compile.packetCount = statistics.compile.taskCount + 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        ++statistics.compile.mergedTaskCount;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.compile.directResourceUseCount = statistics.compile.resourceUseCount + 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        ++statistics.compile.expandedResourceSetMemberUseCount;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.compile.payloadObjectCount = statistics.compile.taskCount + 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.compile.logicalOwnershipTransferSignatureCount =
            statistics.compile.logicalOwnershipTransferCount + 1u
        ;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.compile.repeatedOwnershipTransferSignatureCount =
            statistics.compile.logicalOwnershipTransferSignatureCount + 1u
        ;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.compile.concurrentSharingCouldAvoidTransferCount =
            statistics.compile.logicalOwnershipTransferCount + 1u
        ;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.compile.concurrentSharingAdviceResourceCount = statistics.compile.resourceCount + 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.compile.logicalOwnershipTransferInternalCount =
            statistics.compile.logicalOwnershipTransferCount + 1u
        ;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.compile.logicalOwnershipTransferExternalImportCount =
            statistics.compile.logicalOwnershipTransferCount
            - statistics.compile.logicalOwnershipTransferInternalCount
            + 1u
        ;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        ++statistics.compile.logicalOwnershipTransferExternalExportCount;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.recording.packetCount = statistics.compile.packetCount + 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.recording.taskCount = statistics.compile.taskCount + 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.recording.taskCount = statistics.recording.packetCount - 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.recording.commandListCount = statistics.recording.packetCount - 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.recording.workerRoutedPacketCount = statistics.recording.packetCount + 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.recording.parallelPacketCount = statistics.recording.packetCount + 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.submission.acceptedPacketCount = statistics.compile.packetCount + 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.submission.rejectedPacketCount = statistics.compile.packetCount
            - statistics.submission.acceptedPacketCount + 1u
        ;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.submission.acceptedTaskCount = statistics.compile.taskCount + 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.submission.rejectedTaskCount = statistics.compile.taskCount
            - statistics.submission.acceptedTaskCount + 1u
        ;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.submission.acceptedTaskCount = statistics.submission.acceptedPacketCount - 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.submission.rejectedTaskCount = statistics.submission.rejectedPacketCount - 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.submission.acceptedPacketCount = statistics.submission.nativeSubmissionCount - 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.submission.rejectedSubmissionCount = statistics.submission.rejectedPacketCount + 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.submission.acceptedFrontierSubmissionCount = statistics.submission.nativeSubmissionCount + 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.submission.nativeSubmissionCount = statistics.recording.packetCount + 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.submission.nativeCommandListCount = statistics.submission.nativeSubmissionCount - 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.submission.nativeCommandListCount = statistics.recording.commandListCount + 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.submission.sameQueueWaitElisionCount = statistics.submission.plannedWaitTokenCount + 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.submission.mergedTimelineWaitCount = statistics.submission.plannedWaitTokenCount
            - statistics.submission.sameQueueWaitElisionCount + 1u
        ;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        ++statistics.submission.timelineWaitCount;
    },
};

static void BuildTestAssignedFrameGraph(
    Telemetry::TelemetryArena& arena,
    Telemetry::FrameGraphNodeDescs& nodes,
    Telemetry::FrameGraphEdgeDescs& edges
){
    BuildTestFrameGraph(arena, nodes, edges);
    nodes[0u].queueAssignment = MakeChangedFrameGraphQueueAssignment();
    nodes[2u].queueAssignment = MakeNotAcceptedFrameGraphQueueAssignment();
}

static void BuildTestCompiledFrameGraph(
    Telemetry::TelemetryArena& arena,
    Telemetry::FrameGraphNodeDescs& nodes,
    Telemetry::FrameGraphEdgeDescs& edges
){
    BuildTestAssignedFrameGraph(arena, nodes, edges);
    nodes[0u].compiledTask = MakeFrameGraphCompiledTask(
        41u,
        7u,
        Telemetry::FrameGraphTaskPacketizationDecision::FirstTask
    );
    nodes[2u].compiledTask = MakeFrameGraphCompiledTask(
        41u,
        7u,
        Telemetry::FrameGraphTaskPacketizationDecision::MergedExplicit
    );
}

static void BuildTestRuntimeFrameGraph(
    Telemetry::TelemetryArena& arena,
    Telemetry::FrameGraphNodeDescs& nodes,
    Telemetry::FrameGraphEdgeDescs& edges
){
    BuildTestCompiledFrameGraph(arena, nodes, edges);
    nodes[0u].runtimeStatistics = MakeFrameGraphRuntimeStatistics();
    nodes[2u].runtimeStatistics = MakeFrameGraphRuntimeStatistics();
    nodes[2u].runtimeStatistics.graphGeneration = 61u;
    nodes[2u].runtimeStatistics.planGeneration = 62u;
    nodes[2u].runtimeStatistics.recordingAttemptGeneration = 63u;
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

class QueueAssignmentFrameGraphContributor final : public Telemetry::IFrameGraphContributor{
public:
    virtual bool appendFrameGraph(Telemetry::FrameGraphBuilder& builder)override{
        return builder.addPass(
            Name("assigned_pass"),
            "Assigned Pass",
            MakeChangedFrameGraphQueueAssignment(),
            5u
        ).valid();
    }
};

class RuntimeStatisticsFrameGraphContributor final : public Telemetry::IFrameGraphContributor{
public:
    virtual bool appendFrameGraph(Telemetry::FrameGraphBuilder& builder)override{
        return builder.addPass(
            Name("runtime_pass"),
            "Runtime Pass",
            Telemetry::FrameGraphPassMetadata{
                .queueAssignment = MakeChangedFrameGraphQueueAssignment(),
                .compiledTask = MakeFrameGraphCompiledTask(
                    52u,
                    9u,
                    Telemetry::FrameGraphTaskPacketizationDecision::FirstTask
                ),
                .runtimeStatistics = MakeFrameGraphRuntimeStatistics(),
            },
            6u
        ).valid();
    }
};

class CaptureFrameIndexFrameGraphContributor final : public Telemetry::IFrameGraphContributor{
public:
    virtual bool appendFrameGraph(Telemetry::FrameGraphBuilder& builder)override{
        m_frameIndex = builder.frameIndex();
        return builder.addPass(Name("capture_frame_index"), "Capture Frame Index").valid();
    }
    [[nodiscard]] u64 frameIndex()const{ return m_frameIndex; }

private:
    u64 m_frameIndex = Limit<u64>::s_Max;
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

TEST(Telemetry, FrameGraphRegistryPreservesQueueAssignments){
    TestArena testArena;
    Telemetry::CaptureSession session(testArena.arena);
    session.setCaptureOptions(Telemetry::CaptureOptions::FrameGraphOnly());

    Telemetry::FrameGraphRegistry registry(testArena.arena);
    QueueAssignmentFrameGraphContributor contributor;
    registry.registerContributor(contributor);

    ASSERT_TRUE(registry.record(session));
    const Telemetry::EventRecord* event = session.view().eventAt(0u);
    ASSERT_NE(event, nullptr);

    Telemetry::FrameGraphPayload parsed(testArena.arena);
    ASSERT_TRUE(Telemetry::ParseFrameGraphPayload(testArena.arena, event->payload.data(), event->payload.size(), parsed));
    ASSERT_EQ(parsed.nodes.size(), 1u);
    EXPECT_EQ(parsed.nodes[0u].flags, 5u);
    EXPECT_EQ(parsed.nodes[0u].queueAssignment.acceptance, Telemetry::FrameGraphQueueAssignmentAcceptance::Changed);
    EXPECT_EQ(parsed.nodes[0u].queueAssignment.acceptedQueue.index, 3u);
    EXPECT_EQ(parsed.nodes[0u].queueAssignment.acceptedQueue.deviceGeneration, 17u);
}

TEST(Telemetry, FrameGraphRegistryPreservesRuntimeStatistics){
    TestArena testArena;
    Telemetry::CaptureSession session(testArena.arena);
    session.setCaptureOptions(Telemetry::CaptureOptions::FrameGraphOnly());

    Telemetry::FrameGraphRegistry registry(testArena.arena);
    RuntimeStatisticsFrameGraphContributor contributor;
    registry.registerContributor(contributor);

    ASSERT_TRUE(registry.record(session));
    const Telemetry::EventRecord* event = session.view().eventAt(0u);
    ASSERT_NE(event, nullptr);

    Telemetry::FrameGraphPayload parsed(testArena.arena);
    ASSERT_TRUE(Telemetry::ParseFrameGraphPayload(testArena.arena, event->payload.data(), event->payload.size(), parsed));
    ASSERT_EQ(parsed.nodes.size(), 1u);
    EXPECT_EQ(parsed.nodes[0u].flags, 6u);
    EXPECT_TRUE(parsed.nodes[0u].runtimeStatistics.present);
    EXPECT_EQ(parsed.nodes[0u].runtimeStatistics.graphGeneration, 51u);
    EXPECT_EQ(parsed.nodes[0u].runtimeStatistics.compile.uploadBlobBytes, 30u);
    EXPECT_EQ(parsed.nodes[0u].runtimeStatistics.recording.parallelPacketCount, 29u);
    EXPECT_EQ(parsed.nodes[0u].runtimeStatistics.submission.acceptedFrontierSubmissionCount, 28u);
}

TEST(Telemetry, FrameGraphRegistryPropagatesCaptureFrameIndexToBuilder){
    TestArena testArena;
    Telemetry::CaptureSession session(testArena.arena);
    session.setCaptureOptions(Telemetry::CaptureOptions::FrameGraphOnly());
    session.setFrameIndex(914u);

    Telemetry::FrameGraphRegistry registry(testArena.arena);
    CaptureFrameIndexFrameGraphContributor contributor;
    registry.registerContributor(contributor);

    ASSERT_TRUE(registry.record(session));
    EXPECT_EQ(contributor.frameIndex(), 914u);

    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    Telemetry::FrameGraphPendingNameEdges pendingNameEdges(testArena.arena);
    Telemetry::FrameGraphBuilder directBuilder(nodes, edges, pendingNameEdges);
    EXPECT_EQ(directBuilder.frameIndex(), 0u);
}

TEST(Telemetry, FrameGraphLegacyV1PayloadRoundTrip){
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
    EXPECT_FALSE(parsed.nodes[0u].queueAssignment.present);
    EXPECT_FALSE(parsed.nodes[1u].queueAssignment.present);
    EXPECT_FALSE(parsed.nodes[2u].queueAssignment.present);

    Telemetry::EncodedFrameGraphPayloadHeader legacyHeader;
    NWB_MEMCPY(&legacyHeader, sizeof(legacyHeader), payload.data(), sizeof(legacyHeader));
    EXPECT_EQ(legacyHeader.version, Telemetry::s_FrameGraphLegacyPayloadVersion);

    payload[0u] = 0u;
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));
}

TEST(Telemetry, FrameGraphQueueAssignmentPayloadRoundTrip){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestAssignedFrameGraph(testArena.arena, nodes, edges);

    Telemetry::TelemetryBytes payload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 906u, nodes, edges, payload));
    EXPECT_EQ(payload.size(), sizeof(Telemetry::EncodedFrameGraphPayloadHeaderV2)
            + (sizeof(Telemetry::EncodedFrameGraphNode) * nodes.size())
            + (sizeof(Telemetry::EncodedFrameGraphEdge) * edges.size())
            + (sizeof(Telemetry::EncodedFrameGraphQueueAssignment) * 2u)
            + sizeof("GBuffer Pass")
            + sizeof("Albedo Texture")
            + sizeof("Lighting Pass"));

    Telemetry::EncodedFrameGraphPayloadHeaderV2 header;
    NWB_MEMCPY(&header, sizeof(header), payload.data(), sizeof(header));
    EXPECT_EQ(header.version, Telemetry::s_FrameGraphQueueAssignmentPayloadVersion);
    EXPECT_EQ(header.queueAssignmentCount, 2u);

    Telemetry::FrameGraphPayload parsed(testArena.arena);
    ASSERT_TRUE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));
    ASSERT_EQ(parsed.nodes.size(), 3u);
    const Telemetry::FrameGraphQueueAssignment& changed = parsed.nodes[0u].queueAssignment;
    EXPECT_TRUE(changed.present);
    EXPECT_EQ(changed.initialQueue.index, 1u);
    EXPECT_EQ(changed.initialQueue.deviceGeneration, 17u);
    EXPECT_EQ(changed.plannedQueue.index, 3u);
    EXPECT_EQ(changed.acceptedQueue, changed.plannedQueue);
    EXPECT_EQ(changed.previousAcceptedQueue.index, 2u);
    EXPECT_EQ(changed.previousAcceptedQueue.deviceGeneration, 17u);
    EXPECT_EQ(changed.queueClass, Telemetry::FrameGraphQueueClass::Compute);
    EXPECT_EQ(changed.reason, Telemetry::FrameGraphQueueAssignmentReason::Fallback);
    EXPECT_EQ(changed.modifiers, Telemetry::FrameGraphQueueAssignmentModifier::All);
    EXPECT_TRUE(changed.dedicated);
    EXPECT_EQ(changed.score.preference, 11);
    EXPECT_EQ(changed.score.overlap, 7);
    EXPECT_EQ(changed.score.queueLoad, 3);
    EXPECT_EQ(changed.score.incomingCrossings, 2);
    EXPECT_EQ(changed.score.outgoingCrossings, 1);
    EXPECT_EQ(changed.score.ownershipTransfers, 4);
    EXPECT_EQ(changed.score.total, 8);
    EXPECT_EQ(changed.acceptance, Telemetry::FrameGraphQueueAssignmentAcceptance::Changed);

    EXPECT_FALSE(parsed.nodes[1u].queueAssignment.present);
    const Telemetry::FrameGraphQueueAssignment& notAccepted = parsed.nodes[2u].queueAssignment;
    EXPECT_TRUE(notAccepted.present);
    EXPECT_EQ(notAccepted.initialQueue.index, 4u);
    EXPECT_EQ(notAccepted.plannedQueue.index, 5u);
    EXPECT_FALSE(notAccepted.acceptedQueue.valid());
    EXPECT_EQ(notAccepted.previousAcceptedQueue.index, 2u);
    EXPECT_EQ(notAccepted.queueClass, Telemetry::FrameGraphQueueClass::Transfer);
    EXPECT_EQ(notAccepted.reason, Telemetry::FrameGraphQueueAssignmentReason::ScoredAny);
    EXPECT_EQ(notAccepted.modifiers, Telemetry::FrameGraphQueueAssignmentModifier::TimingFeedback);
    EXPECT_FALSE(notAccepted.dedicated);
    EXPECT_EQ(notAccepted.score.total, 1);
    EXPECT_EQ(notAccepted.acceptance, Telemetry::FrameGraphQueueAssignmentAcceptance::NotAccepted);
}

TEST(Telemetry, FrameGraphCompiledTaskPayloadRoundTrip){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestCompiledFrameGraph(testArena.arena, nodes, edges);

    Telemetry::TelemetryBytes payload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 907u, nodes, edges, payload));
    EXPECT_EQ(payload.size(), sizeof(Telemetry::EncodedFrameGraphPayloadHeaderV3)
            + (sizeof(Telemetry::EncodedFrameGraphNode) * nodes.size())
            + (sizeof(Telemetry::EncodedFrameGraphEdge) * edges.size())
            + (sizeof(Telemetry::EncodedFrameGraphQueueAssignment) * 2u)
            + (sizeof(Telemetry::EncodedFrameGraphCompiledTask) * 2u)
            + sizeof("GBuffer Pass")
            + sizeof("Albedo Texture")
            + sizeof("Lighting Pass"));

    Telemetry::EncodedFrameGraphPayloadHeaderV3 header;
    NWB_MEMCPY(&header, sizeof(header), payload.data(), sizeof(header));
    EXPECT_EQ(header.version, Telemetry::s_FrameGraphCompiledTaskPayloadVersion);
    EXPECT_EQ(header.queueAssignmentCount, 2u);
    EXPECT_EQ(header.compiledTaskCount, 2u);

    Telemetry::FrameGraphPayload parsed(testArena.arena);
    ASSERT_TRUE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));
    ASSERT_EQ(parsed.nodes.size(), 3u);
    EXPECT_TRUE(parsed.nodes[0u].compiledTask.present);
    EXPECT_EQ(parsed.nodes[0u].compiledTask.planGeneration, 41u);
    EXPECT_EQ(parsed.nodes[0u].compiledTask.packetIndex, 7u);
    EXPECT_EQ(
        parsed.nodes[0u].compiledTask.packetizationDecision,
        Telemetry::FrameGraphTaskPacketizationDecision::FirstTask
    );
    EXPECT_FALSE(parsed.nodes[1u].compiledTask.present);
    EXPECT_TRUE(parsed.nodes[2u].compiledTask.present);
    EXPECT_EQ(parsed.nodes[2u].compiledTask.planGeneration, 41u);
    EXPECT_EQ(parsed.nodes[2u].compiledTask.packetIndex, 7u);
    EXPECT_EQ(
        parsed.nodes[2u].compiledTask.packetizationDecision,
        Telemetry::FrameGraphTaskPacketizationDecision::MergedExplicit
    );
}

TEST(Telemetry, FrameGraphRuntimeStatisticsPayloadRoundTrip){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestRuntimeFrameGraph(testArena.arena, nodes, edges);

    Telemetry::TelemetryBytes payload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 910u, nodes, edges, payload));
    EXPECT_EQ(payload.size(), sizeof(Telemetry::EncodedFrameGraphPayloadHeaderV4)
            + (sizeof(Telemetry::EncodedFrameGraphNode) * nodes.size())
            + (sizeof(Telemetry::EncodedFrameGraphEdge) * edges.size())
            + (sizeof(Telemetry::EncodedFrameGraphQueueAssignment) * 2u)
            + (sizeof(Telemetry::EncodedFrameGraphCompiledTask) * 2u)
            + (sizeof(Telemetry::EncodedFrameGraphRuntimeStatistics) * 2u)
            + sizeof("GBuffer Pass")
            + sizeof("Albedo Texture")
            + sizeof("Lighting Pass"));

    Telemetry::EncodedFrameGraphPayloadHeaderV4 header;
    NWB_MEMCPY(&header, sizeof(header), payload.data(), sizeof(header));
    EXPECT_EQ(header.version, Telemetry::s_FrameGraphPayloadVersion);
    EXPECT_EQ(header.queueAssignmentCount, 2u);
    EXPECT_EQ(header.compiledTaskCount, 2u);
    EXPECT_EQ(header.runtimeStatisticsCount, 2u);

    Telemetry::FrameGraphPayload parsed(testArena.arena);
    ASSERT_TRUE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));
    ASSERT_EQ(parsed.nodes.size(), 3u);
    const Telemetry::FrameGraphRuntimeStatistics expected = MakeFrameGraphRuntimeStatistics();
    const Telemetry::FrameGraphRuntimeStatistics& first = parsed.nodes[0u].runtimeStatistics;
    EXPECT_TRUE(first.present);
    EXPECT_EQ(first.graphGeneration, 51u);
    EXPECT_EQ(first.planGeneration, 52u);
    EXPECT_EQ(first.recordingAttemptGeneration, 53u);
    EXPECT_EQ(first.deviceGeneration, 17u);
    EXPECT_EQ(NWB_MEMCMP(&first.compile, &expected.compile, sizeof(expected.compile)), 0);
    EXPECT_EQ(NWB_MEMCMP(&first.recording, &expected.recording, sizeof(expected.recording)), 0);
    EXPECT_EQ(NWB_MEMCMP(&first.submission, &expected.submission, sizeof(expected.submission)), 0);
    EXPECT_FALSE(parsed.nodes[1u].runtimeStatistics.present);
    EXPECT_TRUE(parsed.nodes[2u].runtimeStatistics.present);
    EXPECT_EQ(parsed.nodes[2u].runtimeStatistics.graphGeneration, 61u);
    EXPECT_EQ(parsed.nodes[2u].runtimeStatistics.planGeneration, 62u);
    EXPECT_EQ(parsed.nodes[2u].runtimeStatistics.recordingAttemptGeneration, 63u);
}

TEST(Telemetry, FrameGraphRuntimeStatisticsV4WireFieldOrderIsStable){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestRuntimeFrameGraph(testArena.arena, nodes, edges);

    Telemetry::TelemetryBytes payload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 912u, nodes, edges, payload));
    const usize runtimeStatisticsOffset = sizeof(Telemetry::EncodedFrameGraphPayloadHeaderV4)
        + sizeof(Telemetry::EncodedFrameGraphNode) * nodes.size()
        + sizeof(Telemetry::EncodedFrameGraphEdge) * edges.size()
        + sizeof(Telemetry::EncodedFrameGraphQueueAssignment) * 2u
        + sizeof(Telemetry::EncodedFrameGraphCompiledTask) * 2u
    ;
    ASSERT_GE(payload.size(), runtimeStatisticsOffset + sizeof(Telemetry::EncodedFrameGraphRuntimeStatistics));

    const auto readU16 = [&payload, runtimeStatisticsOffset](const usize wireOffset){
        u16 value = 0u;
        NWB_MEMCPY(&value, sizeof(value), payload.data() + runtimeStatisticsOffset + wireOffset, sizeof(value));
        return value;
    };
    const auto readU32 = [&payload, runtimeStatisticsOffset](const usize wireOffset){
        u32 value = 0u;
        NWB_MEMCPY(&value, sizeof(value), payload.data() + runtimeStatisticsOffset + wireOffset, sizeof(value));
        return value;
    };
    const auto readU64 = [&payload, runtimeStatisticsOffset](const usize wireOffset){
        u64 value = 0u;
        NWB_MEMCPY(&value, sizeof(value), payload.data() + runtimeStatisticsOffset + wireOffset, sizeof(value));
        return value;
    };
    const auto readF64 = [&payload, runtimeStatisticsOffset](const usize wireOffset){
        f64 value = 0.0;
        NWB_MEMCPY(&value, sizeof(value), payload.data() + runtimeStatisticsOffset + wireOffset, sizeof(value));
        return value;
    };

    EXPECT_EQ(readU32(0u), 0u);
    EXPECT_EQ(readU16(4u), 17u);
    EXPECT_EQ(readU16(6u), 0u);
    EXPECT_EQ(readU64(8u), 51u);
    EXPECT_EQ(readU64(16u), 52u);
    EXPECT_EQ(readU64(24u), 53u);

    const u64 expectedCompileCounts[] = {
        78u, 2u, 50u, 4u, 5u, 76u, 7u, 2u, 9u, 10u,
        11u, 12u, 13u, 60u, 15u, 14u, 17u, 2u, 19u, 20u,
        21u, 22u, 23u, 24u, 25u, 26u, 27u, 28u, 29u, 30u,
    };
    for(
        usize fieldIndex = 0u;
        fieldIndex < LengthOf(expectedCompileCounts);
        ++fieldIndex
    )
        EXPECT_EQ(readU64(32u + fieldIndex * sizeof(u64)), expectedCompileCounts[fieldIndex]);
    const f64 expectedCompileSeconds[] = {
        0.001, 0.002, 0.003, 0.004, 0.005, 0.006,
        0.007, 0.008, 0.009, 0.010, 0.011, 0.012,
    };
    for(
        usize fieldIndex = 0u;
        fieldIndex < LengthOf(expectedCompileSeconds);
        ++fieldIndex
    )
        EXPECT_DOUBLE_EQ(readF64(272u + fieldIndex * sizeof(f64)), expectedCompileSeconds[fieldIndex]);

    const u64 expectedRecordingCounts[] = { 31u, 32u, 33u, 34u, 30u, 29u };
    for(
        usize fieldIndex = 0u;
        fieldIndex < LengthOf(expectedRecordingCounts);
        ++fieldIndex
    )
        EXPECT_EQ(readU64(368u + fieldIndex * sizeof(u64)), expectedRecordingCounts[fieldIndex]);
    const f64 expectedRecordingSeconds[] = {
        0.013, 0.014, 0.015, 0.016, 0.017, 0.018, 0.019, 0.020,
    };
    for(
        usize fieldIndex = 0u;
        fieldIndex < LengthOf(expectedRecordingSeconds);
        ++fieldIndex
    )
        EXPECT_DOUBLE_EQ(readF64(416u + fieldIndex * sizeof(f64)), expectedRecordingSeconds[fieldIndex]);

    const u64 expectedSubmissionCounts[] = {
        37u, 38u, 39u, 40u, 30u, 38u, 32u, 44u, 12u, 14u, 18u, 28u,
    };
    for(
        usize fieldIndex = 0u;
        fieldIndex < LengthOf(expectedSubmissionCounts);
        ++fieldIndex
    )
        EXPECT_EQ(readU64(480u + fieldIndex * sizeof(u64)), expectedSubmissionCounts[fieldIndex]);
    EXPECT_DOUBLE_EQ(readF64(576u), 0.021);
}

TEST(Telemetry, FrameGraphPayloadRejectsUnknownVersion){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestFrameGraph(testArena.arena, nodes, edges);

    Telemetry::TelemetryBytes payload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 907u, nodes, edges, payload));
    Telemetry::EncodedFrameGraphPayloadHeader header;
    NWB_MEMCPY(&header, sizeof(header), payload.data(), sizeof(header));
    header.version = 99u;
    NWB_MEMCPY(payload.data(), payload.size(), &header, sizeof(header));

    Telemetry::FrameGraphPayload parsed(testArena.arena);
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));
}

TEST(Telemetry, FrameGraphQueueAssignmentPayloadRejectsMalformedRecords){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestAssignedFrameGraph(testArena.arena, nodes, edges);

    Telemetry::TelemetryBytes payload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 908u, nodes, edges, payload));
    const usize assignmentOffset = sizeof(Telemetry::EncodedFrameGraphPayloadHeaderV2)
        + sizeof(Telemetry::EncodedFrameGraphNode) * nodes.size()
        + sizeof(Telemetry::EncodedFrameGraphEdge) * edges.size()
    ;
    Telemetry::EncodedFrameGraphQueueAssignment first;
    Telemetry::EncodedFrameGraphQueueAssignment second;
    NWB_MEMCPY(&first, sizeof(first), payload.data() + assignmentOffset, sizeof(first));
    NWB_MEMCPY(
        &second,
        sizeof(second),
        payload.data() + assignmentOffset + sizeof(first),
        sizeof(second)
    );

    Telemetry::FrameGraphPayload parsed(testArena.arena);
    second.nodeIndex = first.nodeIndex;
    NWB_MEMCPY(
        payload.data() + assignmentOffset + sizeof(first),
        payload.size() - assignmentOffset - sizeof(first),
        &second,
        sizeof(second)
    );
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 908u, nodes, edges, payload));
    NWB_MEMCPY(&first, sizeof(first), payload.data() + assignmentOffset, sizeof(first));
    NWB_MEMCPY(
        &second,
        sizeof(second),
        payload.data() + assignmentOffset + sizeof(first),
        sizeof(second)
    );
    first.nodeIndex = 2u;
    second.nodeIndex = 0u;
    NWB_MEMCPY(payload.data() + assignmentOffset, payload.size() - assignmentOffset, &first, sizeof(first));
    NWB_MEMCPY(
        payload.data() + assignmentOffset + sizeof(first),
        payload.size() - assignmentOffset - sizeof(first),
        &second,
        sizeof(second)
    );
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 908u, nodes, edges, payload));
    NWB_MEMCPY(&first, sizeof(first), payload.data() + assignmentOffset, sizeof(first));
    first.nodeIndex = 1u;
    NWB_MEMCPY(payload.data() + assignmentOffset, payload.size() - assignmentOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 908u, nodes, edges, payload));
    NWB_MEMCPY(&first, sizeof(first), payload.data() + assignmentOffset, sizeof(first));
    first.modifiers = static_cast<u8>(1u << 7u);
    NWB_MEMCPY(payload.data() + assignmentOffset, payload.size() - assignmentOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 908u, nodes, edges, payload));
    NWB_MEMCPY(&first, sizeof(first), payload.data() + assignmentOffset, sizeof(first));
    ++first.scoreTotal;
    NWB_MEMCPY(payload.data() + assignmentOffset, payload.size() - assignmentOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 908u, nodes, edges, payload));
    NWB_MEMCPY(&first, sizeof(first), payload.data() + assignmentOffset, sizeof(first));
    first.acceptedQueue.deviceGeneration = 0u;
    NWB_MEMCPY(payload.data() + assignmentOffset, payload.size() - assignmentOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 908u, nodes, edges, payload));
    NWB_MEMCPY(&first, sizeof(first), payload.data() + assignmentOffset, sizeof(first));
    first.previousAcceptedQueue.index = Limit<u16>::s_Max;
    NWB_MEMCPY(payload.data() + assignmentOffset, payload.size() - assignmentOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));
}

TEST(Telemetry, FrameGraphCompiledTaskPayloadRejectsMalformedRecords){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestCompiledFrameGraph(testArena.arena, nodes, edges);

    Telemetry::TelemetryBytes payload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 909u, nodes, edges, payload));
    const usize compiledTaskOffset = sizeof(Telemetry::EncodedFrameGraphPayloadHeaderV3)
        + sizeof(Telemetry::EncodedFrameGraphNode) * nodes.size()
        + sizeof(Telemetry::EncodedFrameGraphEdge) * edges.size()
        + sizeof(Telemetry::EncodedFrameGraphQueueAssignment) * 2u
    ;
    Telemetry::EncodedFrameGraphCompiledTask first;
    Telemetry::EncodedFrameGraphCompiledTask second;
    NWB_MEMCPY(&first, sizeof(first), payload.data() + compiledTaskOffset, sizeof(first));
    NWB_MEMCPY(
        &second,
        sizeof(second),
        payload.data() + compiledTaskOffset + sizeof(first),
        sizeof(second)
    );

    Telemetry::FrameGraphPayload parsed(testArena.arena);
    second.nodeIndex = first.nodeIndex;
    NWB_MEMCPY(
        payload.data() + compiledTaskOffset + sizeof(first),
        payload.size() - compiledTaskOffset - sizeof(first),
        &second,
        sizeof(second)
    );
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 909u, nodes, edges, payload));
    NWB_MEMCPY(&first, sizeof(first), payload.data() + compiledTaskOffset, sizeof(first));
    first.nodeIndex = 1u;
    NWB_MEMCPY(payload.data() + compiledTaskOffset, payload.size() - compiledTaskOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 909u, nodes, edges, payload));
    NWB_MEMCPY(&first, sizeof(first), payload.data() + compiledTaskOffset, sizeof(first));
    first.planGeneration = 0u;
    NWB_MEMCPY(payload.data() + compiledTaskOffset, payload.size() - compiledTaskOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 909u, nodes, edges, payload));
    NWB_MEMCPY(&first, sizeof(first), payload.data() + compiledTaskOffset, sizeof(first));
    first.packetIndex = Limit<u32>::s_Max;
    NWB_MEMCPY(payload.data() + compiledTaskOffset, payload.size() - compiledTaskOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 909u, nodes, edges, payload));
    NWB_MEMCPY(&first, sizeof(first), payload.data() + compiledTaskOffset, sizeof(first));
    first.packetizationDecision = Telemetry::FrameGraphTaskPacketizationDecision::Unknown;
    NWB_MEMCPY(payload.data() + compiledTaskOffset, payload.size() - compiledTaskOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 909u, nodes, edges, payload));
    NWB_MEMCPY(&first, sizeof(first), payload.data() + compiledTaskOffset, sizeof(first));
    first.packetizationDecision = Telemetry::FrameGraphTaskPacketizationDecision::kCount;
    NWB_MEMCPY(payload.data() + compiledTaskOffset, payload.size() - compiledTaskOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 909u, nodes, edges, payload));
    NWB_MEMCPY(&first, sizeof(first), payload.data() + compiledTaskOffset, sizeof(first));
    first.reserved[0u] = 1u;
    NWB_MEMCPY(payload.data() + compiledTaskOffset, payload.size() - compiledTaskOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));
}

TEST(Telemetry, FrameGraphRuntimeStatisticsPayloadRejectsMalformedRecords){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestRuntimeFrameGraph(testArena.arena, nodes, edges);

    Telemetry::TelemetryBytes payload(testArena.arena);
    const usize runtimeStatisticsOffset = sizeof(Telemetry::EncodedFrameGraphPayloadHeaderV4)
        + sizeof(Telemetry::EncodedFrameGraphNode) * nodes.size()
        + sizeof(Telemetry::EncodedFrameGraphEdge) * edges.size()
        + sizeof(Telemetry::EncodedFrameGraphQueueAssignment) * 2u
        + sizeof(Telemetry::EncodedFrameGraphCompiledTask) * 2u
    ;
    Telemetry::EncodedFrameGraphRuntimeStatistics first;
    Telemetry::EncodedFrameGraphRuntimeStatistics second;
    const auto loadRuntimeStatistics = [&]()->bool{
        if(!Telemetry::BuildFrameGraphPayload(testArena.arena, 911u, nodes, edges, payload))
            return false;
        NWB_MEMCPY(
            &first,
            sizeof(first),
            payload.data() + runtimeStatisticsOffset,
            sizeof(first)
        );
        NWB_MEMCPY(
            &second,
            sizeof(second),
            payload.data() + runtimeStatisticsOffset + sizeof(first),
            sizeof(second)
        );
        return true;
    };
    Telemetry::FrameGraphPayload parsed(testArena.arena);

    ASSERT_TRUE(loadRuntimeStatistics());
    second.nodeIndex = first.nodeIndex;
    NWB_MEMCPY(
        payload.data() + runtimeStatisticsOffset + sizeof(first),
        payload.size() - runtimeStatisticsOffset - sizeof(first),
        &second,
        sizeof(second)
    );
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(loadRuntimeStatistics());
    first.nodeIndex = 2u;
    second.nodeIndex = 0u;
    NWB_MEMCPY(payload.data() + runtimeStatisticsOffset, payload.size() - runtimeStatisticsOffset, &first, sizeof(first));
    NWB_MEMCPY(
        payload.data() + runtimeStatisticsOffset + sizeof(first),
        payload.size() - runtimeStatisticsOffset - sizeof(first),
        &second,
        sizeof(second)
    );
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(loadRuntimeStatistics());
    first.nodeIndex = 1u;
    NWB_MEMCPY(payload.data() + runtimeStatisticsOffset, payload.size() - runtimeStatisticsOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(loadRuntimeStatistics());
    first.nodeIndex = 3u;
    NWB_MEMCPY(payload.data() + runtimeStatisticsOffset, payload.size() - runtimeStatisticsOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(loadRuntimeStatistics());
    first.reserved = 1u;
    NWB_MEMCPY(payload.data() + runtimeStatisticsOffset, payload.size() - runtimeStatisticsOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(loadRuntimeStatistics());
    first.graphGeneration = 0u;
    NWB_MEMCPY(payload.data() + runtimeStatisticsOffset, payload.size() - runtimeStatisticsOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(loadRuntimeStatistics());
    first.planGeneration = 0u;
    NWB_MEMCPY(payload.data() + runtimeStatisticsOffset, payload.size() - runtimeStatisticsOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(loadRuntimeStatistics());
    first.recordingAttemptGeneration = 0u;
    NWB_MEMCPY(payload.data() + runtimeStatisticsOffset, payload.size() - runtimeStatisticsOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(loadRuntimeStatistics());
    first.deviceGeneration = 0u;
    NWB_MEMCPY(payload.data() + runtimeStatisticsOffset, payload.size() - runtimeStatisticsOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(loadRuntimeStatistics());
    first.compile.declarationSeconds = -1.0;
    NWB_MEMCPY(payload.data() + runtimeStatisticsOffset, payload.size() - runtimeStatisticsOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(loadRuntimeStatistics());
    first.recording.recordingSeconds = Limit<f64>::s_Infinity;
    NWB_MEMCPY(payload.data() + runtimeStatisticsOffset, payload.size() - runtimeStatisticsOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(loadRuntimeStatistics());
    first.submission.submissionSeconds = Limit<f64>::s_QuietNaN;
    NWB_MEMCPY(payload.data() + runtimeStatisticsOffset, payload.size() - runtimeStatisticsOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    for(usize mutationIndex = 0u; mutationIndex < LengthOf(s_FrameGraphRuntimeStatisticsCountMutations); ++mutationIndex){
        SCOPED_TRACE(mutationIndex);
        ASSERT_TRUE(loadRuntimeStatistics());
        Telemetry::FrameGraphRuntimeStatistics malformed = MakeFrameGraphRuntimeStatistics();
        s_FrameGraphRuntimeStatisticsCountMutations[mutationIndex](malformed);
        first = EncodeTestFrameGraphRuntimeStatistics(malformed, first.nodeIndex, first.reserved);
        NWB_MEMCPY(payload.data() + runtimeStatisticsOffset, payload.size() - runtimeStatisticsOffset, &first, sizeof(first));
        EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));
    }

    ASSERT_TRUE(loadRuntimeStatistics());
    Telemetry::EncodedFrameGraphPayloadHeaderV4 header;
    NWB_MEMCPY(&header, sizeof(header), payload.data(), sizeof(header));
    header.runtimeStatisticsCount = 4u;
    NWB_MEMCPY(payload.data(), payload.size(), &header, sizeof(header));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(loadRuntimeStatistics());
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size() - 1u, parsed));
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

    BuildTestAssignedFrameGraph(testArena.arena, nodes, edges);
    nodes[0u].queueAssignment.acceptance = Telemetry::FrameGraphQueueAssignmentAcceptance::First;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(testArena.arena, 1u, nodes, edges, payload));

    BuildTestAssignedFrameGraph(testArena.arena, nodes, edges);
    nodes[1u].queueAssignment = MakeChangedFrameGraphQueueAssignment();
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(testArena.arena, 1u, nodes, edges, payload));

    BuildTestCompiledFrameGraph(testArena.arena, nodes, edges);
    nodes[1u].compiledTask = MakeFrameGraphCompiledTask(
        41u,
        7u,
        Telemetry::FrameGraphTaskPacketizationDecision::FirstTask
    );
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(testArena.arena, 1u, nodes, edges, payload));

    BuildTestFrameGraph(testArena.arena, nodes, edges);
    nodes[1u].runtimeStatistics = MakeFrameGraphRuntimeStatistics();
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(testArena.arena, 1u, nodes, edges, payload));

    BuildTestRuntimeFrameGraph(testArena.arena, nodes, edges);
    nodes[0u].runtimeStatistics.graphGeneration = 0u;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(testArena.arena, 1u, nodes, edges, payload));

    BuildTestRuntimeFrameGraph(testArena.arena, nodes, edges);
    nodes[0u].runtimeStatistics.compile.totalSeconds = -1.0;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(testArena.arena, 1u, nodes, edges, payload));

    BuildTestRuntimeFrameGraph(testArena.arena, nodes, edges);
    nodes[0u].runtimeStatistics.recording.recordingElapsedSeconds = Limit<f64>::s_QuietNaN;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(testArena.arena, 1u, nodes, edges, payload));

    for(usize mutationIndex = 0u; mutationIndex < LengthOf(s_FrameGraphRuntimeStatisticsCountMutations); ++mutationIndex){
        SCOPED_TRACE(mutationIndex);
        BuildTestRuntimeFrameGraph(testArena.arena, nodes, edges);
        s_FrameGraphRuntimeStatisticsCountMutations[mutationIndex](nodes[0u].runtimeStatistics);
        EXPECT_FALSE(Telemetry::IsValidFrameGraphRuntimeStatistics(nodes[0u].runtimeStatistics));
        EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(testArena.arena, 1u, nodes, edges, payload));
    }
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

static ::ArenaMemoryStats MakeTestArenaStats(
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
    EXPECT_TRUE(result.ok());
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
    EXPECT_TRUE(result.ok());
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

    const Name cpuScopeName("gbuffer");
    NWB::Core::Perf::TimingStats stats = MakeTestTimingStats();
    stats.sampleCount = 1u;
    stats.firstSampleFrameIndex = stats.publishFrameIndex;
    stats.lastSampleFrameIndex = stats.publishFrameIndex;
    EXPECT_TRUE(Telemetry::RecordPerfTiming(recorder, Telemetry::PerfTimingSource::Cpu, cpuScopeName, "gbuffer", stats, 2u));

    const Name memoryScopeName("memory/project_arena");
    const NWB::Core::Perf::MemorySnapshot snapshot = MakeTestMemorySnapshot(memoryScopeName);
    const NWB::Core::Perf::MemoryDelta delta = MakeTestMemoryDelta();
    EXPECT_TRUE(Telemetry::RecordPerfMemory(recorder, memoryScopeName, "memory/project_arena", snapshot, delta, 3u));

    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestFrameGraph(testArena.arena, nodes, edges);
    EXPECT_TRUE(Telemetry::RecordFrameGraph(recorder, stats.publishFrameIndex, nodes, edges, 4u));

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
    EXPECT_EQ(report.summary.maxFrameIndex, snapshot.frameIndex);
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
    EXPECT_TRUE(ContainsText(AStringView(report.perfCsv.data(), report.perfCsv.size()), "cpu,gbuffer"));
    EXPECT_TRUE(ContainsText(AStringView(report.graph.data(), report.graph.size()), "GBuffer Pass\\n125.000 ms"));
}

TEST(Telemetry, TelemetryReportPreservesEveryFrameGraphAndCorrelatesTimingByFrame){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    const Name gbufferScopeName("gbuffer");
    NWB::Core::Perf::TimingStats firstTiming = MakeTestTimingStats();
    firstTiming.seconds = 0.041;
    firstTiming.sampleCount = 1u;
    firstTiming.publishFrameIndex = 41u;
    firstTiming.firstSampleFrameIndex = 40u;
    firstTiming.lastSampleFrameIndex = 40u;
    ASSERT_TRUE(Telemetry::RecordPerfTiming(
        recorder,
        Telemetry::PerfTimingSource::Gpu,
        gbufferScopeName,
        "gbuffer",
        firstTiming,
        70u
    ));

    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestFrameGraph(testArena.arena, nodes, edges);
    ASSERT_TRUE(Telemetry::RecordFrameGraph(recorder, 40u, nodes, edges, 7u));

    NWB::Core::Perf::TimingStats secondTiming = MakeTestTimingStats();
    secondTiming.seconds = 0.042;
    secondTiming.sampleCount = 1u;
    secondTiming.publishFrameIndex = 42u;
    secondTiming.firstSampleFrameIndex = 41u;
    secondTiming.lastSampleFrameIndex = 41u;
    ASSERT_TRUE(Telemetry::RecordPerfTiming(
        recorder,
        Telemetry::PerfTimingSource::Gpu,
        gbufferScopeName,
        "gbuffer",
        secondTiming,
        80u
    ));

    nodes[0u].label = "Second GBuffer Pass";
    nodes[0u].flags = 129u;
    edges[0u].flags = 64u;
    edges[1u].flags = 3u;
    ASSERT_TRUE(Telemetry::RecordFrameGraph(recorder, 41u, nodes, edges, 8u));

    nodes[0u].label = "Unmatched GBuffer Pass";
    ASSERT_TRUE(Telemetry::RecordFrameGraph(recorder, 42u, nodes, edges, 9u));

    Log::TelemetryReport report(testArena.arena);
    ASSERT_TRUE(Log::BuildTelemetryReport(testArena.arena, recorder.view(), report));
    EXPECT_EQ(report.summary.frameGraphFrameCount, 3u);

    const AStringView json(report.json.data(), report.json.size());
    const usize firstJsonGraph = json.find("\"frameIndex\": 40");
    const usize secondJsonGraph = json.find("\"frameIndex\": 41");
    const usize thirdJsonGraph = json.find("\"frameIndex\": 42");
    ASSERT_NE(firstJsonGraph, AStringView::npos);
    ASSERT_NE(secondJsonGraph, AStringView::npos);
    ASSERT_NE(thirdJsonGraph, AStringView::npos);
    EXPECT_LT(firstJsonGraph, secondJsonGraph);
    EXPECT_LT(secondJsonGraph, thirdJsonGraph);
    const AStringView firstJsonRecord = json.substr(firstJsonGraph, secondJsonGraph - firstJsonGraph);
    const AStringView secondJsonRecord = json.substr(secondJsonGraph, thirdJsonGraph - secondJsonGraph);
    const AStringView thirdJsonRecord = json.substr(thirdJsonGraph);
    EXPECT_TRUE(ContainsText(firstJsonRecord, "\"streamId\": 7"));
    EXPECT_TRUE(ContainsText(secondJsonRecord, "\"streamId\": 8"));
    EXPECT_TRUE(ContainsText(thirdJsonRecord, "\"streamId\": 9"));
    EXPECT_TRUE(ContainsText(firstJsonRecord, "\"label\": \"GBuffer Pass\", \"kind\": \"pass\", \"flags\": 1"));
    EXPECT_TRUE(ContainsText(secondJsonRecord, "\"label\": \"Second GBuffer Pass\", \"kind\": \"pass\", \"flags\": 129"));
    EXPECT_TRUE(ContainsText(thirdJsonRecord, "\"label\": \"Unmatched GBuffer Pass\", \"kind\": \"pass\", \"flags\": 129"));
    EXPECT_TRUE(ContainsText(firstJsonRecord, "\"from\": 1, \"to\": 2, \"kind\": \"reads\", \"flags\": 2"));
    EXPECT_TRUE(ContainsText(secondJsonRecord, "\"from\": 1, \"to\": 2, \"kind\": \"reads\", \"flags\": 3"));
    EXPECT_TRUE(ContainsText(firstJsonRecord, "\"from\": 0, \"to\": 1, \"kind\": \"writes\", \"flags\": 0"));
    EXPECT_TRUE(ContainsText(secondJsonRecord, "\"from\": 0, \"to\": 1, \"kind\": \"writes\", \"flags\": 64"));

    char gbufferIdentityText[NameDetail::s_DebugHashTextLength + 1u] = {};
    NameDetail::HashToDebugString(Name("gbuffer").hash(), gbufferIdentityText, sizeof(gbufferIdentityText));
    constexpr AStringView identityPrefix = "\"identity\": \"";
    const usize identityOffset = json.find(identityPrefix);
    ASSERT_NE(identityOffset, AStringView::npos);
    EXPECT_EQ(
        json.substr(identityOffset + identityPrefix.size(), NameDetail::s_DebugHashTextLength),
        AStringView(gbufferIdentityText, NameDetail::s_DebugHashTextLength)
    );

    const AStringView dot(report.graph.data(), report.graph.size());
    const usize firstDotGraph = dot.find("digraph frame_graph_40_7_0");
    const usize secondDotGraph = dot.find("digraph frame_graph_41_8_1");
    const usize thirdDotGraph = dot.find("digraph frame_graph_42_9_2");
    ASSERT_NE(firstDotGraph, AStringView::npos);
    ASSERT_NE(secondDotGraph, AStringView::npos);
    ASSERT_NE(thirdDotGraph, AStringView::npos);
    EXPECT_LT(firstDotGraph, secondDotGraph);
    EXPECT_LT(secondDotGraph, thirdDotGraph);
    EXPECT_EQ(dot.find("digraph frame_graph_", thirdDotGraph + 1u), AStringView::npos);
    const AStringView firstDotRecord = dot.substr(firstDotGraph, secondDotGraph - firstDotGraph);
    const AStringView secondDotRecord = dot.substr(secondDotGraph, thirdDotGraph - secondDotGraph);
    const AStringView thirdDotRecord = dot.substr(thirdDotGraph);
    EXPECT_TRUE(ContainsText(firstDotRecord, AStringView(gbufferIdentityText, NameDetail::s_DebugHashTextLength)));
    EXPECT_TRUE(ContainsText(secondDotRecord, AStringView(gbufferIdentityText, NameDetail::s_DebugHashTextLength)));
    EXPECT_TRUE(ContainsText(thirdDotRecord, AStringView(gbufferIdentityText, NameDetail::s_DebugHashTextLength)));
    EXPECT_TRUE(ContainsText(firstDotRecord, "GBuffer Pass\\n41.000 ms"));
    EXPECT_TRUE(ContainsText(secondDotRecord, "Second GBuffer Pass\\n42.000 ms"));
    EXPECT_TRUE(ContainsText(thirdDotRecord, "Unmatched GBuffer Pass"));
    EXPECT_FALSE(ContainsText(thirdDotRecord, " ms"));
    EXPECT_TRUE(ContainsText(firstDotRecord, "kind=\"pass\", flags=1"));
    EXPECT_TRUE(ContainsText(secondDotRecord, "kind=\"pass\", flags=129"));
    EXPECT_TRUE(ContainsText(firstDotRecord, "label=\"reads\", flags=2"));
    EXPECT_TRUE(ContainsText(secondDotRecord, "label=\"reads\", flags=3"));
    EXPECT_TRUE(ContainsText(firstDotRecord, "label=\"writes\", flags=0"));
    EXPECT_TRUE(ContainsText(secondDotRecord, "label=\"writes\", flags=64"));
}

TEST(Telemetry, TelemetryReportDoesNotAttachAggregatedTimingToOneGraph){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    NWB::Core::Perf::TimingStats aggregatedTiming = MakeTestTimingStats();
    aggregatedTiming.seconds = 0.043;
    aggregatedTiming.sampleCount = 2u;
    aggregatedTiming.publishFrameIndex = 44u;
    aggregatedTiming.firstSampleFrameIndex = 43u;
    aggregatedTiming.lastSampleFrameIndex = 43u;
    ASSERT_TRUE(Telemetry::RecordPerfTiming(
        recorder,
        Telemetry::PerfTimingSource::Gpu,
        Name("gbuffer"),
        "gbuffer",
        aggregatedTiming,
        70u
    ));

    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestFrameGraph(testArena.arena, nodes, edges);
    ASSERT_TRUE(Telemetry::RecordFrameGraph(recorder, 43u, nodes, edges, 7u));
    nodes[0u].label = "Publish Frame GBuffer Pass";
    ASSERT_TRUE(Telemetry::RecordFrameGraph(recorder, 44u, nodes, edges, 8u));

    Log::TelemetryReport report(testArena.arena);
    ASSERT_TRUE(Log::BuildTelemetryReport(testArena.arena, recorder.view(), report));
    const AStringView dot(report.graph.data(), report.graph.size());
    EXPECT_TRUE(ContainsText(dot, "digraph frame_graph_43_7_0"));
    EXPECT_TRUE(ContainsText(dot, "digraph frame_graph_44_8_1"));
    EXPECT_FALSE(ContainsText(dot, " ms"));
    EXPECT_EQ(report.summary.gpuTimingEventCount, 1u);
    EXPECT_EQ(report.summary.gpuTimingSampleCount, aggregatedTiming.sampleCount);
    EXPECT_EQ(report.summary.gpuTimingSeconds, aggregatedTiming.seconds);
    EXPECT_TRUE(ContainsText(AStringView(report.perfCsv.data(), report.perfCsv.size()), "gpu,gbuffer"));
}

TEST(Telemetry, TelemetryReportPreservesExactQueueAssignments){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestRuntimeFrameGraph(testArena.arena, nodes, edges);
    nodes[2u].queueAssignment.previousAcceptedQueue = {};
    ASSERT_TRUE(Telemetry::RecordFrameGraph(recorder, 52u, nodes, edges, 12u));

    Log::TelemetryReport report(testArena.arena);
    ASSERT_TRUE(Log::BuildTelemetryReport(testArena.arena, recorder.view(), report));

    const AStringView json(report.json.data(), report.json.size());
    const usize changedJsonOffset = json.find("\"label\": \"GBuffer Pass\"");
    const usize unassignedJsonOffset = json.find("\"label\": \"Albedo Texture\"");
    const usize rejectedJsonOffset = json.find("\"label\": \"Lighting Pass\"");
    const usize jsonEdgesOffset = json.find("\"edges\": [", rejectedJsonOffset);
    ASSERT_NE(changedJsonOffset, AStringView::npos);
    ASSERT_NE(unassignedJsonOffset, AStringView::npos);
    ASSERT_NE(rejectedJsonOffset, AStringView::npos);
    ASSERT_NE(jsonEdgesOffset, AStringView::npos);
    ASSERT_LT(changedJsonOffset, unassignedJsonOffset);
    ASSERT_LT(unassignedJsonOffset, rejectedJsonOffset);
    ASSERT_LT(rejectedJsonOffset, jsonEdgesOffset);

    const AStringView changedJson = json.substr(changedJsonOffset, unassignedJsonOffset - changedJsonOffset);
    const AStringView unassignedJson = json.substr(unassignedJsonOffset, rejectedJsonOffset - unassignedJsonOffset);
    const AStringView rejectedJson = json.substr(rejectedJsonOffset, jsonEdgesOffset - rejectedJsonOffset);
    EXPECT_TRUE(ContainsText(
        changedJson,
        "\"queueAssignment\": {\"initialQueue\": {\"index\": 1, \"deviceGeneration\": 17}, "
        "\"plannedQueue\": {\"index\": 3, \"deviceGeneration\": 17}, "
        "\"acceptedQueue\": {\"index\": 3, \"deviceGeneration\": 17}, "
        "\"previousAcceptedQueue\": {\"index\": 2, \"deviceGeneration\": 17}, \"queueClass\": \"compute\", "
        "\"reason\": \"fallback\", \"modifierMask\": 63, \"acceptance\": \"changed\", \"dedicated\": true, "
        "\"score\": {\"preference\": 11, \"overlap\": 7, \"queueLoad\": 3, \"incomingCrossings\": 2, "
        "\"outgoingCrossings\": 1, \"ownershipTransfers\": 4, \"total\": 8}}"
    ));
    EXPECT_TRUE(ContainsText(unassignedJson, "\"queueAssignment\": null"));
    EXPECT_TRUE(ContainsText(
        changedJson,
        "\"compiledTask\": {\"planGeneration\": 41, \"packetIndex\": 7, "
        "\"packetizationDecision\": \"firstTask\"}"
    ));
    EXPECT_TRUE(ContainsText(unassignedJson, "\"compiledTask\": null"));
    EXPECT_TRUE(ContainsText(
        rejectedJson,
        "\"compiledTask\": {\"planGeneration\": 41, \"packetIndex\": 7, "
        "\"packetizationDecision\": \"mergedExplicit\"}"
    ));
    EXPECT_TRUE(ContainsText(
        rejectedJson,
        "\"queueAssignment\": {\"initialQueue\": {\"index\": 4, \"deviceGeneration\": 17}, "
        "\"plannedQueue\": {\"index\": 5, \"deviceGeneration\": 17}, \"acceptedQueue\": null, "
        "\"previousAcceptedQueue\": null, \"queueClass\": \"transfer\", \"reason\": \"scoredAny\", "
        "\"modifierMask\": 32, \"acceptance\": \"notAccepted\", \"dedicated\": false, "
        "\"score\": {\"preference\": 5, \"overlap\": 6, \"queueLoad\": 1, \"incomingCrossings\": 2, "
        "\"outgoingCrossings\": 3, \"ownershipTransfers\": 4, \"total\": 1}}"
    ));
    EXPECT_TRUE(ContainsText(
        changedJson,
        "\"runtimeStatistics\": {\"graphGeneration\": 51, \"planGeneration\": 52, "
        "\"recordingAttemptGeneration\": 53, \"deviceGeneration\": 17, \"compile\": {\"taskCount\": 78, "
        "\"resourceCount\": 2, \"resourceUseCount\": 50, \"explicitDependencyCount\": 4"
    ));
    EXPECT_TRUE(ContainsText(
        changedJson,
        "\"payloadObjectBytes\": 28, \"uploadBlobCount\": 29, \"uploadBlobBytes\": 30, "
        "\"declarationSeconds\": 0.001"
    ));
    EXPECT_TRUE(ContainsText(
        changedJson,
        "\"recording\": {\"packetCount\": 31, \"taskCount\": 32, \"commandListCount\": 33, "
        "\"barrierCount\": 34, \"workerRoutedPacketCount\": 30, \"parallelPacketCount\": 29"
    ));
    EXPECT_TRUE(ContainsText(
        changedJson,
        "\"submission\": {\"acceptedPacketCount\": 37, \"acceptedTaskCount\": 38, "
        "\"rejectedPacketCount\": 39, \"rejectedTaskCount\": 40, \"nativeSubmissionCount\": 30, "
        "\"rejectedSubmissionCount\": 38, \"nativeCommandListCount\": 32, \"plannedWaitTokenCount\": 44, "
        "\"sameQueueWaitElisionCount\": 12, \"timelineWaitCount\": 14, \"mergedTimelineWaitCount\": 18, "
        "\"acceptedFrontierSubmissionCount\": 28, \"submissionSeconds\": 0.021}"
    ));
    EXPECT_TRUE(ContainsText(unassignedJson, "\"runtimeStatistics\": null"));
    EXPECT_TRUE(ContainsText(
        rejectedJson,
        "\"runtimeStatistics\": {\"graphGeneration\": 61, \"planGeneration\": 62, "
        "\"recordingAttemptGeneration\": 63, \"deviceGeneration\": 17"
    ));

    const AStringView dot(report.graph.data(), report.graph.size());
    const usize changedDotOffset = dot.find("  n0 [");
    const usize unassignedDotOffset = dot.find("  n1 [");
    const usize rejectedDotOffset = dot.find("  n2 [");
    const usize dotEdgesOffset = dot.find("  n0 ->", rejectedDotOffset);
    ASSERT_NE(changedDotOffset, AStringView::npos);
    ASSERT_NE(unassignedDotOffset, AStringView::npos);
    ASSERT_NE(rejectedDotOffset, AStringView::npos);
    ASSERT_NE(dotEdgesOffset, AStringView::npos);
    ASSERT_LT(changedDotOffset, unassignedDotOffset);
    ASSERT_LT(unassignedDotOffset, rejectedDotOffset);
    ASSERT_LT(rejectedDotOffset, dotEdgesOffset);

    const AStringView changedDot = dot.substr(changedDotOffset, unassignedDotOffset - changedDotOffset);
    const AStringView unassignedDot = dot.substr(unassignedDotOffset, rejectedDotOffset - unassignedDotOffset);
    const AStringView rejectedDot = dot.substr(rejectedDotOffset, dotEdgesOffset - rejectedDotOffset);
    EXPECT_TRUE(ContainsText(
        changedDot,
        "queue_assignment=\"present\", queue_initial_index=1, queue_initial_device_generation=17, "
        "queue_planned_index=3, queue_planned_device_generation=17, queue_accepted_index=3, "
        "queue_accepted_device_generation=17, queue_previous_accepted_index=2, "
        "queue_previous_accepted_device_generation=17, queue_class=\"compute\", queue_reason=\"fallback\", "
        "queue_modifier_mask=63, queue_acceptance=\"changed\", queue_dedicated=true, queue_score_preference=11, "
        "queue_score_overlap=7, queue_score_queue_load=3, queue_score_incoming_crossings=2, "
        "queue_score_outgoing_crossings=1, queue_score_ownership_transfers=4, queue_score_total=8"
    ));
    EXPECT_TRUE(ContainsText(unassignedDot, "queue_assignment=\"none\""));
    EXPECT_TRUE(ContainsText(
        changedDot,
        "compiled_task=\"present\", compiled_plan_generation=41, compiled_packet_index=7, "
        "packetization_decision=\"firstTask\""
    ));
    EXPECT_TRUE(ContainsText(unassignedDot, "compiled_task=\"none\""));
    EXPECT_TRUE(ContainsText(
        rejectedDot,
        "compiled_task=\"present\", compiled_plan_generation=41, compiled_packet_index=7, "
        "packetization_decision=\"mergedExplicit\""
    ));
    EXPECT_TRUE(ContainsText(
        rejectedDot,
        "queue_assignment=\"present\", queue_initial_index=4, queue_initial_device_generation=17, "
        "queue_planned_index=5, queue_planned_device_generation=17, queue_accepted_index=\"none\", "
        "queue_accepted_device_generation=\"none\", queue_previous_accepted_index=\"none\", "
        "queue_previous_accepted_device_generation=\"none\", queue_class=\"transfer\", queue_reason=\"scoredAny\", "
        "queue_modifier_mask=32, queue_acceptance=\"notAccepted\", queue_dedicated=false, queue_score_preference=5, "
        "queue_score_overlap=6, queue_score_queue_load=1, queue_score_incoming_crossings=2, "
        "queue_score_outgoing_crossings=3, queue_score_ownership_transfers=4, queue_score_total=1"
    ));
    EXPECT_TRUE(ContainsText(
        changedDot,
        "runtime_statistics=\"present\", runtime_graph_generation=51, runtime_plan_generation=52, "
        "runtime_recording_attempt_generation=53, runtime_device_generation=17"
    ));
    EXPECT_TRUE(ContainsText(unassignedDot, "runtime_statistics=\"none\""));
    EXPECT_TRUE(ContainsText(
        rejectedDot,
        "runtime_statistics=\"present\", runtime_graph_generation=61, runtime_plan_generation=62, "
        "runtime_recording_attempt_generation=63, runtime_device_generation=17"
    ));
}

TEST(Telemetry, TelemetryReportMarksLegacyRuntimeStatisticsAbsent){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestCompiledFrameGraph(testArena.arena, nodes, edges);
    ASSERT_TRUE(Telemetry::RecordFrameGraph(recorder, 53u, nodes, edges, 13u));

    Log::TelemetryReport report(testArena.arena);
    ASSERT_TRUE(Log::BuildTelemetryReport(testArena.arena, recorder.view(), report));
    EXPECT_TRUE(ContainsText(AStringView(report.json.data(), report.json.size()), "\"runtimeStatistics\": null"));
    EXPECT_TRUE(ContainsText(AStringView(report.graph.data(), report.graph.size()), "runtime_statistics=\"none\""));
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

