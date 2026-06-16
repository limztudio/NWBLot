// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/test_context.h>
#include <tests/capturing_logger.h>

#include <core/telemetry/module.h>

#include <global/filesystem/operations.h>


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
    NWB_TELEMETRY_TEST_CHECK(context, Telemetry::CaptureAllowsEventKind(perf, Telemetry::EventKind::MemoryFrame));
    NWB_TELEMETRY_TEST_CHECK(context, Telemetry::CaptureAllowsEventKind(all, Telemetry::EventKind::FrameGraphFrame));
    NWB_TELEMETRY_TEST_CHECK(context, !Telemetry::CaptureAllowsEventKind(frameGraph, Telemetry::EventKind::PerfFrame));
    NWB_TELEMETRY_TEST_CHECK(context, !Telemetry::CaptureAllowsEventKind(frameGraph, Telemetry::EventKind::MemoryFrame));
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

static ::Path<NWB::Core::Alloc::GlobalArena> TelemetryTestStorageDirectory(NWB::Core::Alloc::GlobalArena& arena){
    ::Path<NWB::Core::Alloc::GlobalArena> executableDirectory(arena);
    if(GetExecutableDirectory(executableDirectory))
        return executableDirectory / "telemetry_test_storage";

    return ::Path<NWB::Core::Alloc::GlobalArena>(arena, "telemetry_test_storage");
}

static void TestEventStreamArchiveRoundTrip(TestContext& context){
    TestArena testArena;
    const ::Path<NWB::Core::Alloc::GlobalArena> storageDirectory = TelemetryTestStorageDirectory(testArena.arena);
    const ::Path<NWB::Core::Alloc::GlobalArena> archivePath = storageDirectory / "stream.nwbs";

    ErrorCode error;
    static_cast<void>(RemoveAllIfExists(storageDirectory, error));
    error.clear();
    static_cast<void>(EnsureDirectories(storageDirectory, error));
    NWB_TELEMETRY_TEST_CHECK(context, !error);

    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());
    NWB_TELEMETRY_TEST_CHECK(context, Telemetry::RecordTextLog(
        recorder,
        NWB::Core::Common::LogType::Info,
        NWB_TEXT("archive text"),
        404u,
        19u
    ));

    const Telemetry::ArchiveResult writeResult = Telemetry::WriteEventStreamArchive(testArena.arena, recorder.view(), archivePath);
    NWB_TELEMETRY_TEST_CHECK(context, writeResult.ok());
    NWB_TELEMETRY_TEST_CHECK(context, writeResult.byteCount == sizeof(Telemetry::EncodedStreamHeader) + sizeof(Telemetry::EncodedEventHeader) + recorder.view().eventAt(0u)->payload.size());
    NWB_TELEMETRY_TEST_CHECK(context, FileExists(archivePath, error));
    NWB_TELEMETRY_TEST_CHECK(context, !error);

    Telemetry::Recorder decoded(testArena.arena);
    const Telemetry::ArchiveResult readResult = Telemetry::ReadEventStreamArchive(testArena.arena, archivePath, decoded);
    NWB_TELEMETRY_TEST_CHECK(context, readResult.ok());
    NWB_TELEMETRY_TEST_CHECK(context, readResult.status == Telemetry::ArchiveStatus::Ok);
    NWB_TELEMETRY_TEST_CHECK(context, readResult.decode.ok());
    NWB_TELEMETRY_TEST_CHECK(context, readResult.byteCount == writeResult.byteCount);
    NWB_TELEMETRY_TEST_CHECK(context, decoded.eventCount() == 1u);

    const Telemetry::EventRecord* event = decoded.view().eventAt(0u);
    NWB_TELEMETRY_TEST_CHECK(context, event != nullptr);
    if(event){
        NWB_TELEMETRY_TEST_CHECK(context, event->header.kind == Telemetry::EventKind::TextLog);
        NWB_TELEMETRY_TEST_CHECK(context, event->header.frameIndex == 404u);
        NWB_TELEMETRY_TEST_CHECK(context, event->header.streamId == 19u);

        Telemetry::TextLogPayload parsed(testArena.arena);
        NWB_TELEMETRY_TEST_CHECK(context, Telemetry::ParseTextLogPayload(testArena.arena, event->payload.data(), event->payload.size(), parsed));
        NWB_TELEMETRY_TEST_CHECK(context, parsed.messageUtf8 == "archive text");
    }

    static_cast<void>(RemoveAllIfExists(storageDirectory, error));
}

