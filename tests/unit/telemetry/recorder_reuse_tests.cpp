// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "telemetry_test_helpers.h"

#include <global/thread.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_recorder_reuse_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TelemetryTestDetail;

struct PayloadBuildFailure{};

[[nodiscard]] bool RecordFilledPayload(Telemetry::Recorder& recorder, const usize byteCount, const u8 value){
    return recorder.recordBuiltPayload(
        Telemetry::EventKind::PerfFrame,
        1u,
        0u,
        [byteCount, value](Telemetry::TelemetryArena&, Telemetry::TelemetryBytes& payload){
            EXPECT_TRUE(payload.empty());
            payload.resize(byteCount, value);
            return true;
        }
    );
}

void RunConcurrentReset(const bool disableCapture){
    constexpr usize s_WorkerCount = 4u;
    TestArena testArena;
    const ArenaMemoryStats initial = testArena.arena.memoryStats();
    {
        Telemetry::Recorder recorder(testArena.arena);
        recorder.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());
        Atomic<usize> accepted{ 0u };
        Atomic<usize> damagedPayloads{ 0u };
        Latch payloadsReady(s_WorkerCount);
        Latch releasePayloads(1u);
        Thread workers[s_WorkerCount];
        for(usize index = 0u; index < s_WorkerCount; ++index){
            workers[index] = Thread([&, index](){
                bool enteredBuilder = false;
                const bool recorded = recorder.recordBuiltPayload(
                    Telemetry::EventKind::PerfFrame,
                    42u,
                    static_cast<u32>(index),
                    [&](Telemetry::TelemetryArena&, Telemetry::TelemetryBytes& payload){
                        enteredBuilder = true;
                        payload.resize(256u, static_cast<u8>(index + 1u));
                        payloadsReady.count_down();
                        releasePayloads.wait();
                        for(const u8 value : payload){
                            if(value != index + 1u){
                                damagedPayloads.fetch_add(1u, MemoryOrder::relaxed);
                                break;
                            }
                        }
                        return true;
                    }
                );
                if(!enteredBuilder)
                    payloadsReady.count_down();
                if(recorded)
                    accepted.fetch_add(1u, MemoryOrder::relaxed);
            });
        }
        payloadsReady.wait();
        recorder.clear();
        const u8 marker = 99u;
        const bool markerRecorded = recorder.recordBinary(Telemetry::EventKind::PerfFrame, 7u, &marker, sizeof(marker), 99u);
        if(disableCapture)
            recorder.setCaptureOptions(Telemetry::CaptureOptions::Disabled());
        releasePayloads.count_down();
        for(Thread& worker : workers)
            worker.join();

        EXPECT_TRUE(markerRecorded);
        EXPECT_EQ(damagedPayloads.load(MemoryOrder::relaxed), 0u);
        EXPECT_EQ(accepted.load(MemoryOrder::relaxed), disableCapture ? 0u : s_WorkerCount);
        EXPECT_EQ(recorder.eventCount(), disableCapture ? 0u : s_WorkerCount + 1u);
        if(!disableCapture){
            bool seen[s_WorkerCount] = {};
            for(usize index = 1u; index < recorder.eventCount(); ++index){
                const auto* event = recorder.view().eventAt(index);
                ASSERT_NE(event, nullptr);
                ASSERT_LT(event->header.streamId, s_WorkerCount);
                EXPECT_FALSE(seen[event->header.streamId]);
                seen[event->header.streamId] = true;
                ASSERT_EQ(event->payload.size(), 256u);
                EXPECT_EQ(event->payload.front(), event->header.streamId + 1u);
                EXPECT_EQ(event->payload.back(), event->header.streamId + 1u);
            }
            for(const bool recorded : seen)
                EXPECT_TRUE(recorded);
        }
    }
    const ArenaMemoryStats after = testArena.arena.memoryStats();
    EXPECT_EQ(after.usedBytes, initial.usedBytes);
    EXPECT_EQ(after.reservedBytes, initial.reservedBytes);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Telemetry, RecorderReusesSlotsAndPayloadCapacityUntilDisabled){
    TestArena testArena;
    const ArenaMemoryStats initial = testArena.arena.memoryStats();
    {
        Telemetry::Recorder recorder(testArena.arena);
        recorder.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());
        ASSERT_TRUE(RecordFilledPayload(recorder, 256u, 11u));
        const u8* const firstPayload = recorder.view().eventAt(0u)->payload.data();
        recorder.clear();
        const ArenaMemoryStats warmed = testArena.arena.memoryStats();
        for(u32 frame = 0u; frame < 64u; ++frame){
            ASSERT_TRUE(RecordFilledPayload(recorder, 64u, static_cast<u8>(frame)));
            const auto* event = recorder.view().eventAt(0u);
            ASSERT_NE(event, nullptr);
            ASSERT_EQ(event->payload.size(), 64u);
            EXPECT_EQ(event->payload.data(), firstPayload);
            for(const u8 value : event->payload)
                EXPECT_EQ(value, frame);
            recorder.clear();
        }
        const ArenaMemoryStats reused = testArena.arena.memoryStats();
        EXPECT_EQ(reused.allocationCount, warmed.allocationCount);
        EXPECT_EQ(reused.reallocationCount, warmed.reallocationCount);
        EXPECT_EQ(reused.deallocationCount, warmed.deallocationCount);
        EXPECT_EQ(reused.usedBytes, warmed.usedBytes);

        ASSERT_TRUE(RecordFilledPayload(recorder, 0u, 0u));
        EXPECT_TRUE(recorder.view().eventAt(0u)->payload.empty());
        recorder.clear();
        recorder.setCaptureOptions(Telemetry::CaptureOptions::Disabled());
        EXPECT_LT(testArena.arena.memoryStats().usedBytes, warmed.usedBytes);
    }
    const ArenaMemoryStats after = testArena.arena.memoryStats();
    EXPECT_EQ(after.usedBytes, initial.usedBytes);
    EXPECT_EQ(after.reservedBytes, initial.reservedBytes);
}

