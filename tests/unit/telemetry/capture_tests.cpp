// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "telemetry_test_helpers.h"

#include <gtest/gtest.h>

#include <global/scope_exit.h>
#include <global/thread.h>

#include <tests/common/capturing_logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_capture_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;


using namespace TelemetryTestDetail;

static u32 s_ExistingDiagnosticCallbackCount = 0u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void ExistingDiagnosticCallback(const DiagnosticEventRecord&)noexcept{
    ++s_ExistingDiagnosticCallbackCount;
}


TEST(Telemetry, CaptureScopeRestoresLoggerAndStopsDiagnosticsAfterDestruction){
    TestArena testArena;
    Telemetry::CaptureSession session(testArena.arena);
    session.setCaptureOptions(Telemetry::CaptureOptions::All());

    NWB::Tests::CapturingLogger previousLogger;
    {
        NWB::Core::Common::LoggerRegistrationGuard previousRegistration(previousLogger);
        {
            Telemetry::CaptureSessionCaptureScope captureScope(session);

            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("scope text"));
            CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = DiagnosticEventName::s_Error.data(), .category = "scope_diagnostic", .message = "scope diagnostic", .file = "scope.cpp", .line = 67u, });
        }

        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("after scope"));
    }

    CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = DiagnosticEventName::s_Error.data(), .category = "scope_diagnostic", .message = "ignored after scope", });

    EXPECT_EQ(previousLogger.messageCount(), s_ExpectedDualCount);
    EXPECT_TRUE(previousLogger.sawMessageContaining(NWB_TEXT("scope text")));
    EXPECT_TRUE(previousLogger.sawMessageContaining(NWB_TEXT("after scope")));
    EXPECT_EQ(session.eventCount(), s_ExpectedDualCount);
}

TEST(Telemetry, TextCaptureCountsRejectedPayloadsWithoutCountingFilteredEventsAndStillForwards){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    NWB::Tests::CapturingLogger forwardLogger;
    Telemetry::TextLogCaptureLogger capture(recorder, &forwardLogger);
    NWB::Core::Common::LogString message(NWB_TEXT("capture admission"), testArena.arena);
    const auto invalidType = static_cast<NWB::Core::Common::LogType::Enum>(0xffu);
    capture.enqueue(message, invalidType);
    EXPECT_EQ(capture.droppedTextLogCount(), 0u);
    EXPECT_EQ(recorder.eventCount(), 0u);

    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());
    capture.enqueue(message, invalidType);
    EXPECT_EQ(capture.droppedTextLogCount(), 1u);
    EXPECT_EQ(recorder.eventCount(), 0u);
    capture.enqueue(message, NWB::Core::Common::LogType::Info);
    EXPECT_EQ(capture.droppedTextLogCount(), 1u);
    EXPECT_EQ(recorder.eventCount(), 1u);

    recorder.setCaptureOptions(Telemetry::CaptureOptions::Disabled());
    capture.enqueue(message, invalidType);
    EXPECT_EQ(capture.droppedTextLogCount(), 1u);
    EXPECT_EQ(recorder.eventCount(), 0u);
    EXPECT_EQ(forwardLogger.messageCount(), 4u);
    EXPECT_TRUE(forwardLogger.sawMessageContaining(NWB_TEXT("capture admission")));
}

TEST(Telemetry, TextLogPayloadRejectsCorruptedHeaderAfterValidParse){
    TestArena testArena;
    Telemetry::TelemetryBytes payload(testArena.arena);

    ASSERT_TRUE(Telemetry::BuildTextLogPayload(testArena.scratchArena, NWB::Core::Common::LogType::Warning, NWB_TEXT("telemetry text log"), payload));

    Expected<Telemetry::TextLogPayload> parsed = MakeUnexpected(Failure{});
    ASSERT_TRUE((parsed = Telemetry::ParseTextLogPayload(testArena.arena, payload.data(), payload.size())));

    payload[0u] = 0u;
    EXPECT_FALSE((parsed = Telemetry::ParseTextLogPayload(testArena.arena, payload.data(), payload.size())));
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

    Expected<Telemetry::DiagnosticPayload> parsed = MakeUnexpected(Failure{});
    ASSERT_TRUE((parsed = Telemetry::ParseDiagnosticPayload(testArena.arena, payload.data(), payload.size())));
    EXPECT_EQ(AStringView(parsed->event), source.event);
    EXPECT_EQ(AStringView(parsed->message), source.message);

    payload[0u] = 0u;
    EXPECT_FALSE((parsed = Telemetry::ParseDiagnosticPayload(testArena.arena, payload.data(), payload.size())));
}