static void TestCaptureSessionFlushArchiveClearsOnSuccess(TestContext& context){
    TestArena testArena;
    const ::Path<NWB::Core::Alloc::GlobalArena> storageDirectory = TelemetryTestStorageDirectory(testArena.arena);
    const ::Path<NWB::Core::Alloc::GlobalArena> archivePath = storageDirectory / "session.nwbs";

    ErrorCode error;
    static_cast<void>(RemoveAllIfExists(storageDirectory, error));
    error.clear();
    static_cast<void>(EnsureDirectories(storageDirectory, error));
    NWB_TELEMETRY_TEST_CHECK(context, !error);

    Telemetry::CaptureSession session(testArena.arena);
    session.setCaptureOptions(Telemetry::CaptureOptions::All());
    NWB_TELEMETRY_TEST_CHECK(context, session.enabled());
    NWB_TELEMETRY_TEST_CHECK(context, Telemetry::RecordTextLog(
        session.recorder(),
        NWB::Core::Common::LogType::Info,
        NWB_TEXT("session archive"),
        505u,
        21u
    ));
    NWB_TELEMETRY_TEST_CHECK(context, session.eventCount() == 1u);

    const Telemetry::ArchiveResult writeResult = session.flushArchive(archivePath, true);
    NWB_TELEMETRY_TEST_CHECK(context, writeResult.ok());
    NWB_TELEMETRY_TEST_CHECK(context, session.eventCount() == 0u);

    Telemetry::Recorder decoded(testArena.arena);
    const Telemetry::ArchiveResult readResult = Telemetry::ReadEventStreamArchive(testArena.arena, archivePath, decoded);
    NWB_TELEMETRY_TEST_CHECK(context, readResult.ok());
    NWB_TELEMETRY_TEST_CHECK(context, decoded.eventCount() == 1u);

    const Telemetry::EventRecord* event = decoded.view().eventAt(0u);
    NWB_TELEMETRY_TEST_CHECK(context, event != nullptr);
    if(event){
        NWB_TELEMETRY_TEST_CHECK(context, event->header.kind == Telemetry::EventKind::TextLog);
        NWB_TELEMETRY_TEST_CHECK(context, event->header.frameIndex == 505u);
        NWB_TELEMETRY_TEST_CHECK(context, event->header.streamId == 21u);
    }

    static_cast<void>(RemoveAllIfExists(storageDirectory, error));
}

static void TestCaptureSessionRecordsTextLogWithContext(TestContext& context){
    TestArena testArena;
    Telemetry::CaptureSession session(testArena.arena);
    session.setCaptureOptions(Telemetry::CaptureOptions::All());
    session.setFrameIndex(606u);
    session.setStreamId(24u);

    NWB_TELEMETRY_TEST_CHECK(context, session.frameIndex() == 606u);
    NWB_TELEMETRY_TEST_CHECK(context, session.streamId() == 24u);
    NWB_TELEMETRY_TEST_CHECK(context, session.textLogCaptureLogger().frameIndex() == 606u);
    NWB_TELEMETRY_TEST_CHECK(context, session.textLogCaptureLogger().streamId() == 24u);
    NWB_TELEMETRY_TEST_CHECK(context, session.recordTextLog(
        NWB::Core::Common::LogType::Warning,
        NWB_TEXT("session text")
    ));

    const Telemetry::EventRecord* event = session.view().eventAt(0u);
    NWB_TELEMETRY_TEST_CHECK(context, event != nullptr);
    if(!event)
        return;

    NWB_TELEMETRY_TEST_CHECK(context, event->header.kind == Telemetry::EventKind::TextLog);
    NWB_TELEMETRY_TEST_CHECK(context, event->header.frameIndex == 606u);
    NWB_TELEMETRY_TEST_CHECK(context, event->header.streamId == 24u);

    Telemetry::TextLogPayload parsed(testArena.arena);
    NWB_TELEMETRY_TEST_CHECK(context, Telemetry::ParseTextLogPayload(testArena.arena, event->payload.data(), event->payload.size(), parsed));
    NWB_TELEMETRY_TEST_CHECK(context, parsed.type == NWB::Core::Common::LogType::Warning);
    NWB_TELEMETRY_TEST_CHECK(context, parsed.messageUtf8 == "session text");
}