TEST(Telemetry, RecorderBuilderCanClearAndReenterWhileItsPayloadIsLeased){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());
    ASSERT_TRUE(RecordFilledPayload(recorder, 256u, 3u));
    ASSERT_TRUE(recorder.recordBuiltPayload(
        Telemetry::EventKind::PerfFrame,
        2u,
        7u,
        [&](Telemetry::TelemetryArena& arena, Telemetry::TelemetryBytes& payload){
            EXPECT_EQ(&arena, &testArena.arena);
            EXPECT_TRUE(payload.empty());
            payload.resize(128u, 42u);
            const u8* const leasedData = payload.data();
            recorder.clear();
            EXPECT_TRUE(recorder.captureOptions().perfEnabled());
            EXPECT_EQ(recorder.eventCount(), 0u);
            const bool nested = recorder.recordBinary(Telemetry::EventKind::PerfFrame, 1u, payload.data(), payload.size(), 5u);
            EXPECT_TRUE(nested);
            EXPECT_EQ(payload.data(), leasedData);
            for(const u8 value : payload)
                EXPECT_EQ(value, 42u);
            return nested;
        }
    ));
    ASSERT_EQ(recorder.eventCount(), 2u);
    const auto* nested = recorder.view().eventAt(0u);
    const auto* outer = recorder.view().eventAt(1u);
    ASSERT_NE(nested, nullptr);
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(nested->header.streamId, 5u);
    EXPECT_EQ(outer->header.streamId, 7u);
    EXPECT_NE(nested->payload.data(), outer->payload.data());
    ASSERT_EQ(nested->payload.size(), 128u);
    ASSERT_EQ(outer->payload.size(), 128u);
    EXPECT_EQ(NWB_MEMCMP(nested->payload.data(), outer->payload.data(), 128u), 0);
}

TEST(Telemetry, RecorderDisableDuringBuildRejectsPublicationAndReleasesLease){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());
    const ArenaMemoryStats before = testArena.arena.memoryStats();
    EXPECT_FALSE(recorder.recordBuiltPayload(
        Telemetry::EventKind::PerfFrame,
        1u,
        0u,
        [&](Telemetry::TelemetryArena&, Telemetry::TelemetryBytes& payload){
            payload.resize(256u, 19u);
            recorder.setCaptureOptions(Telemetry::CaptureOptions::Disabled());
            EXPECT_EQ(payload.front(), 19u);
            payload.back() = 37u;
            EXPECT_EQ(payload.back(), 37u);
            return true;
        }
    ));
    EXPECT_EQ(recorder.eventCount(), 0u);
    const ArenaMemoryStats disabled = testArena.arena.memoryStats();
    EXPECT_EQ(disabled.usedBytes, before.usedBytes);
    bool builderCalled = false;
    EXPECT_FALSE(recorder.recordBuiltPayload(
        Telemetry::EventKind::PerfFrame,
        2u,
        0u,
        [&](Telemetry::TelemetryArena&, Telemetry::TelemetryBytes&){ builderCalled = true; return true; }
    ));
    EXPECT_FALSE(builderCalled);
    EXPECT_EQ(testArena.arena.memoryStats().allocationCount, disabled.allocationCount);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());
    EXPECT_TRUE(RecordFilledPayload(recorder, 64u, 23u));
}