TEST(Telemetry, DiagnosticCaptureGuardDestructionStopsGlobalCapture){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    {
        Telemetry::DiagnosticCaptureGuard guard(recorder);
        EXPECT_TRUE(guard.installed());
        CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = DiagnosticEventName::s_Error.data(), .category = "telemetry_guard", .message = "captured diagnostic", .file = "guard.cpp", .line = 44u, });
    }

    CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = DiagnosticEventName::s_Error.data(), .category = "telemetry_guard", .message = "ignored after guard", });

    EXPECT_EQ(recorder.eventCount(), 1u);
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
        CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = DiagnosticEventName::s_Error.data(), .message = "existing callback should keep ownership", });
    }
    ClearDiagnosticEventCallback(ExistingDiagnosticCallback);

    EXPECT_EQ(s_ExistingDiagnosticCallbackCount, 1u);
    EXPECT_EQ(recorder.eventCount(), 0u);
}

TEST(Telemetry, DiagnosticObserverReusesPreparedStorageAndCountsPoolOverflow){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());
    const DiagnosticEventRecord source{ .event = "error", .message = "bounded observer", };
    const auto prepared = testArena.arena.memoryStats();
    for(u32 iteration = 0u; iteration < 3u; ++iteration){
        recorder.setCaptureOptions(Telemetry::CaptureOptions::All());
        Telemetry::DiagnosticCaptureGuard guard(recorder);
        ASSERT_TRUE(guard.installed());
        static_assert(noexcept(guard.capture(source)));
        guard.setFrameIndex(iteration);
        guard.setStreamId(9u);
        for(usize eventIndex = 0u; eventIndex < Telemetry::s_DiagnosticCaptureEventCapacity; ++eventIndex)
            guard.capture(source);
        guard.capture(source);
        EXPECT_EQ(recorder.eventCount(), Telemetry::s_DiagnosticCaptureEventCapacity);
        EXPECT_EQ(recorder.droppedDiagnosticCount(), iteration + 1u);
        const auto* const first = recorder.view().eventAt(0u);
        ASSERT_NE(first, nullptr);
        EXPECT_EQ(first->header.frameIndex, iteration);
        EXPECT_EQ(first->header.streamId, 9u);
        recorder.clear();
    }
    const auto reused = testArena.arena.memoryStats();
    EXPECT_EQ(reused.allocationCount, prepared.allocationCount);
    EXPECT_EQ(reused.reallocationCount, prepared.reallocationCount);
    EXPECT_EQ(reused.deallocationCount, prepared.deallocationCount);
}

