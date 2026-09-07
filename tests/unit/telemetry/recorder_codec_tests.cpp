// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "telemetry_test_helpers.h"
#include <gtest/gtest.h>
#include <global/thread.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_recorder_codec_tests{


using namespace TelemetryTestDetail;



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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