TEST(Telemetry, RecorderPublicationUsesCaptureOptionsAtBuilderCompletion){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());
    ASSERT_TRUE(recorder.recordBuiltPayload(
        Telemetry::EventKind::PerfFrame,
        1u,
        0u,
        [&](Telemetry::TelemetryArena&, Telemetry::TelemetryBytes& payload){
            payload.resize(64u, 71u);
            recorder.setCaptureOptions(Telemetry::CaptureOptions::Disabled());
            recorder.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());
            return true;
        }
    ));
    ASSERT_EQ(recorder.eventCount(), 1u);
    EXPECT_EQ(recorder.view().eventAt(0u)->payload.back(), 71u);
    EXPECT_FALSE(recorder.recordBuiltPayload(
        Telemetry::EventKind::PerfFrame,
        2u,
        0u,
        [&](Telemetry::TelemetryArena&, Telemetry::TelemetryBytes& payload){
            payload.resize(64u, 29u);
            recorder.setCaptureOptions(Telemetry::CaptureOptions::FrameGraphOnly());
            return true;
        }
    ));
    EXPECT_EQ(recorder.eventCount(), 1u);
}

TEST(Telemetry, RecorderFailedAndThrowingBuildersRecycleWithoutAllocations){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());
    ASSERT_TRUE(RecordFilledPayload(recorder, 256u, 13u));
    recorder.clear();
    const ArenaMemoryStats warmed = testArena.arena.memoryStats();
    EXPECT_FALSE(recorder.recordBuiltPayload(
        Telemetry::EventKind::PerfFrame,
        1u,
        0u,
        [](Telemetry::TelemetryArena&, Telemetry::TelemetryBytes& payload){ payload.resize(128u, 31u); return false; }
    ));
    EXPECT_THROW(
        EXPECT_TRUE(recorder.recordBuiltPayload(
            Telemetry::EventKind::PerfFrame,
            2u,
            0u,
            [](Telemetry::TelemetryArena&, Telemetry::TelemetryBytes& payload)->bool{
                payload.resize(128u, 47u);
                throw PayloadBuildFailure{};
            }
        )),
        PayloadBuildFailure
    );
    EXPECT_EQ(recorder.eventCount(), 0u);
    ASSERT_TRUE(RecordFilledPayload(recorder, 64u, 59u));
    recorder.clear();
    const ArenaMemoryStats after = testArena.arena.memoryStats();
    EXPECT_EQ(after.allocationCount, warmed.allocationCount);
    EXPECT_EQ(after.reallocationCount, warmed.reallocationCount);
    EXPECT_EQ(after.deallocationCount, warmed.deallocationCount);
    EXPECT_EQ(after.usedBytes, warmed.usedBytes);
}

TEST(Telemetry, RecorderActiveEventAliasesRemainStableAcrossStorageGrowth){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());
    const u8 bytes[] = { 3u, 7u, 11u, 19u };
    ASSERT_TRUE(recorder.recordBinary(Telemetry::EventKind::PerfFrame, 5u, bytes, sizeof(bytes)));
    const auto* source = recorder.view().eventAt(0u);
    ASSERT_NE(source, nullptr);
    const u8* const sourceData = source->payload.data();
    for(u32 index = 0u; index < 128u; ++index){
        ASSERT_TRUE(recorder.append(source->header, source->payload.data(), source->payload.size()));
        ASSERT_TRUE(recorder.recordBinary(Telemetry::EventKind::PerfFrame, index, sourceData, sizeof(bytes)));
        Telemetry::TelemetryBytes ownedPayload(testArena.arena);
        ownedPayload.assign(source->payload.begin(), source->payload.end());
        const u8* const movedData = ownedPayload.data();
        ASSERT_TRUE(recorder.append(source->header, Move(ownedPayload)));
        EXPECT_EQ(recorder.view().eventAt(recorder.eventCount() - 1u)->payload.data(), movedData);
        EXPECT_TRUE(ownedPayload.empty());
        EXPECT_EQ(recorder.view().eventAt(0u), source);
        EXPECT_EQ(source->payload.data(), sourceData);
    }
    ASSERT_EQ(recorder.eventCount(), 385u);
    for(usize index = 0u; index < recorder.eventCount(); ++index){
        const auto* event = recorder.view().eventAt(index);
        ASSERT_NE(event, nullptr);
        ASSERT_EQ(event->payload.size(), sizeof(bytes));
        EXPECT_EQ(NWB_MEMCMP(event->payload.data(), bytes, sizeof(bytes)), 0);
    }
}

