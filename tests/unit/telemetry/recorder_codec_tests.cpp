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

    Telemetry::EventRecord decoded(testArena.arena);
    Telemetry::DecodeResult result = Telemetry::DecodeEvent(testArena.arena, encoded.data(), encoded.size(), decoded);
    EXPECT_EQ(result.status, Telemetry::DecodeStatus::InvalidHeader);

    result = Telemetry::DecodeEvent(testArena.arena, encoded.data(), sizeof(Telemetry::EncodedEventHeader) - 1u, decoded);
    EXPECT_EQ(result.status, Telemetry::DecodeStatus::TruncatedHeader);
}

TEST(Telemetry, EventCodecReportsTruncatedPayload){
    TestArena testArena;
    Telemetry::TelemetryBytes encoded(testArena.arena);

    const u8 payload[] = { 1u, s_ExpectedDualCount, 3u };
    Telemetry::EventHeader header;
    header.kind = Telemetry::EventKind::PerfFrame;
    header.payloadBytes = sizeof(payload);
    EXPECT_TRUE(Telemetry::EncodeEvent(header, payload, sizeof(payload), encoded));

    Telemetry::EventRecord decoded(testArena.arena);
    const Telemetry::DecodeResult result = Telemetry::DecodeEvent(testArena.arena, encoded.data(), encoded.size() - 1u, decoded);
    EXPECT_EQ(result.status, Telemetry::DecodeStatus::TruncatedPayload);
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
    GLB_MEMCPY(&streamHeader, sizeof(streamHeader), corrupted.data(), sizeof(streamHeader));
    streamHeader.eventCount = 0u;
    GLB_MEMCPY(corrupted.data(), corrupted.size(), &streamHeader, sizeof(streamHeader));
    result = Telemetry::DecodeEventStream(testArena.arena, corrupted.data(), corrupted.size(), decoded);
    EXPECT_EQ(result.status, Telemetry::DecodeStatus::InvalidHeader);
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
    Telemetry::EventRecord decoded(testArena.arena);
    Telemetry::Recorder recorder(testArena.arena);
    for(const u16 version : unsupportedVersions){
        SCOPED_TRACE(version);
        header.version = version;
        EXPECT_FALSE(Telemetry::EncodeEvent(header, payload, sizeof(payload), encoded));
        EXPECT_EQ(encoded, current);
        EXPECT_FALSE(recorder.append(header, payload, sizeof(payload)));
        EXPECT_EQ(recorder.eventCount(), 0u);

        Telemetry::EncodedEventHeader encodedHeader;
        usize cursor = 0u;
        ASSERT_TRUE(ReadPOD(current, cursor, encodedHeader));
        encodedHeader.version = version;
        GLB_MEMCPY(encoded.data(), encoded.size(), &encodedHeader, sizeof(encodedHeader));
        EXPECT_EQ(
            Telemetry::DecodeEvent(testArena.arena, encoded.data(), encoded.size(), decoded).status,
            Telemetry::DecodeStatus::InvalidHeader
        );
        EXPECT_TRUE(decoded.payload.empty());
        encoded = current;
        ASSERT_TRUE(Telemetry::DecodeEvent(testArena.arena, current.data(), current.size(), decoded).ok());
        ASSERT_EQ(decoded.payload.size(), sizeof(payload));
        EXPECT_EQ(decoded.payload[0u], payload[0u]);
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
        Telemetry::EncodedStreamHeader streamHeader;
        usize cursor = 0u;
        ASSERT_TRUE(ReadPOD(current, cursor, streamHeader));
        streamHeader.version = version;
        GLB_MEMCPY(encoded.data(), encoded.size(), &streamHeader, sizeof(streamHeader));
        EXPECT_EQ(
            Telemetry::DecodeEventStream(testArena.arena, encoded.data(), encoded.size(), decoded).status,
            Telemetry::DecodeStatus::InvalidHeader
        );
        EXPECT_EQ(decoded.eventCount(), 0u);

        encoded = current;
        Telemetry::EncodedEventHeader eventHeader;
        ASSERT_TRUE(ReadPOD(current, cursor, eventHeader));
        eventHeader.version = version;
        GLB_MEMCPY(
            encoded.data() + sizeof(streamHeader), encoded.size() - sizeof(streamHeader), &eventHeader, sizeof(eventHeader)
        );
        EXPECT_EQ(
            Telemetry::DecodeEventStream(testArena.arena, encoded.data(), encoded.size(), decoded).status,
            Telemetry::DecodeStatus::InvalidHeader
        );
        EXPECT_EQ(decoded.eventCount(), 0u);
        encoded = current;
        ASSERT_TRUE(Telemetry::DecodeEventStream(testArena.arena, current.data(), current.size(), decoded).ok());
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
    Telemetry::DiagnosticPayload parsed(testArena.arena);
    for(const u16 version : unsupportedVersions){
        SCOPED_TRACE(version);
        ASSERT_TRUE(Telemetry::ParseDiagnosticPayload(testArena.arena, current.data(), current.size(), parsed));
        Telemetry::EncodedDiagnosticPayloadHeader header;
        usize cursor = 0u;
        ASSERT_TRUE(ReadPOD(current, cursor, header));
        header.version = version;
        GLB_MEMCPY(encoded.data(), encoded.size(), &header, sizeof(header));
        EXPECT_FALSE(Telemetry::ParseDiagnosticPayload(testArena.arena, encoded.data(), encoded.size(), parsed));
        EXPECT_TRUE(parsed.message.empty());
        ASSERT_TRUE(Telemetry::ParseDiagnosticPayload(testArena.arena, current.data(), current.size(), parsed));
        EXPECT_EQ(AStringView(parsed.message), record.message);
    }
}

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
                    GLB_TEXT("concurrent telemetry record"),
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