TEST(Telemetry, DiagnosticObserverRejectsOversizeWithoutTruncatingDirectDiagnostics){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());
    Telemetry::DiagnosticCaptureGuard guard(recorder);
    ASSERT_TRUE(guard.installed());
    AString<Telemetry::TelemetryArena> message(testArena.arena);
    constexpr usize s_MessageCapacity = Telemetry::s_DiagnosticCapturePayloadCapacity - sizeof(Telemetry::EncodedDiagnosticPayloadHeader) - 5u;
    message.resize(s_MessageCapacity + 1u, 'x');
    DiagnosticEventRecord source{ .event = "error", .message = AStringView(message.data(), s_MessageCapacity), };
    const auto prepared = testArena.arena.memoryStats();
    guard.capture(source);
    ASSERT_EQ(recorder.eventCount(), 1u);
    const auto* const boundedEvent = recorder.view().eventAt(0u);
    ASSERT_NE(boundedEvent, nullptr);
    EXPECT_EQ(boundedEvent->payload.size(), Telemetry::s_DiagnosticCapturePayloadCapacity);
    source.message = AStringView(message);
    guard.capture(source);
    EXPECT_EQ(recorder.droppedDiagnosticCount(), 1u);
    EXPECT_EQ(recorder.eventCount(), 1u);
    const auto captured = testArena.arena.memoryStats();
    EXPECT_EQ(captured.allocationCount, prepared.allocationCount);
    EXPECT_EQ(captured.reallocationCount, prepared.reallocationCount);

    ASSERT_TRUE(Telemetry::RecordDiagnostic(recorder, source));
    const auto* const directEvent = recorder.view().eventAt(1u);
    ASSERT_NE(directEvent, nullptr);
    const auto parsed = Telemetry::ParseDiagnosticPayload(testArena.arena, directEvent->payload.data(), directEvent->payload.size());
    ASSERT_TRUE(parsed);
    EXPECT_EQ(AStringView(parsed->message), AStringView(message));
    EXPECT_EQ(boundedEvent->payload.size(), Telemetry::s_DiagnosticCapturePayloadCapacity);
    EXPECT_EQ(recorder.droppedDiagnosticCount(), 1u);
}

TEST(Telemetry, DiagnosticObserverCapturesDuringUnwindWithoutAllocating){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());
    Telemetry::DiagnosticCaptureGuard guard(recorder);
    ASSERT_TRUE(guard.installed());
    const auto prepared = testArena.arena.memoryStats();
    struct UnwindMarker{};
    bool caught = false;
    try{
        ScopeExit capture([]()noexcept{
            CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = "warning", .category = "unwind", .message = "cleanup failed", });
        });
        throw UnwindMarker{};
    }
    catch(const UnwindMarker&){
        caught = true;
    }
    const auto captured = testArena.arena.memoryStats();
    EXPECT_TRUE(caught);
    EXPECT_EQ(recorder.eventCount(), 1u);
    EXPECT_EQ(recorder.droppedDiagnosticCount(), 0u);
    EXPECT_EQ(captured.allocationCount, prepared.allocationCount);
    EXPECT_EQ(captured.reallocationCount, prepared.reallocationCount);
    const auto* const event = recorder.view().eventAt(0u);
    ASSERT_NE(event, nullptr);
    const auto parsed = Telemetry::ParseDiagnosticPayload(testArena.arena, event->payload.data(), event->payload.size());
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed->category, "unwind");
}

TEST(Telemetry, DiagnosticObserverPreservesPublishedRecordsAcrossFlagTogglesAndFiltersDisabledCapture){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());
    Telemetry::DiagnosticCaptureGuard guard(recorder);
    ASSERT_TRUE(guard.installed());
    const DiagnosticEventRecord source{ .event = "error", .message = "retained", };
    guard.capture(source);
    ASSERT_EQ(recorder.eventCount(), 1u);
    const auto* const retained = recorder.view().eventAt(0u);
    ASSERT_NE(retained, nullptr);
    const auto prepared = testArena.arena.memoryStats();
    for(u32 iteration = 0u; iteration < 3u; ++iteration){
        recorder.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());
        guard.capture(source);
        EXPECT_EQ(recorder.droppedDiagnosticCount(), 0u);
        EXPECT_EQ(recorder.view().eventAt(0u), retained);
        recorder.setCaptureOptions(Telemetry::CaptureOptions::All());
        guard.capture(source);
        EXPECT_EQ(recorder.eventCount(), iteration + 2u);
    }
    const auto toggled = testArena.arena.memoryStats();
    EXPECT_EQ(toggled.allocationCount, prepared.allocationCount);
    EXPECT_EQ(toggled.reallocationCount, prepared.reallocationCount);
    EXPECT_EQ(toggled.deallocationCount, prepared.deallocationCount);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::Disabled());
    guard.capture(source);
    EXPECT_EQ(recorder.eventCount(), 0u);
    EXPECT_EQ(recorder.droppedDiagnosticCount(), 0u);
    EXPECT_LT(testArena.arena.memoryStats().usedBytes, prepared.usedBytes);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());
    guard.capture(source);
    EXPECT_EQ(recorder.eventCount(), 1u);
}