TEST(Telemetry, RecorderReusedSlotsKeepForeignPayloadsIndependentAndMoveOwnedPayloads){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());
    ASSERT_TRUE(RecordFilledPayload(recorder, 256u, 17u));
    recorder.clear();
    {
        TestArena foreignArena;
        Telemetry::TelemetryBytes foreignPayload(foreignArena.arena);
        foreignPayload.resize(64u, 41u);
        const u8* const sourceData = foreignPayload.data();
        ASSERT_TRUE(recorder.recordPayload(Telemetry::EventKind::PerfFrame, 1u, Move(foreignPayload)));
        const auto* event = recorder.view().eventAt(0u);
        ASSERT_NE(event, nullptr);
        EXPECT_NE(event->payload.data(), sourceData);
        EXPECT_EQ(foreignPayload.data(), sourceData);
        ASSERT_EQ(foreignPayload.size(), 64u);
        foreignPayload.front() = 99u;
        EXPECT_EQ(event->payload.front(), 41u);
    }
    EXPECT_EQ(recorder.view().eventAt(0u)->payload.back(), 41u);
    recorder.clear();
    Telemetry::TelemetryBytes ownedPayload(testArena.arena);
    ownedPayload.resize(128u, 67u);
    const u8* const ownedData = ownedPayload.data();
    ASSERT_TRUE(recorder.recordPayload(Telemetry::EventKind::PerfFrame, 2u, Move(ownedPayload)));
    EXPECT_EQ(recorder.view().eventAt(0u)->payload.data(), ownedData);
    EXPECT_TRUE(ownedPayload.empty());
    recorder.clear();
    ASSERT_TRUE(recorder.recordBinary(Telemetry::EventKind::PerfFrame, 3u, nullptr, 0u));
    EXPECT_TRUE(recorder.view().eventAt(0u)->payload.empty());
}

TEST(Telemetry, RecorderRejectsInvalidInputsWithoutConsumingPayloadsOrReusedSlots){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());
    ASSERT_TRUE(RecordFilledPayload(recorder, 256u, 5u));
    recorder.clear();
    Telemetry::TelemetryBytes payload(testArena.arena);
    payload.resize(64u, 17u);
    const u8* const payloadData = payload.data();
    const ArenaMemoryStats before = testArena.arena.memoryStats();
    const Telemetry::EventKind::Enum invalidKinds[] = {
        Telemetry::EventKind::Unknown,
        static_cast<Telemetry::EventKind::Enum>(0xffffu),
    };
    bool builderCalled = false;
    for(const auto kind : invalidKinds){
        EXPECT_FALSE(recorder.recordBuiltPayload(
            kind,
            1u,
            0u,
            [&](Telemetry::TelemetryArena&, Telemetry::TelemetryBytes&){ builderCalled = true; return true; }
        ));
        Telemetry::EventHeader header;
        header.kind = kind;
        header.payloadBytes = payload.size();
        EXPECT_FALSE(recorder.append(header, Move(payload)));
        EXPECT_FALSE(recorder.append(header, payload.data(), payload.size()));
    }
    Telemetry::EventHeader header;
    header.kind = Telemetry::EventKind::PerfFrame;
    header.payloadBytes = payload.size();
    header.magic = 0u;
    EXPECT_FALSE(recorder.append(header, Move(payload)));
    header.magic = Telemetry::s_EventMagic;
    EXPECT_FALSE(recorder.append(header, nullptr, payload.size()));
    ++header.payloadBytes;
    EXPECT_FALSE(recorder.append(header, Move(payload)));
    EXPECT_FALSE(builderCalled);
    EXPECT_EQ(recorder.eventCount(), 0u);
    EXPECT_EQ(payload.data(), payloadData);
    EXPECT_EQ(payload.size(), 64u);
    EXPECT_EQ(testArena.arena.memoryStats().allocationCount, before.allocationCount);
    ASSERT_TRUE(RecordFilledPayload(recorder, 64u, 23u));
    EXPECT_EQ(testArena.arena.memoryStats().allocationCount, before.allocationCount);
}