static void TestCaptureSessionTextLoggerForwardsAndRecords(TestContext& context){
    TestArena testArena;
    Telemetry::CaptureSession session(testArena.arena);
    session.setCaptureOptions(Telemetry::CaptureOptions::All());
    session.setFrameIndex(607u);
    session.setStreamId(25u);

    NWB::Tests::CapturingLogger forwardLogger;
    session.setForwardLogger(&forwardLogger);

    Telemetry::TextLogCaptureLogger& logger = session.textLogCaptureLogger();
    logger.enqueue(NWB::Core::Common::LogString(NWB_TEXT("session bridged"), logger.arena()), NWB::Core::Common::LogType::Error);

    NWB_TELEMETRY_TEST_CHECK(context, forwardLogger.messageCount() == 1u);
    NWB_TELEMETRY_TEST_CHECK(context, forwardLogger.errorCount() == 1u);
    NWB_TELEMETRY_TEST_CHECK(context, forwardLogger.sawMessageContaining(NWB_TEXT("session bridged")));
    NWB_TELEMETRY_TEST_CHECK(context, session.eventCount() == 1u);

    const Telemetry::EventRecord* event = session.view().eventAt(0u);
    NWB_TELEMETRY_TEST_CHECK(context, event != nullptr);
    if(!event)
        return;

    NWB_TELEMETRY_TEST_CHECK(context, event->header.kind == Telemetry::EventKind::TextLog);
    NWB_TELEMETRY_TEST_CHECK(context, event->header.frameIndex == 607u);
    NWB_TELEMETRY_TEST_CHECK(context, event->header.streamId == 25u);

    Telemetry::TextLogPayload parsed(testArena.arena);
    NWB_TELEMETRY_TEST_CHECK(context, Telemetry::ParseTextLogPayload(testArena.arena, event->payload.data(), event->payload.size(), parsed));
    NWB_TELEMETRY_TEST_CHECK(context, parsed.type == NWB::Core::Common::LogType::Error);
    NWB_TELEMETRY_TEST_CHECK(context, parsed.messageUtf8 == "session bridged");
}

static void TestCaptureSessionLogRegistrationGuardForwardsAndRestores(TestContext& context){
    TestArena testArena;
    Telemetry::CaptureSession session(testArena.arena);
    session.setCaptureOptions(Telemetry::CaptureOptions::All());
    session.setFrameIndex(609u);
    session.setStreamId(27u);

    NWB::Tests::CapturingLogger previousLogger;
    {
        NWB::Core::Common::LoggerRegistrationGuard previousRegistration(previousLogger);
        NWB_TELEMETRY_TEST_CHECK(context, NWB::Core::Common::CurrentLogger() == &previousLogger);

        {
            Telemetry::CaptureSessionLogRegistrationGuard sessionRegistration(session);
            NWB::Core::Common::ILogger* const logger = NWB::Core::Common::CurrentLogger();
            NWB_TELEMETRY_TEST_CHECK(context, logger == &session.textLogCaptureLogger());
            {
                Telemetry::CaptureSessionLogRegistrationGuard nestedRegistration(session);
                NWB_TELEMETRY_TEST_CHECK(context, session.textLogCaptureLogger().forwardLogger() == &previousLogger);
            }
            NWB_TELEMETRY_TEST_CHECK(context, NWB::Core::Common::CurrentLogger() == &session.textLogCaptureLogger());
            if(logger)
                logger->enqueue(NWB::Core::Common::LogString(NWB_TEXT("session registered"), logger->arena()), NWB::Core::Common::LogType::CriticalWarning);
        }

        NWB_TELEMETRY_TEST_CHECK(context, NWB::Core::Common::CurrentLogger() == &previousLogger);
        NWB::Core::Common::ILogger* const logger = NWB::Core::Common::CurrentLogger();
        if(logger)
            logger->enqueue(NWB::Core::Common::LogString(NWB_TEXT("after session guard"), logger->arena()), NWB::Core::Common::LogType::Info);
    }

    NWB_TELEMETRY_TEST_CHECK(context, previousLogger.messageCount() == 2u);
    NWB_TELEMETRY_TEST_CHECK(context, previousLogger.sawMessageContaining(NWB_TEXT("session registered")));
    NWB_TELEMETRY_TEST_CHECK(context, previousLogger.sawMessageContaining(NWB_TEXT("after session guard")));
    NWB_TELEMETRY_TEST_CHECK(context, session.eventCount() == 1u);

    const Telemetry::EventRecord* event = session.view().eventAt(0u);
    NWB_TELEMETRY_TEST_CHECK(context, event != nullptr);
    if(!event)
        return;

    NWB_TELEMETRY_TEST_CHECK(context, event->header.kind == Telemetry::EventKind::TextLog);
    NWB_TELEMETRY_TEST_CHECK(context, event->header.frameIndex == 609u);
    NWB_TELEMETRY_TEST_CHECK(context, event->header.streamId == 27u);

    Telemetry::TextLogPayload parsed(testArena.arena);
    NWB_TELEMETRY_TEST_CHECK(context, Telemetry::ParseTextLogPayload(testArena.arena, event->payload.data(), event->payload.size(), parsed));
    NWB_TELEMETRY_TEST_CHECK(context, parsed.type == NWB::Core::Common::LogType::CriticalWarning);
    NWB_TELEMETRY_TEST_CHECK(context, parsed.messageUtf8 == "session registered");
}