TEST(Telemetry, DiagnosticObserverDoesNotGrowPublicationStorageWhenOrdinaryEventsFillIt){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());
    Telemetry::DiagnosticCaptureGuard guard(recorder);
    ASSERT_TRUE(guard.installed());
    for(usize eventIndex = 0u; eventIndex < Telemetry::s_DiagnosticCaptureEventCapacity; ++eventIndex)
        ASSERT_TRUE(recorder.recordBinary(Telemetry::EventKind::PerfFrame, 0u, nullptr, 0u));
    const auto filled = testArena.arena.memoryStats();
    const DiagnosticEventRecord source{ .event = "error", .message = "publication full", };
    guard.capture(source);
    EXPECT_EQ(recorder.droppedDiagnosticCount(), 1u);
    EXPECT_EQ(testArena.arena.memoryStats().allocationCount, filled.allocationCount);
    EXPECT_EQ(testArena.arena.memoryStats().reallocationCount, filled.reallocationCount);
    recorder.clear();
    guard.capture(source);
    EXPECT_EQ(recorder.eventCount(), 1u);
}

TEST(Telemetry, DiagnosticObserverPreservesOrderingAndPayloadLifetimeDuringBuilderReentry){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());
    Telemetry::DiagnosticCaptureGuard guard(recorder);
    ASSERT_TRUE(guard.installed());
    bool captureDidNotAllocate = false;
    ASSERT_TRUE(recorder.recordBuiltPayload(Telemetry::EventKind::PerfFrame, 7u, 3u, [&](Telemetry::TelemetryArena&, Telemetry::TelemetryBytes& payload){
            const auto before = testArena.arena.memoryStats();
            CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = "error", .message = "builder callback", });
            const auto after = testArena.arena.memoryStats();
            captureDidNotAllocate = before.allocationCount == after.allocationCount && before.reallocationCount == after.reallocationCount;
            payload.push_back(17u);
            return recorder.recordBinary(Telemetry::EventKind::MemoryFrame, 8u, nullptr, 0u);
        }
    ));
    ASSERT_TRUE(captureDidNotAllocate);
    ASSERT_EQ(recorder.eventCount(), 3u);
    const auto events = recorder.view();
    EXPECT_EQ(events.eventAt(0u)->header.kind, Telemetry::EventKind::Diagnostic);
    EXPECT_EQ(events.eventAt(1u)->header.kind, Telemetry::EventKind::MemoryFrame);
    EXPECT_EQ(events.eventAt(2u)->header.kind, Telemetry::EventKind::PerfFrame);
    const auto* const diagnostic = events.eventAt(0u);
    ASSERT_TRUE(recorder.recordBinary(Telemetry::EventKind::PerfFrame, 9u, nullptr, 0u));
    EXPECT_EQ(events.eventAt(0u), diagnostic);
    const auto parsed = Telemetry::ParseDiagnosticPayload(testArena.arena, diagnostic->payload.data(), diagnostic->payload.size());
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed->message, "builder callback");
}

TEST(Telemetry, ConcurrentDiagnosticObserversAccountForEveryAdmittedAttempt){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());
    Telemetry::DiagnosticCaptureGuard guard(recorder);
    ASSERT_TRUE(guard.installed());
    constexpr usize s_AttemptsPerThread = 256u;
    const DiagnosticEventRecord source{ .event = "error", .message = "concurrent bounded observer", };
    AtomicFlag start;
    const auto capture = [&](){
        while(!start.test(MemoryOrder::acquire))
            start.wait(false, MemoryOrder::acquire);
        for(usize attempt = 0u; attempt < s_AttemptsPerThread; ++attempt)
            guard.capture(source);
    };
    const auto prepared = testArena.arena.memoryStats();
    Thread first(capture);
    Thread second(capture);
    start.testAndSet(MemoryOrder::release);
    start.notifyAll();
    first.join();
    second.join();
    const auto captured = testArena.arena.memoryStats();
    EXPECT_GT(recorder.eventCount(), 0u);
    EXPECT_LE(recorder.eventCount(), Telemetry::s_DiagnosticCaptureEventCapacity);
    EXPECT_EQ(recorder.eventCount() + recorder.droppedDiagnosticCount(), s_AttemptsPerThread * 2u);
    EXPECT_EQ(captured.allocationCount, prepared.allocationCount);
    EXPECT_EQ(captured.reallocationCount, prepared.reallocationCount);
    const u64 dropped = recorder.droppedDiagnosticCount();
    recorder.setCaptureOptions(Telemetry::CaptureOptions::Disabled());
    guard.capture(source);
    EXPECT_EQ(recorder.droppedDiagnosticCount(), dropped);
    EXPECT_EQ(recorder.eventCount(), 0u);
}

