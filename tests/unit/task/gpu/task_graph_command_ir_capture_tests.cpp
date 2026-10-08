// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"
#include "task_graph_command_ir_test_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_command_ir_capture_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuCommandIrCapture, RejectsForeignGraphAndPlanGenerationsWithoutChangingCapturedBytes){
    TestArena testArena;
    Graphics::GpuCommandIrCapture capture(testArena.arena);
    const Graphics::GpuTaskId task{ .generation = 17u, .index = 4u };
    const Graphics::GpuSubmissionPacketId packet{ .generation = 23u, .index = s_ExpectedDualCount };
    const Graphics::GpuPhysicalQueueId queue{ .index = 1u, .deviceGeneration = 3u };
    const Graphics::GpuGraphResourceId source{ .generation = 17u, .index = 5u };
    const Graphics::GpuGraphResourceId destination{ .generation = 17u, .index = 6u };

    ASSERT_TRUE(capture.captureCopyBuffer(
        task,
        packet,
        queue,
        source,
        16u,
        destination,
        32u,
        64u
    ));
    Graphics::GpuClearTextureTaskDesc clearTexture;
    clearTexture.destination = destination;
    clearTexture.subresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u);
    clearTexture.valueType = Graphics::GpuClearTextureTaskValueType::Float;
    clearTexture.floatValue = Graphics::Color(0.25f, 0.5f, 0.75f, 1.f);
    // The stream omits aspect flags ignored by native color-clear lowering.
    clearTexture.clearDepth = true;
    clearTexture.clearStencil = true;
    ASSERT_TRUE(capture.captureClearTexture(task, packet, queue, destination, clearTexture));

    const BinaryByteView bytesBeforeRejectedRecord = capture.commandBytes();
    Graphics::GraphicsBytes streamBeforeRejectedRecord(testArena.arena);
    streamBeforeRejectedRecord.resize(bytesBeforeRejectedRecord.size());
    NWB_MEMCPY(
        streamBeforeRejectedRecord.data(),
        streamBeforeRejectedRecord.size(),
        bytesBeforeRejectedRecord.data(),
        streamBeforeRejectedRecord.size()
    );
    const Graphics::GpuTaskId anotherGraphTask{ .generation = task.generation + 1u, .index = static_cast<u32>(task.index) };
    const Graphics::GpuSubmissionPacketId anotherGraphPacket{ .generation = packet.generation + 1u, .index = static_cast<u32>(packet.index) };
    const Graphics::GpuGraphResourceId anotherGraphResource{ .generation = destination.generation + 1u, .index = static_cast<u32>(destination.index) };
    EXPECT_FALSE(capture.captureClearBuffer(
        anotherGraphTask,
        anotherGraphPacket,
        queue,
        anotherGraphResource,
        0xdecafbadU
    ));
    EXPECT_FALSE(capture.captureClearBuffer(
        task,
        Graphics::GpuSubmissionPacketId{ .generation = packet.generation + s_ExpectedDualCount, .index = packet.index },
        queue,
        destination,
        0xdecafbadU
    ));
    const BinaryByteView bytesAfterRejectedRecord = capture.commandBytes();
    EXPECT_EQ(bytesAfterRejectedRecord.size(), streamBeforeRejectedRecord.size());
    EXPECT_EQ(
        NWB_MEMCMP(
            bytesAfterRejectedRecord.data(),
            streamBeforeRejectedRecord.data(),
            streamBeforeRejectedRecord.size()
        ),
        0
    );

}