static void TestCaptureSessionRecordsDiagnosticWithContext(TestContext& context){
    TestArena testArena;
    Telemetry::CaptureSession session(testArena.arena);
    session.setCaptureOptions(Telemetry::CaptureOptions::All());
    session.setFrameIndex(608u);
    session.setStreamId(26u);

    const DiagnosticEventRecord source{
        .event = DiagnosticEventName::s_Error,
        .category = "session_diagnostic",
        .message = "session diagnostic",
        .file = "session.cpp",
        .line = 66u,
    };

    NWB_TELEMETRY_TEST_CHECK(context, session.recordDiagnostic(source));

    const Telemetry::EventRecord* event = session.view().eventAt(0u);
    NWB_TELEMETRY_TEST_CHECK(context, event != nullptr);
    if(!event)
        return;

    NWB_TELEMETRY_TEST_CHECK(context, event->header.kind == Telemetry::EventKind::Diagnostic);
    NWB_TELEMETRY_TEST_CHECK(context, event->header.frameIndex == 608u);
    NWB_TELEMETRY_TEST_CHECK(context, event->header.streamId == 26u);

    Telemetry::DiagnosticPayload parsed(testArena.arena);
    NWB_TELEMETRY_TEST_CHECK(context, Telemetry::ParseDiagnosticPayload(testArena.arena, event->payload.data(), event->payload.size(), parsed));
    NWB_TELEMETRY_TEST_CHECK(context, parsed.category == "session_diagnostic");
    NWB_TELEMETRY_TEST_CHECK(context, parsed.message == "session diagnostic");
    NWB_TELEMETRY_TEST_CHECK(context, parsed.file == "session.cpp");
    NWB_TELEMETRY_TEST_CHECK(context, parsed.line == 66u);
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

static void TestCaptureSessionRecordsFrameGraphWithContext(TestContext& context){
    TestArena testArena;
    Telemetry::CaptureSession session(testArena.arena);
    session.setCaptureOptions(Telemetry::CaptureOptions::All());
    session.setFrameIndex(910u);
    session.setStreamId(15u);

    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestFrameGraph(testArena.arena, nodes, edges);

    NWB_TELEMETRY_TEST_CHECK(context, session.recordFrameGraph(nodes, edges));

    const Telemetry::EventRecord* event = session.view().eventAt(0u);
    NWB_TELEMETRY_TEST_CHECK(context, event != nullptr);
    if(!event)
        return;

    NWB_TELEMETRY_TEST_CHECK(context, event->header.kind == Telemetry::EventKind::FrameGraphFrame);
    NWB_TELEMETRY_TEST_CHECK(context, event->header.payloadFormat == Telemetry::PayloadFormat::Binary);
    NWB_TELEMETRY_TEST_CHECK(context, event->header.frameIndex == 910u);
    NWB_TELEMETRY_TEST_CHECK(context, event->header.streamId == 15u);

    Telemetry::FrameGraphPayload parsed(testArena.arena);
    NWB_TELEMETRY_TEST_CHECK(context, Telemetry::ParseFrameGraphPayload(testArena.arena, event->payload.data(), event->payload.size(), parsed));
    NWB_TELEMETRY_TEST_CHECK(context, parsed.frameIndex == 910u);
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

static void TestPerfMemoryPayloadRoundTrip(TestContext& context){
    TestArena testArena;
    const Name scopeName("memory/project_arena");
    const NWB::Core::Perf::MemorySnapshot snapshot = MakeTestMemorySnapshot(scopeName);
    const NWB::Core::Perf::MemoryDelta delta = MakeTestMemoryDelta();

    Telemetry::TelemetryBytes payload(testArena.arena);
    NWB_TELEMETRY_TEST_CHECK(context, Telemetry::BuildPerfMemoryPayload(
        testArena.arena,
        scopeName,
        "Project Arena",
        snapshot,
        delta,
        payload
    ));
    NWB_TELEMETRY_TEST_CHECK(context, payload.size() == sizeof(Telemetry::EncodedPerfMemoryPayloadHeader) + sizeof("Project Arena") - 1u);

    Telemetry::PerfMemoryPayload parsed(testArena.arena);
    NWB_TELEMETRY_TEST_CHECK(context, Telemetry::ParsePerfMemoryPayload(testArena.arena, payload.data(), payload.size(), parsed));
    NWB_TELEMETRY_TEST_CHECK(context, parsed.scopeName == scopeName);
    NWB_TELEMETRY_TEST_CHECK(context, parsed.scopeText == "Project Arena");
    NWB_TELEMETRY_TEST_CHECK(context, parsed.snapshot.scopeName == scopeName);
    NWB_TELEMETRY_TEST_CHECK(context, parsed.snapshot.frameIndex == snapshot.frameIndex);
    NWB_TELEMETRY_TEST_CHECK(context, parsed.snapshot.reservedBytes == snapshot.reservedBytes);
    NWB_TELEMETRY_TEST_CHECK(context, parsed.snapshot.usedBytes == snapshot.usedBytes);
    NWB_TELEMETRY_TEST_CHECK(context, parsed.snapshot.peakUsedBytes == snapshot.peakUsedBytes);
    NWB_TELEMETRY_TEST_CHECK(context, parsed.snapshot.allocationCount == snapshot.allocationCount);
    NWB_TELEMETRY_TEST_CHECK(context, parsed.snapshot.reallocationCount == snapshot.reallocationCount);
    NWB_TELEMETRY_TEST_CHECK(context, parsed.snapshot.deallocationCount == snapshot.deallocationCount);
    NWB_TELEMETRY_TEST_CHECK(context, parsed.delta.hasSamples);
    NWB_TELEMETRY_TEST_CHECK(context, parsed.delta.previousFrameIndex == delta.previousFrameIndex);
    NWB_TELEMETRY_TEST_CHECK(context, parsed.delta.currentFrameIndex == snapshot.frameIndex);
    NWB_TELEMETRY_TEST_CHECK(context, parsed.delta.reservedBytes == delta.reservedBytes);
    NWB_TELEMETRY_TEST_CHECK(context, parsed.delta.usedBytes == delta.usedBytes);
    NWB_TELEMETRY_TEST_CHECK(context, parsed.delta.peakUsedBytes == delta.peakUsedBytes);
    NWB_TELEMETRY_TEST_CHECK(context, parsed.delta.allocationCount == delta.allocationCount);
    NWB_TELEMETRY_TEST_CHECK(context, parsed.delta.reallocationCount == delta.reallocationCount);
    NWB_TELEMETRY_TEST_CHECK(context, parsed.delta.deallocationCount == delta.deallocationCount);

    payload[0u] = 0u;
    NWB_TELEMETRY_TEST_CHECK(context, !Telemetry::ParsePerfMemoryPayload(testArena.arena, payload.data(), payload.size(), parsed));
}

static void TestPerfMemoryPayloadRejectsInvalidInput(TestContext& context){
    TestArena testArena;
    Telemetry::TelemetryBytes payload(testArena.arena);
    const Name scopeName("memory/project_arena");
    NWB::Core::Perf::MemorySnapshot snapshot = MakeTestMemorySnapshot(scopeName);
    NWB::Core::Perf::MemoryDelta delta = MakeTestMemoryDelta();

    NWB_TELEMETRY_TEST_CHECK(context, !Telemetry::BuildPerfMemoryPayload(
        testArena.arena,
        NAME_NONE,
        "Project Arena",
        snapshot,
        delta,
        payload
    ));

    NWB_TELEMETRY_TEST_CHECK(context, !Telemetry::BuildPerfMemoryPayload(
        testArena.arena,
        scopeName,
        AStringView(),
        snapshot,
        delta,
        payload
    ));

    snapshot.scopeName = Name("memory/other_arena");
    NWB_TELEMETRY_TEST_CHECK(context, !Telemetry::BuildPerfMemoryPayload(
        testArena.arena,
        scopeName,
        "Project Arena",
        snapshot,
        delta,
        payload
    ));

    snapshot = MakeTestMemorySnapshot(scopeName);
    delta.currentFrameIndex = 99u;
    NWB_TELEMETRY_TEST_CHECK(context, !Telemetry::BuildPerfMemoryPayload(
        testArena.arena,
        scopeName,
        "Project Arena",
        snapshot,
        delta,
        payload
    ));
}

static void TestRecordPerfMemoryUsesTelemetryEvent(TestContext& context){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    const Name scopeName("memory/project_arena");
    const NWB::Core::Perf::MemorySnapshot snapshot = MakeTestMemorySnapshot(scopeName);
    const NWB::Core::Perf::MemoryDelta delta = MakeTestMemoryDelta();
    NWB_TELEMETRY_TEST_CHECK(context, Telemetry::RecordPerfMemory(recorder, scopeName, snapshot, delta, 12u));

    const Telemetry::EventRecord* event = recorder.view().eventAt(0u);
    NWB_TELEMETRY_TEST_CHECK(context, event != nullptr);
    if(!event)
        return;

    NWB_TELEMETRY_TEST_CHECK(context, event->header.kind == Telemetry::EventKind::MemoryFrame);
    NWB_TELEMETRY_TEST_CHECK(context, event->header.payloadFormat == Telemetry::PayloadFormat::Binary);
    NWB_TELEMETRY_TEST_CHECK(context, event->header.frameIndex == snapshot.frameIndex);
    NWB_TELEMETRY_TEST_CHECK(context, event->header.streamId == 12u);

    Telemetry::PerfMemoryPayload parsed(testArena.arena);
    NWB_TELEMETRY_TEST_CHECK(context, Telemetry::ParsePerfMemoryPayload(testArena.arena, event->payload.data(), event->payload.size(), parsed));
    NWB_TELEMETRY_TEST_CHECK(context, parsed.scopeName == scopeName);
    NWB_TELEMETRY_TEST_CHECK(context, parsed.snapshot.usedBytes == snapshot.usedBytes);
    NWB_TELEMETRY_TEST_CHECK(context, parsed.delta.hasSamples);
    NWB_TELEMETRY_TEST_CHECK(context, parsed.delta.usedBytes == delta.usedBytes);
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

static void TestPerfViewsExposeScopes(TestContext& context){
    TestArena testArena;
    NWB::Core::Perf::TimingRecorder cpuTiming(testArena.arena);
    NWB::Core::Perf::TimingRecorder gpuTiming(testArena.arena);
    NWB::Core::Perf::MemoryRecorder memory(testArena.arena);
    NWB::Core::Perf::SessionReport report;
    BuildTestPerfReport(cpuTiming, gpuTiming, memory, report);

    NWB_TELEMETRY_TEST_CHECK(context, report.cpuTiming.scopeCount() == 1u);
    NWB_TELEMETRY_TEST_CHECK(context, report.gpuTiming.scopeCount() == 1u);
    NWB_TELEMETRY_TEST_CHECK(context, report.memory.scopeCount() == 1u);
    NWB_TELEMETRY_TEST_CHECK(context, report.cpuTiming.scopeNameAt(0u) == Name("perf/cpu/update"));
    NWB_TELEMETRY_TEST_CHECK(context, report.gpuTiming.scopeNameAt(0u) == Name("perf/gpu/frame"));
    NWB_TELEMETRY_TEST_CHECK(context, report.memory.scopeNameAt(0u) == Name("perf/memory/project"));
    NWB_TELEMETRY_TEST_CHECK(context, report.cpuTiming.scopeAt(0u).valid());
    NWB_TELEMETRY_TEST_CHECK(context, !report.cpuTiming.scopeAt(1u).valid());
    NWB_TELEMETRY_TEST_CHECK(context, report.cpuTiming.statsAt(0u).sampleCount == 2u);
    NWB_TELEMETRY_TEST_CHECK(context, report.gpuTiming.statsAt(0u).sampleCount == 1u);
    NWB_TELEMETRY_TEST_CHECK(context, report.memory.snapshotAt(0u).usedBytes == 2048u);
    NWB_TELEMETRY_TEST_CHECK(context, report.memory.deltaAt(0u).usedBytes == 1024);
}

static void TestRecordPerfSessionReportUsesTelemetryEvents(TestContext& context){
    TestArena testArena;
    NWB::Core::Perf::TimingRecorder cpuTiming(testArena.arena);
    NWB::Core::Perf::TimingRecorder gpuTiming(testArena.arena);
    NWB::Core::Perf::MemoryRecorder memory(testArena.arena);
    NWB::Core::Perf::SessionReport report;
    BuildTestPerfReport(cpuTiming, gpuTiming, memory, report);

    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    const Telemetry::PerfSessionRecordResult result = Telemetry::RecordPerfSessionReport(recorder, report, 17u);
    NWB_TELEMETRY_TEST_CHECK(context, result.recordedAny());
    NWB_TELEMETRY_TEST_CHECK(context, result.cpuTimingEvents == 1u);
    NWB_TELEMETRY_TEST_CHECK(context, result.gpuTimingEvents == 1u);
    NWB_TELEMETRY_TEST_CHECK(context, result.memoryEvents == 1u);
    NWB_TELEMETRY_TEST_CHECK(context, result.eventCount() == 3u);
    NWB_TELEMETRY_TEST_CHECK(context, recorder.eventCount() == 3u);

    const Telemetry::EventRecord* cpuEvent = recorder.view().eventAt(0u);
    const Telemetry::EventRecord* gpuEvent = recorder.view().eventAt(1u);
    const Telemetry::EventRecord* memoryEvent = recorder.view().eventAt(2u);
    NWB_TELEMETRY_TEST_CHECK(context, cpuEvent != nullptr);
    NWB_TELEMETRY_TEST_CHECK(context, gpuEvent != nullptr);
    NWB_TELEMETRY_TEST_CHECK(context, memoryEvent != nullptr);
    if(!cpuEvent || !gpuEvent || !memoryEvent)
        return;

    NWB_TELEMETRY_TEST_CHECK(context, cpuEvent->header.kind == Telemetry::EventKind::PerfFrame);
    NWB_TELEMETRY_TEST_CHECK(context, gpuEvent->header.kind == Telemetry::EventKind::PerfFrame);
    NWB_TELEMETRY_TEST_CHECK(context, memoryEvent->header.kind == Telemetry::EventKind::MemoryFrame);
    NWB_TELEMETRY_TEST_CHECK(context, cpuEvent->header.streamId == 17u);
    NWB_TELEMETRY_TEST_CHECK(context, gpuEvent->header.streamId == 17u);
    NWB_TELEMETRY_TEST_CHECK(context, memoryEvent->header.streamId == 17u);

    Telemetry::PerfTimingPayload cpuPayload(testArena.arena);
    Telemetry::PerfTimingPayload gpuPayload(testArena.arena);
    Telemetry::PerfMemoryPayload memoryPayload(testArena.arena);
    NWB_TELEMETRY_TEST_CHECK(context, Telemetry::ParsePerfTimingPayload(testArena.arena, cpuEvent->payload.data(), cpuEvent->payload.size(), cpuPayload));
    NWB_TELEMETRY_TEST_CHECK(context, Telemetry::ParsePerfTimingPayload(testArena.arena, gpuEvent->payload.data(), gpuEvent->payload.size(), gpuPayload));
    NWB_TELEMETRY_TEST_CHECK(context, Telemetry::ParsePerfMemoryPayload(testArena.arena, memoryEvent->payload.data(), memoryEvent->payload.size(), memoryPayload));
    NWB_TELEMETRY_TEST_CHECK(context, cpuPayload.source == Telemetry::PerfTimingSource::Cpu);
    NWB_TELEMETRY_TEST_CHECK(context, gpuPayload.source == Telemetry::PerfTimingSource::Gpu);
    NWB_TELEMETRY_TEST_CHECK(context, cpuPayload.scopeName == Name("perf/cpu/update"));
    NWB_TELEMETRY_TEST_CHECK(context, gpuPayload.scopeName == Name("perf/gpu/frame"));
    NWB_TELEMETRY_TEST_CHECK(context, memoryPayload.scopeName == Name("perf/memory/project"));
    NWB_TELEMETRY_TEST_CHECK(context, memoryPayload.snapshot.frameIndex == 102u);
    NWB_TELEMETRY_TEST_CHECK(context, memoryPayload.delta.hasSamples);
}

static void TestCaptureSessionRecordsPerfReport(TestContext& context){
    TestArena testArena;
    NWB::Core::Perf::TimingRecorder cpuTiming(testArena.arena);
    NWB::Core::Perf::TimingRecorder gpuTiming(testArena.arena);
    NWB::Core::Perf::MemoryRecorder memory(testArena.arena);
    NWB::Core::Perf::SessionReport report;
    BuildTestPerfReport(cpuTiming, gpuTiming, memory, report);

    Telemetry::CaptureSession session(testArena.arena);
    session.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());

    const Telemetry::PerfSessionRecordResult result = session.recordPerfReport(report, 23u);
    NWB_TELEMETRY_TEST_CHECK(context, result.eventCount() == 3u);
    NWB_TELEMETRY_TEST_CHECK(context, session.eventCount() == 3u);

    const Telemetry::EventRecord* cpuEvent = session.view().eventAt(0u);
    const Telemetry::EventRecord* gpuEvent = session.view().eventAt(1u);
    const Telemetry::EventRecord* memoryEvent = session.view().eventAt(2u);
    NWB_TELEMETRY_TEST_CHECK(context, cpuEvent != nullptr);
    NWB_TELEMETRY_TEST_CHECK(context, gpuEvent != nullptr);
    NWB_TELEMETRY_TEST_CHECK(context, memoryEvent != nullptr);
    if(!cpuEvent || !gpuEvent || !memoryEvent)
        return;

    NWB_TELEMETRY_TEST_CHECK(context, cpuEvent->header.kind == Telemetry::EventKind::PerfFrame);
    NWB_TELEMETRY_TEST_CHECK(context, gpuEvent->header.kind == Telemetry::EventKind::PerfFrame);
    NWB_TELEMETRY_TEST_CHECK(context, memoryEvent->header.kind == Telemetry::EventKind::MemoryFrame);
    NWB_TELEMETRY_TEST_CHECK(context, cpuEvent->header.streamId == 23u);
    NWB_TELEMETRY_TEST_CHECK(context, gpuEvent->header.streamId == 23u);
    NWB_TELEMETRY_TEST_CHECK(context, memoryEvent->header.streamId == 23u);
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
    __hidden_tests::TestEventStreamArchiveRoundTrip(context);
    __hidden_tests::TestCaptureSessionFlushArchiveClearsOnSuccess(context);
    __hidden_tests::TestCaptureSessionRecordsTextLogWithContext(context);
    __hidden_tests::TestCaptureSessionTextLoggerForwardsAndRecords(context);
    __hidden_tests::TestCaptureSessionLogRegistrationGuardForwardsAndRestores(context);
    __hidden_tests::TestCaptureSessionRecordsDiagnosticWithContext(context);
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
    __hidden_tests::TestCaptureSessionRecordsFrameGraphWithContext(context);
    __hidden_tests::TestPerfTimingPayloadRoundTrip(context);
    __hidden_tests::TestPerfTimingPayloadRejectsInvalidInput(context);
    __hidden_tests::TestRecordPerfTimingUsesTelemetryEvent(context);
    __hidden_tests::TestPerfMemoryPayloadRoundTrip(context);
    __hidden_tests::TestPerfMemoryPayloadRejectsInvalidInput(context);
    __hidden_tests::TestRecordPerfMemoryUsesTelemetryEvent(context);
    __hidden_tests::TestPerfViewsExposeScopes(context);
    __hidden_tests::TestRecordPerfSessionReportUsesTelemetryEvents(context);
    __hidden_tests::TestCaptureSessionRecordsPerfReport(context);
})


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