TEST(Telemetry, DiagnosticCaptureGuardConcurrentLifetimeStress){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    AtomicFlag captureThreadStarted;
    AtomicFlag stopCaptureThread;
    Thread captureThread([&captureThreadStarted, &stopCaptureThread](){
        captureThreadStarted.testAndSet(MemoryOrder::release);
        captureThreadStarted.notifyAll();
        while(!stopCaptureThread.test(MemoryOrder::acquire)){
            CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = DiagnosticEventName::s_Error.data(), .category = "telemetry_guard", .message = "capture during destruction", });
            YieldThread();
        }
    }
    );

    while(!captureThreadStarted.test(MemoryOrder::acquire))
        captureThreadStarted.wait(false, MemoryOrder::acquire);

    constexpr u32 s_IterationCount = 64u;
    bool allGuardsInstalled = true;
    bool allIterationsCaptured = true;
    bool dropsMonotonic = true;
    usize observedEventCount = 0u;
    u64 previousDroppedCount = 0u;
    for(u32 iteration = 0u; iteration < s_IterationCount; ++iteration){
        recorder.clear();
        {
            Telemetry::DiagnosticCaptureGuard guard(recorder);
            if(!guard.installed()){
                allGuardsInstalled = false;
                break;
            }

            const auto deadline = TimerAddMS(TimerNow(), 5000);
            while(recorder.eventCount() == 0u && TimerNow() < deadline)
                YieldThread();
        }
        const usize captured = recorder.eventCount();
        if(captured == 0u){
            allIterationsCaptured = false;
            break;
        }
        observedEventCount += captured;
        const u64 dropped = recorder.droppedDiagnosticCount();
        dropsMonotonic = dropsMonotonic && dropped >= previousDroppedCount;
        previousDroppedCount = dropped;
    }

    stopCaptureThread.testAndSet(MemoryOrder::release);
    captureThread.join();
    EXPECT_TRUE(allGuardsInstalled);
    EXPECT_TRUE(allIterationsCaptured);
    EXPECT_TRUE(dropsMonotonic);
    EXPECT_GE(observedEventCount, s_IterationCount);

    {
        Telemetry::DiagnosticCaptureGuard finalGuard(recorder);
        EXPECT_TRUE(finalGuard.installed());
    }

    const usize eventCountAfterFinalDestruction = recorder.eventCount();
    CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = DiagnosticEventName::s_Error.data(), .category = "telemetry_guard", .message = "ignored after destruction", });
    EXPECT_EQ(recorder.eventCount(), eventCountAfterFinalDestruction);

    const Telemetry::EventView events = recorder.view();
    for(usize eventIndex = 0u; eventIndex < events.eventCount(); ++eventIndex){
        const Telemetry::EventRecord* const event = events.eventAt(eventIndex);
        ASSERT_NE(event, nullptr);

        Expected<Telemetry::DiagnosticPayload> parsed = MakeUnexpected(Failure{});
        ASSERT_TRUE((parsed = Telemetry::ParseDiagnosticPayload(testArena.arena, event->payload.data(), event->payload.size())));
        EXPECT_EQ(parsed->event, DiagnosticEventName::s_Error);
        EXPECT_EQ(parsed->category, "telemetry_guard");
        EXPECT_EQ(parsed->message, "capture during destruction");
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

