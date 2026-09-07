// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "telemetry_test_helpers.h"
#include <gtest/gtest.h>
#include <global/thread.h>
#include <tests/common/capturing_logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_capture_tests{


using namespace TelemetryTestDetail;

static u32 s_ExistingDiagnosticCallbackCount = 0u;

static void ExistingDiagnosticCallback(const DiagnosticEventRecord&)noexcept{
    ++s_ExistingDiagnosticCallbackCount;
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

TEST(Telemetry, DiagnosticCaptureGuardConcurrentLifetimeStress){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    AtomicFlag captureThreadStarted;
    AtomicFlag stopCaptureThread;
    Thread captureThread([&captureThreadStarted, &stopCaptureThread](){
        captureThreadStarted.test_and_set(MemoryOrder::release);
        captureThreadStarted.notify_all();
        while(!stopCaptureThread.test(MemoryOrder::acquire)){
            CaptureDiagnosticEvent(DiagnosticEventRecord{
                .event = DiagnosticEventName::s_Error.data(),
                .category = "telemetry_guard",
                .message = "capture during destruction",
            });
            YieldThread();
        }
    });

    while(!captureThreadStarted.test(MemoryOrder::acquire))
        captureThreadStarted.wait(false, MemoryOrder::acquire);

    constexpr u32 s_IterationCount = 64u;
    bool allGuardsInstalled = true;
    for(u32 iteration = 0u; iteration < s_IterationCount; ++iteration){
        {
            Telemetry::DiagnosticCaptureGuard guard(recorder);
            if(!guard.installed()){
                allGuardsInstalled = false;
                break;
            }

            const usize eventCountBeforeCapture = recorder.eventCount();
            while(recorder.eventCount() == eventCountBeforeCapture)
                YieldThread();
        }
    }

    stopCaptureThread.test_and_set(MemoryOrder::release);
    captureThread.join();
    EXPECT_TRUE(allGuardsInstalled);
    EXPECT_GE(recorder.eventCount(), s_IterationCount);

    {
        Telemetry::DiagnosticCaptureGuard finalGuard(recorder);
        EXPECT_TRUE(finalGuard.installed());
    }

    const usize eventCountAfterFinalDestruction = recorder.eventCount();
    CaptureDiagnosticEvent(DiagnosticEventRecord{
        .event = DiagnosticEventName::s_Error.data(),
        .category = "telemetry_guard",
        .message = "ignored after destruction",
    });
    EXPECT_EQ(recorder.eventCount(), eventCountAfterFinalDestruction);

    const Telemetry::EventView events = recorder.view();
    for(usize eventIndex = 0u; eventIndex < events.eventCount(); ++eventIndex){
        const Telemetry::EventRecord* const event = events.eventAt(eventIndex);
        ASSERT_NE(event, nullptr);

        Telemetry::DiagnosticPayload parsed(testArena.arena);
        ASSERT_TRUE(Telemetry::ParseDiagnosticPayload(testArena.arena, event->payload.data(), event->payload.size(), parsed));
        EXPECT_EQ(parsed.event, DiagnosticEventName::s_Error);
        EXPECT_EQ(parsed.category, "telemetry_guard");
        EXPECT_EQ(parsed.message, "capture during destruction");
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