TEST(Telemetry, RecorderBuilderForeignMovesKeepOwnershipWithinEachArena){
    TestArena testArena;
    const ArenaMemoryStats initial = testArena.arena.memoryStats();
    {
        Telemetry::Recorder recorder(testArena.arena);
        recorder.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());
        ASSERT_TRUE(RecordFilledPayload(recorder, 256u, 7u));
        recorder.clear();
        {
            TestArena foreignArena;
            const ArenaMemoryStats foreignInitial = foreignArena.arena.memoryStats();
            {
                Telemetry::TelemetryBytes foreignPayload(foreignArena.arena);
                foreignPayload.resize(64u, 83u);
                ASSERT_TRUE(recorder.recordBuiltPayload(
                    Telemetry::EventKind::PerfFrame,
                    3u,
                    0u,
                    [&](Telemetry::TelemetryArena&, Telemetry::TelemetryBytes& payload){
                        payload = Move(foreignPayload);
                        return true;
                    }
                ));
            }
            EXPECT_EQ(foreignArena.arena.memoryStats().usedBytes, foreignInitial.usedBytes);
        }
        ASSERT_EQ(recorder.eventCount(), 1u);
        const auto* event = recorder.view().eventAt(0u);
        ASSERT_NE(event, nullptr);
        EXPECT_EQ(event->payload.get_allocator().arenaPtr(), &testArena.arena);
        ASSERT_EQ(event->payload.size(), 64u);
        EXPECT_EQ(event->payload.front(), 83u);
        EXPECT_EQ(event->payload.back(), 83u);
        recorder.clear();
        ASSERT_TRUE(RecordFilledPayload(recorder, 64u, 29u));
    }
    EXPECT_EQ(testArena.arena.memoryStats().usedBytes, initial.usedBytes);
}

TEST(Telemetry, RecorderExplicitForeignBuilderStorageIsCopiedOrReleasedOnEveryExit){
    using Payload = Telemetry::TelemetryBytes;
    TestArena testArena;
    const ArenaMemoryStats initial = testArena.arena.memoryStats();
    {
        Telemetry::Recorder recorder(testArena.arena);
        recorder.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());
        for(u32 outcome = 0u; outcome < 4u; ++outcome){
            recorder.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());
            recorder.clear();
            {
                TestArena foreignArena;
                const ArenaMemoryStats foreignInitial = foreignArena.arena.memoryStats();
                {
                    Payload foreignPayload(foreignArena.arena);
                    foreignPayload.resize(64u, 37u);
                    const auto buildPayload = [&](Telemetry::TelemetryArena&, Payload& payload)->bool{
                        // Explicit move construction replaces allocator identity, independently of assignment policy.
                        payload.~Payload();
                        new(&payload) Payload(Move(foreignPayload));
                        EXPECT_EQ(payload.get_allocator().arenaPtr(), &foreignArena.arena);
                        if(outcome == 2u)
                            throw PayloadBuildFailure{};
                        if(outcome == 3u)
                            recorder.setCaptureOptions(Telemetry::CaptureOptions::Disabled());
                        return outcome != 1u;
                    };
                    if(outcome == 2u){
                        EXPECT_THROW(
                            EXPECT_TRUE(recorder.recordBuiltPayload(Telemetry::EventKind::PerfFrame, 1u, 0u, buildPayload)),
                            PayloadBuildFailure
                        );
                    }
                    else{
                        EXPECT_EQ(
                            recorder.recordBuiltPayload(Telemetry::EventKind::PerfFrame, 1u, 0u, buildPayload),
                            outcome == 0u
                        );
                    }
                }
                EXPECT_EQ(foreignArena.arena.memoryStats().usedBytes, foreignInitial.usedBytes);
            }
            ASSERT_EQ(recorder.eventCount(), outcome == 0u ? 1u : 0u);
            if(outcome == 0u){
                const auto* event = recorder.view().eventAt(0u);
                ASSERT_NE(event, nullptr);
                EXPECT_EQ(event->payload.get_allocator().arenaPtr(), &testArena.arena);
                ASSERT_EQ(event->payload.size(), 64u);
                EXPECT_EQ(event->payload.front(), 37u);
                EXPECT_EQ(event->payload.back(), 37u);
            }
        }
        recorder.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());
        ASSERT_TRUE(RecordFilledPayload(recorder, 64u, 59u));
    }
    EXPECT_EQ(testArena.arena.memoryStats().usedBytes, initial.usedBytes);
}

TEST(Telemetry, RecorderConcurrentClearPreservesExclusivelyLeasedPayloads){
    RunConcurrentReset(false);
}

TEST(Telemetry, RecorderConcurrentDisableWaitsForLeaseReleaseWithoutPublishing){
    RunConcurrentReset(true);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