TEST(GpuCommandIrCapture, RejectsNonEmptyCaptureFromDifferentRecordingAttempt){
    TestArena testArena;
    Graphics::GpuCommandIrCapture capture(testArena.arena);
    const Graphics::GpuTaskId task{ .generation = 17u, .index = 4u };
    const Graphics::GpuSubmissionPacketId packet{ .generation = 23u, .index = s_ExpectedDualCount };
    const Graphics::GpuPhysicalQueueId queue{ .index = 1u, .deviceGeneration = 3u };
    const Graphics::GpuGraphResourceId source{ .generation = 17u, .index = 5u };
    const Graphics::GpuGraphResourceId destination{ .generation = 17u, .index = 6u };

    ASSERT_TRUE(capture.beginRecordingAttempt(41u));
    EXPECT_EQ(capture.recordingAttemptGeneration(), 41u);
    ASSERT_TRUE(capture.captureCopyBuffer(task, packet, queue, source, 0u, destination, 0u, 4u));
    EXPECT_FALSE(capture.beginRecordingAttempt(42u));
    EXPECT_TRUE(capture.beginRecordingAttempt(41u));

    capture.rollback(0u);
    EXPECT_EQ(capture.recordingAttemptGeneration(), 0u);
    EXPECT_TRUE(capture.beginRecordingAttempt(42u));
    EXPECT_EQ(capture.recordingAttemptGeneration(), 42u);
}

TEST(GpuCommandIrCapture, RejectsDegenerateRectUIntClearAndEarlierStreamVersions){
    TestArena testArena;
    Graphics::GpuCommandIrCapture capture(testArena.arena);
    Graphics::GpuClearTextureRectUIntTaskDesc clear;
    clear.destination = s_CommandIrDestination;
    clear.subresources = Graphics::TextureSubresourceSet(s_ExpectedDualCount, 3u, 4u, 5u);
    clear.rect = Graphics::Rect(-2, 7, 3, 11);
    clear.uintValue = Graphics::UIntColor(0x10203040u, 0x50607080u, 0x90a0b0c0u, 0xd0e0f000u);
    ASSERT_TRUE(capture.captureClearTextureRectUInt(
        s_CommandIrTask,
        s_CommandIrPacket,
        s_CommandIrQueue,
        s_CommandIrDestination,
        clear
    ));

    ASSERT_EQ(capture.recordCount(), 1u);
    const BinaryByteView bytes = capture.commandBytes();
    Graphics::GpuCommandIrStreamReader reader(bytes);
    ASSERT_TRUE(reader.nextBuiltinTask());

    clear.rect = Graphics::Rect(4, 4, 0, 1);
    EXPECT_FALSE(capture.captureClearTextureRectUInt(
        s_CommandIrTask,
        s_CommandIrPacket,
        s_CommandIrQueue,
        s_CommandIrDestination,
        clear
    ));
    EXPECT_EQ(capture.recordCount(), 1u);

    Graphics::GraphicsBytes downgradedRectBytes(testArena.arena);
    CopyCommandIrBytes(downgradedRectBytes, bytes);
    WriteCommandIrPod(
        downgradedRectBytes,
        offsetof(Graphics::GpuCommandIrStreamHeader, version),
        static_cast<u16>(Graphics::s_GpuCommandIrStreamVersion - 1u)
    );
    Graphics::GpuCommandIrStreamReader downgradedRectReader(BinaryByteView{
        downgradedRectBytes.data(),
        downgradedRectBytes.size(),
    });
    const auto terminal1 = downgradedRectReader.nextBuiltinTask();
    ASSERT_FALSE(terminal1);
    EXPECT_EQ(terminal1.error(), Graphics::GpuCommandIrStreamReadStatus::Error);
    EXPECT_EQ(
        downgradedRectReader.validation().error,
        Graphics::GpuCommandIrStreamValidationError::UnsupportedVersion
    );
}

