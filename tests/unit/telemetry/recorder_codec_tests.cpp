// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "telemetry_test_helpers.h"

#include <global/binary.h>
#include <global/thread.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_recorder_codec_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;



using namespace TelemetryTestDetail;



TEST(Telemetry, RecorderFiltersOwnsCallerPayloadAndRejectsEndIndex){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::FrameGraphOnly());

    const u8 perfPayload[] = { 1u, s_ExpectedDualCount };
    EXPECT_FALSE(recorder.recordBinary(Telemetry::EventKind::PerfFrame, 12u, perfPayload, sizeof(perfPayload)));
    EXPECT_EQ(recorder.eventCount(), 0u);

    u8 frameGraphPayload[] = { 4u, 5u, 6u };
    EXPECT_TRUE(recorder.recordBinary(Telemetry::EventKind::FrameGraphFrame, 13u, frameGraphPayload, sizeof(frameGraphPayload), 7u));
    frameGraphPayload[0u] = 99u;

    const Telemetry::EventView view = recorder.view();
    EXPECT_EQ(view.eventCount(), 1u);

    const Telemetry::EventRecord* record = view.eventAt(0u);
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->payload.size(), 3u);
    EXPECT_EQ(record->payload[0u], 4u);

    EXPECT_EQ(view.eventAt(1u), nullptr);
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

    auto result = Telemetry::DecodeEvent(testArena.arena, encoded.data(), encoded.size());
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().status, Telemetry::DecodeStatus::InvalidHeader);

    result = Telemetry::DecodeEvent(testArena.arena, encoded.data(), sizeof(Telemetry::EncodedEventHeader) - 1u);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().status, Telemetry::DecodeStatus::TruncatedHeader);
}

TEST(Telemetry, EventCodecReportsTruncatedPayload){
    TestArena testArena;
    Telemetry::TelemetryBytes encoded(testArena.arena);

    const u8 payload[] = { 1u, s_ExpectedDualCount, 3u };
    Telemetry::EventHeader header;
    header.kind = Telemetry::EventKind::PerfFrame;
    header.payloadBytes = sizeof(payload);
    EXPECT_TRUE(Telemetry::EncodeEvent(header, payload, sizeof(payload), encoded));

    const auto result = Telemetry::DecodeEvent(testArena.arena, encoded.data(), encoded.size() - 1u);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().status, Telemetry::DecodeStatus::TruncatedPayload);
}

TEST(Telemetry, EventStreamCodecHandlesEmptyStreams){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);

    Telemetry::TelemetryBytes encoded(testArena.arena);
    EXPECT_TRUE(Telemetry::EncodeEventStream(recorder.view(), encoded));
    EXPECT_EQ(encoded.size(), sizeof(Telemetry::EncodedStreamHeader));

    Telemetry::Recorder decoded(testArena.arena);
    const auto result = Telemetry::DecodeEventStream(testArena.arena, encoded.data(), encoded.size(), decoded);
    ASSERT_TRUE(result);
    EXPECT_EQ(*result, encoded.size());
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
    auto result = Telemetry::DecodeEventStream(testArena.arena, encoded.data(), sizeof(Telemetry::EncodedStreamHeader) - 1u, decoded);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().status, Telemetry::DecodeStatus::TruncatedHeader);

    Telemetry::TelemetryBytes corrupted(testArena.arena);
    corrupted = encoded;
    corrupted[0u] = 0u;
    result = Telemetry::DecodeEventStream(testArena.arena, corrupted.data(), corrupted.size(), decoded);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().status, Telemetry::DecodeStatus::InvalidHeader);

    result = Telemetry::DecodeEventStream(testArena.arena, encoded.data(), encoded.size() - 1u, decoded);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().status, Telemetry::DecodeStatus::TruncatedPayload);

    corrupted = encoded;
    Telemetry::EncodedStreamHeader streamHeader;
    NWB_MEMCPY(&streamHeader, sizeof(streamHeader), corrupted.data(), sizeof(streamHeader));
    streamHeader.eventCount = 0u;
    NWB_MEMCPY(corrupted.data(), corrupted.size(), &streamHeader, sizeof(streamHeader));
    result = Telemetry::DecodeEventStream(testArena.arena, corrupted.data(), corrupted.size(), decoded);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().status, Telemetry::DecodeStatus::InvalidHeader);
}

