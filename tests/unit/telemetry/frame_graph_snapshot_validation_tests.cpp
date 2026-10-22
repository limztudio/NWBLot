// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "telemetry_test_helpers.h"

#include <tests/common/frame_graph_snapshot_validation.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_frame_graph_snapshot_validation_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TelemetryTestDetail;

struct Fixture{
    TestArena arena;
    Telemetry::FrameGraphPayload payload{ arena.arena };
    Telemetry::FrameGraphRuntimeStatistics runtime;


public:
    Fixture(){
        runtime.planGeneration = 9u;
        runtime.compile.taskCount = 4u;
        runtime.compile.packetCount = 3u;
        runtime.submission.acceptedTaskCount = 3u;
        runtime.submission.acceptedPacketCount = 2u;
        runtime.submission.rejectedTaskCount = 1u;
        runtime.submission.rejectedPacketCount = 1u;
        const Array<Name, 4u> names{ Name("render.first"), Name("render.second"), Name("render.present"), Name("render.frame_recovery") };
        const Array<u32, 4u> packets{ 0u, 0u, 1u, 2u };
        for(usize index = 0u; index < names.size(); ++index){
            auto& node = payload.nodes.emplace_back();
            node.name = names[index];
            node.compiledTask = { .planGeneration = runtime.planGeneration, .packetIndex = packets[index], .present = true };
        }
        for(u32 index = 0u; index < 2u; ++index){
            auto& packet = payload.packetSubmissionStatistics.emplace_back();
            packet.packetGeneration = runtime.planGeneration;
            packet.packetIndex = index;
            packet.taskCount = index == 0u ? 2u : 1u;
            packet.queue = { .index = 0u, .deviceGeneration = 1u };
        }
    }


public:
    [[nodiscard]] bool valid()const noexcept{ return NWB::Tests::Smoke::ValidateNormalFrameSnapshotCompletion(payload, runtime); }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(FrameGraphSnapshotValidation, AcceptsOnlyTheUnusedIndependentRecoveryPacket){
    Fixture fixture;
    EXPECT_TRUE(fixture.valid());
}

TEST(FrameGraphSnapshotValidation, AcceptsFullyAcceptedNormalGraphsWithoutRecovery){
    Fixture fixture;
    fixture.payload.nodes.pop_back();
    --fixture.runtime.compile.taskCount;
    --fixture.runtime.compile.packetCount;
    fixture.runtime.submission.rejectedTaskCount = 0u;
    fixture.runtime.submission.rejectedPacketCount = 0u;
    EXPECT_TRUE(fixture.valid());
}

TEST(FrameGraphSnapshotValidation, FullyAcceptedCountsCannotHideMissingTasksOrPackets){
    Fixture fixture;
    fixture.payload.nodes.pop_back();
    --fixture.runtime.compile.taskCount;
    --fixture.runtime.compile.packetCount;
    fixture.runtime.submission.rejectedTaskCount = 0u;
    fixture.runtime.submission.rejectedPacketCount = 0u;
    fixture.payload.nodes[2u].compiledTask.present = false;
    EXPECT_FALSE(fixture.valid());
    fixture.payload.nodes[2u].compiledTask.present = true;
    fixture.payload.packetSubmissionStatistics.pop_back();
    EXPECT_FALSE(fixture.valid());
}

TEST(FrameGraphSnapshotValidation, FullyAcceptedCountsStillRejectExecutedRecovery){
    Fixture fixture;
    fixture.runtime.submission.acceptedTaskCount = fixture.runtime.compile.taskCount;
    fixture.runtime.submission.acceptedPacketCount = fixture.runtime.compile.packetCount;
    fixture.runtime.submission.rejectedTaskCount = 0u;
    fixture.runtime.submission.rejectedPacketCount = 0u;
    fixture.runtime.submission.recoverySubmissionCount = 1u;
    auto& recovery = fixture.payload.packetSubmissionStatistics.emplace_back();
    recovery.packetGeneration = fixture.runtime.planGeneration;
    recovery.packetIndex = 2u;
    recovery.taskCount = 1u;
    recovery.queue = { .index = 0u, .deviceGeneration = 1u };
    recovery.recoverySubmission = true;
    EXPECT_FALSE(fixture.valid());
    fixture.runtime.submission.recoverySubmissionCount = 0u;
    EXPECT_FALSE(fixture.valid());
    recovery.recoverySubmission = false;
    EXPECT_FALSE(fixture.valid());
}

TEST(FrameGraphSnapshotValidation, RejectsNativeFailureOrExecutedRecovery){
    Fixture fixture;
    fixture.runtime.submission.rejectedSubmissionCount = 1u;
    EXPECT_FALSE(fixture.valid());
    fixture.runtime.submission.rejectedSubmissionCount = 0u;
    fixture.runtime.submission.recoverySubmissionCount = 1u;
    EXPECT_FALSE(fixture.valid());
}

TEST(FrameGraphSnapshotValidation, RejectsAMissingNormalTaskOrAcceptedRecoveryTask){
    Fixture fixture;
    fixture.payload.nodes[3u].name = Name("render.normal_work");
    EXPECT_FALSE(fixture.valid());
    fixture.payload.nodes[3u].name = Name("render.frame_recovery");
    fixture.payload.packetSubmissionStatistics[1u].packetIndex = 2u;
    EXPECT_FALSE(fixture.valid());
}

TEST(FrameGraphSnapshotValidation, RejectsRecoverySharingItsUnsubmittedPacket){
    Fixture fixture;
    fixture.payload.nodes[2u].compiledTask.packetIndex = 2u;
    EXPECT_FALSE(fixture.valid());
}

TEST(FrameGraphSnapshotValidation, RejectsAdditionalDiscardsOrIncompleteAcceptanceCounts){
    Fixture fixture;
    fixture.runtime.submission.rejectedPacketCount = 2u;
    EXPECT_FALSE(fixture.valid());
    fixture.runtime.submission.rejectedPacketCount = 1u;
    fixture.runtime.submission.rejectedTaskCount = 2u;
    EXPECT_FALSE(fixture.valid());
    fixture.runtime.submission.rejectedTaskCount = 1u;
    --fixture.runtime.submission.acceptedTaskCount;
    EXPECT_FALSE(fixture.valid());
}

TEST(FrameGraphSnapshotValidation, RejectsDuplicatePacketRowsAndMismatchedTaskCounts){
    Fixture fixture;
    fixture.payload.packetSubmissionStatistics[1u].packetIndex = 0u;
    EXPECT_FALSE(fixture.valid());
    fixture.payload.packetSubmissionStatistics[1u].packetIndex = 1u;
    fixture.payload.packetSubmissionStatistics[0u].taskCount = 1u;
    EXPECT_FALSE(fixture.valid());
}

TEST(FrameGraphSnapshotValidation, RejectsGenerationMismatchAndMissingCompiledTask){
    Fixture fixture;
    ++fixture.payload.nodes[0u].compiledTask.planGeneration;
    EXPECT_FALSE(fixture.valid());
    --fixture.payload.nodes[0u].compiledTask.planGeneration;
    fixture.payload.nodes[0u].compiledTask.present = false;
    EXPECT_FALSE(fixture.valid());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