TEST(GpuCommandIrCapture, RollbackPreservesExactMixedRecordPrefixAtRecordBoundaries){
    TestArena testArena;
    Graphics::GpuCommandIrCapture capture(testArena.arena);
    const Graphics::GpuTaskId task{ .generation = 17u, .index = 4u };
    const Graphics::GpuSubmissionPacketId packet{ .generation = 17u, .index = s_ExpectedDualCount };
    const Graphics::GpuPhysicalQueueId queue{ .index = 1u, .deviceGeneration = 3u };
    const Graphics::GpuGraphResourceId source{ .generation = 17u, .index = 5u };
    const Graphics::GpuGraphResourceId destination{ .generation = 17u, .index = 6u };

    Graphics::TextureSlice sourceSlice;
    sourceSlice
        .setOrigin(1u, s_ExpectedDualCount, 3u)
        .setSize(4u, 5u, 6u)
        .setMipLevel(7u)
        .setArraySlice(8u)
    ;
    Graphics::TextureSlice destinationSlice;
    destinationSlice
        .setOrigin(9u, 10u, 11u)
        .setSize(12u, 13u, 14u)
        .setMipLevel(15u)
        .setArraySlice(16u)
    ;
    Graphics::GpuClearTextureTaskDesc clearTexture;
    clearTexture.destination = destination;
    clearTexture.subresources = Graphics::TextureSubresourceSet(s_ExpectedDualCount, 3u, 4u, 5u);
    clearTexture.valueType = Graphics::GpuClearTextureTaskValueType::DepthStencil;
    clearTexture.floatValue = Graphics::Color(0.25f, 0.5f, 0.75f, 1.f);
    clearTexture.uintValue = Graphics::UIntColor(s_ExpectedDualCount, 3u, 5u, 7u);
    clearTexture.intValue = Graphics::IntColor(-2, -3, -5, -7);
    clearTexture.depthValue = 0.125f;
    clearTexture.stencilValue = 19u;
    clearTexture.clearDepth = true;
    clearTexture.clearStencil = true;

    ASSERT_TRUE(capture.captureCopyBuffer(task, packet, queue, source, 16u, destination, 32u, 64u));
    ASSERT_TRUE(capture.captureCopyTexture(task, packet, queue, source, sourceSlice, destination, destinationSlice));
    ASSERT_TRUE(capture.captureClearBuffer(task, packet, queue, destination, 0xdecafbadU));
    ASSERT_TRUE(capture.captureClearTexture(task, packet, queue, destination, clearTexture));

    const BinaryByteView bytes = capture.commandBytes();
    const usize copyTextureEnd = sizeof(Graphics::GpuCommandIrStreamHeader)
        + sizeof(Graphics::GpuCommandIrCopyBufferRecord)
        + sizeof(Graphics::GpuCommandIrCopyTextureRecord)
    ;
    ASSERT_EQ(capture.recordCount(), 4u);

    Graphics::GraphicsBytes expectedPrefix(testArena.arena);
    expectedPrefix.resize(copyTextureEnd);
    NWB_MEMCPY(expectedPrefix.data(), expectedPrefix.size(), bytes.data(), expectedPrefix.size());
    capture.rollback(s_ExpectedDualCount);
    const BinaryByteView rolledBackBytes = capture.commandBytes();
    EXPECT_EQ(capture.recordCount(), s_ExpectedDualCount);
    EXPECT_EQ(capture.graphGeneration(), task.generation);
    EXPECT_EQ(rolledBackBytes.size(), expectedPrefix.size());
    // Rollback rewrites the stream header's count/payload fields, while the surviving two POD records remain an
    // exact byte prefix of the original capture.
    EXPECT_EQ(
        NWB_MEMCMP(
            rolledBackBytes.data() + sizeof(Graphics::GpuCommandIrStreamHeader),
            expectedPrefix.data() + sizeof(Graphics::GpuCommandIrStreamHeader),
            expectedPrefix.size() - sizeof(Graphics::GpuCommandIrStreamHeader)
        ),
        0
    );

    usize cursor = 0u;
    const auto streamHeader = ReadPOD<Graphics::GpuCommandIrStreamHeader>(rolledBackBytes, cursor);
    ASSERT_TRUE(streamHeader);
    EXPECT_EQ(streamHeader->recordCount, s_ExpectedDualCount);
    EXPECT_EQ(
        streamHeader->commandBytes,
        sizeof(Graphics::GpuCommandIrCopyBufferRecord) + sizeof(Graphics::GpuCommandIrCopyTextureRecord)
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