TEST(Telemetry, EventCodecRejectsNonCurrentVersionsAndRecovers){
    TestArena testArena;
    Telemetry::TelemetryBytes encoded(testArena.arena);
    const u8 payload[] = { 7u, 8u };
    Telemetry::EventHeader header;
    header.kind = Telemetry::EventKind::PerfFrame;
    header.payloadBytes = sizeof(payload);
    ASSERT_TRUE(Telemetry::EncodeEvent(header, payload, sizeof(payload), encoded));
    const Telemetry::TelemetryBytes current = encoded;

    const u16 unsupportedVersions[] = {
        0u,
        static_cast<u16>(Telemetry::s_TelemetryFormatVersion + 1u),
        Limit<u16>::s_Max,
    };
    Expected<Telemetry::DecodedEvent, Telemetry::DecodeResult> decoded = MakeUnexpected(Telemetry::DecodeResult{});
    Telemetry::Recorder recorder(testArena.arena);
    for(const u16 version : unsupportedVersions){
        SCOPED_TRACE(version);
        header.version = version;
        EXPECT_FALSE(Telemetry::EncodeEvent(header, payload, sizeof(payload), encoded));
        EXPECT_EQ(encoded, current);
        EXPECT_FALSE(recorder.append(header, payload, sizeof(payload)));
        EXPECT_EQ(recorder.eventCount(), 0u);

        usize cursor = 0u;
        const auto readEncodedHeader = ReadPOD<Telemetry::EncodedEventHeader>(current, cursor);
        ASSERT_TRUE(readEncodedHeader);
        Telemetry::EncodedEventHeader encodedHeader = *readEncodedHeader;
        encodedHeader.version = version;
        NWB_MEMCPY(encoded.data(), encoded.size(), &encodedHeader, sizeof(encodedHeader));
        decoded = Telemetry::DecodeEvent(testArena.arena, encoded.data(), encoded.size());
        ASSERT_FALSE(decoded);
        EXPECT_EQ(decoded.error().status, Telemetry::DecodeStatus::InvalidHeader);
        encoded = current;
        ASSERT_TRUE((decoded = Telemetry::DecodeEvent(testArena.arena, current.data(), current.size())));
        ASSERT_EQ(decoded->event.payload.size(), sizeof(payload));
        EXPECT_EQ(decoded->event.payload[0u], payload[0u]);
    }
    header.version = Telemetry::s_TelemetryFormatVersion;
    ASSERT_TRUE(recorder.append(header, payload, sizeof(payload)));
    EXPECT_EQ(recorder.eventCount(), 1u);
}

