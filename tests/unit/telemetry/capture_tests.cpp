// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "telemetry_test_helpers.h"
#include <gtest/gtest.h>
#include <global/thread.h>
#include <tests/common/capturing_logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_capture_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;


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

            NWB_LOGGER_ESSENTIAL_INFO(GLOBAL_TEXT("scope text"));
            CaptureDiagnosticEvent(DiagnosticEventRecord{
                .event = DiagnosticEventName::s_Error.data(),
                .category = "scope_diagnostic",
                .message = "scope diagnostic",
                .file = "scope.cpp",
                .line = 67u,
            });
        }

        NWB_LOGGER_ESSENTIAL_INFO(GLOBAL_TEXT("after scope"));
    }

    CaptureDiagnosticEvent(DiagnosticEventRecord{
        .event = DiagnosticEventName::s_Error.data(),
        .category = "scope_diagnostic",
        .message = "ignored after scope",
    });

    EXPECT_EQ(previousLogger.messageCount(), s_ExpectedDualCount);
    EXPECT_TRUE(previousLogger.sawMessageContaining(GLOBAL_TEXT("scope text")));
    EXPECT_TRUE(previousLogger.sawMessageContaining(GLOBAL_TEXT("after scope")));
    EXPECT_EQ(session.eventCount(), s_ExpectedDualCount);

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

TEST(Telemetry, TextLogPayloadRejectsCorruptedHeaderAfterValidParse){
    TestArena testArena;
    Telemetry::TelemetryBytes payload(testArena.arena);

    ASSERT_TRUE(Telemetry::BuildTextLogPayload(
        testArena.arena,
        NWB::Core::Common::LogType::Warning,
        GLOBAL_TEXT("telemetry text log"),
        payload
    ));

    Telemetry::TextLogPayload parsed(testArena.arena);
    ASSERT_TRUE(Telemetry::ParseTextLogPayload(testArena.arena, payload.data(), payload.size(), parsed));

    payload[0u] = 0u;
    EXPECT_FALSE(Telemetry::ParseTextLogPayload(testArena.arena, payload.data(), payload.size(), parsed));
}

TEST(Telemetry, DiagnosticPayloadPreservesBoundedTextAndRejectsCorruptedHeader){
    TestArena testArena;
    Telemetry::TelemetryBytes payload(testArena.arena);

    constexpr char s_Message[] = { 'd', 0, 'm' };
    const DiagnosticEventRecord source{
        .event = AStringView("error.trailing").substr(0u, 5u),
        .category = "unit_category",
        .expression = "value != nullptr",
        .message = AStringView(s_Message, sizeof(s_Message)),
        .file = "diagnostic_test.cpp",
        .instructionPointer = 0x1234u,
        .line = 77u,
        .terminatesProcess = true,
    };

    ASSERT_TRUE(Telemetry::BuildDiagnosticPayload(testArena.arena, source, payload));

    Telemetry::DiagnosticPayload parsed(testArena.arena);
    ASSERT_TRUE(Telemetry::ParseDiagnosticPayload(testArena.arena, payload.data(), payload.size(), parsed));
    EXPECT_EQ(AStringView(parsed.event), source.event);
    EXPECT_EQ(AStringView(parsed.message), source.message);

    payload[0u] = 0u;
    EXPECT_FALSE(Telemetry::ParseDiagnosticPayload(testArena.arena, payload.data(), payload.size(), parsed));
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