TEST(Telemetry, EventStreamRejectsNonCurrentStreamAndNestedEventVersionsAndRecovers){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());
    const u8 payload[] = { 7u, 8u };
    ASSERT_TRUE(recorder.recordBinary(Telemetry::EventKind::PerfFrame, 1u, payload, sizeof(payload)));
    Telemetry::TelemetryBytes encoded(testArena.arena);
    ASSERT_TRUE(Telemetry::EncodeEventStream(recorder.view(), encoded));
    const Telemetry::TelemetryBytes current = encoded;

    const u16 unsupportedVersions[] = {
        0u,
        static_cast<u16>(Telemetry::s_TelemetryFormatVersion + 1u),
        Limit<u16>::s_Max,
    };
    Telemetry::Recorder decoded(testArena.arena);
    for(const u16 version : unsupportedVersions){
        SCOPED_TRACE(version);
        usize cursor = 0u;
        const auto readStreamHeader = ReadPOD<Telemetry::EncodedStreamHeader>(current, cursor);
        ASSERT_TRUE(readStreamHeader);
        Telemetry::EncodedStreamHeader streamHeader = *readStreamHeader;
        streamHeader.version = version;
        NWB_MEMCPY(encoded.data(), encoded.size(), &streamHeader, sizeof(streamHeader));
        const auto invalidStream = Telemetry::DecodeEventStream(testArena.arena, encoded.data(), encoded.size(), decoded);
        ASSERT_FALSE(invalidStream);
        EXPECT_EQ(invalidStream.error().status, Telemetry::DecodeStatus::InvalidHeader);
        EXPECT_EQ(decoded.eventCount(), 0u);

        encoded = current;
        const auto readEventHeader = ReadPOD<Telemetry::EncodedEventHeader>(current, cursor);
        ASSERT_TRUE(readEventHeader);
        Telemetry::EncodedEventHeader eventHeader = *readEventHeader;
        eventHeader.version = version;
        NWB_MEMCPY(
            encoded.data() + sizeof(streamHeader), encoded.size() - sizeof(streamHeader), &eventHeader, sizeof(eventHeader)
        );
        const auto invalidEvent = Telemetry::DecodeEventStream(testArena.arena, encoded.data(), encoded.size(), decoded);
        ASSERT_FALSE(invalidEvent);
        EXPECT_EQ(invalidEvent.error().status, Telemetry::DecodeStatus::InvalidHeader);
        EXPECT_EQ(decoded.eventCount(), 0u);
        encoded = current;
        ASSERT_TRUE(Telemetry::DecodeEventStream(testArena.arena, current.data(), current.size(), decoded));
        ASSERT_EQ(decoded.eventCount(), 1u);
        const Telemetry::EventRecord* record = decoded.view().eventAt(0u);
        ASSERT_NE(record, nullptr);
        EXPECT_EQ(record->payload.size(), sizeof(payload));
    }
}

TEST(Telemetry, DiagnosticPayloadRejectsNonCurrentVersionsAndRecovers){
    TestArena testArena;
    DiagnosticEventRecord record;
    record.event = "assert";
    record.message = "retained diagnostic";
    Telemetry::TelemetryBytes encoded(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildDiagnosticPayload(testArena.arena, record, encoded));
    const Telemetry::TelemetryBytes current = encoded;
    const u16 unsupportedVersions[] = {
        0u,
        static_cast<u16>(Telemetry::s_DiagnosticPayloadVersion + 1u),
        Limit<u16>::s_Max,
    };
    Expected<Telemetry::DiagnosticPayload> parsed = MakeUnexpected(Failure{});
    for(const u16 version : unsupportedVersions){
        SCOPED_TRACE(version);
        ASSERT_TRUE((parsed = Telemetry::ParseDiagnosticPayload(testArena.arena, current.data(), current.size())));
        usize cursor = 0u;
        const auto readHeader = ReadPOD<Telemetry::EncodedDiagnosticPayloadHeader>(current, cursor);
        ASSERT_TRUE(readHeader);
        Telemetry::EncodedDiagnosticPayloadHeader header = *readHeader;
        header.version = version;
        NWB_MEMCPY(encoded.data(), encoded.size(), &header, sizeof(header));
        EXPECT_FALSE((parsed = Telemetry::ParseDiagnosticPayload(testArena.arena, encoded.data(), encoded.size())));
        ASSERT_TRUE((parsed = Telemetry::ParseDiagnosticPayload(testArena.arena, current.data(), current.size())));
        EXPECT_EQ(AStringView(parsed->message), record.message);
    }
}

TEST(Telemetry, RecorderAcceptsConcurrentRecords){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    constexpr u32 s_ThreadCount = 4u;
    constexpr u32 s_EventsPerThread = 64u;
    Thread threads[s_ThreadCount];
    for(u32 threadIndex = 0u; threadIndex < s_ThreadCount; ++threadIndex){
        threads[threadIndex] = Thread([&recorder, threadIndex](){
            for(u32 eventIndex = 0u; eventIndex < s_EventsPerThread; ++eventIndex){
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

    EXPECT_EQ(recorder.eventCount(), s_ThreadCount * s_EventsPerThread);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

