// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"
#include "task_graph_command_ir_test_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_command_ir_capture_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuCommandIrCapture, RejectsForeignGraphAndPlanGenerationsWithoutChangingCapturedBytes){
    TestArena testArena;
    Core::GpuCommandIrCapture capture(testArena.arena);
    const Core::GpuTaskId task{ .generation = 17u, .index = 4u };
    const Core::GpuSubmissionPacketId packet{ .generation = 23u, .index = 2u };
    const Core::GpuPhysicalQueueId queue{ .index = 1u, .deviceGeneration = 3u };
    const Core::GpuGraphResourceId source{ .generation = 17u, .index = 5u };
    const Core::GpuGraphResourceId destination{ .generation = 17u, .index = 6u };

    ASSERT_TRUE(capture.captureCopyBuffer(task, packet, queue, source, 16u, destination, 32u, 64u));
    Core::GpuClearTextureTaskDesc clearTexture;
    clearTexture.destination = destination;
    clearTexture.subresources = Core::TextureSubresourceSet(0u, 1u, 0u, 1u);
    clearTexture.valueType = Core::GpuClearTextureTaskValueType::Float;
    clearTexture.floatValue = Core::Color(0.25f, 0.5f, 0.75f, 1.f);
    // The stream omits aspect flags ignored by native color-clear lowering.
    clearTexture.clearDepth = true;
    clearTexture.clearStencil = true;
    ASSERT_TRUE(capture.captureClearTexture(task, packet, queue, destination, clearTexture));

    const BinaryByteView bytesBeforeRejectedRecord = capture.commandBytes();
    Core::GraphicsBytes streamBeforeRejectedRecord(testArena.arena);
    streamBeforeRejectedRecord.resize(bytesBeforeRejectedRecord.size());
    NWB_MEMCPY(streamBeforeRejectedRecord.data(), streamBeforeRejectedRecord.size(), bytesBeforeRejectedRecord.data(), streamBeforeRejectedRecord.size());
    const Core::GpuTaskId anotherGraphTask{ .generation = task.generation + 1u, .index = static_cast<u32>(task.index) };
    const Core::GpuSubmissionPacketId anotherGraphPacket{ .generation = packet.generation + 1u, .index = static_cast<u32>(packet.index) };
    const Core::GpuGraphResourceId anotherGraphResource{ .generation = destination.generation + 1u, .index = static_cast<u32>(destination.index) };
    EXPECT_FALSE(capture.captureClearBuffer(anotherGraphTask, anotherGraphPacket, queue, anotherGraphResource, 0xdecafbadU));
    EXPECT_FALSE(capture.captureClearBuffer(task, Core::GpuSubmissionPacketId{ .generation = packet.generation + 2u, .index = packet.index }, queue, destination, 0xdecafbadU));
    const BinaryByteView bytesAfterRejectedRecord = capture.commandBytes();
    EXPECT_EQ(bytesAfterRejectedRecord.size(), streamBeforeRejectedRecord.size());
    EXPECT_EQ(NWB_MEMCMP(bytesAfterRejectedRecord.data(), streamBeforeRejectedRecord.data(), streamBeforeRejectedRecord.size()), 0);

}

TEST(GpuCommandIrCapture, RejectsNonEmptyCaptureFromDifferentRecordingAttempt){
    TestArena testArena;
    Core::GpuCommandIrCapture capture(testArena.arena);
    const Core::GpuTaskId task{ .generation = 17u, .index = 4u };
    const Core::GpuSubmissionPacketId packet{ .generation = 23u, .index = 2u };
    const Core::GpuPhysicalQueueId queue{ .index = 1u, .deviceGeneration = 3u };
    const Core::GpuGraphResourceId source{ .generation = 17u, .index = 5u };
    const Core::GpuGraphResourceId destination{ .generation = 17u, .index = 6u };

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
    Core::GpuCommandIrCapture capture(testArena.arena);
    Core::GpuClearTextureRectUIntTaskDesc clear;
    clear.destination = s_CommandIrDestination;
    clear.subresources = Core::TextureSubresourceSet(2u, 3u, 4u, 5u);
    clear.rect = Core::Rect(-2, 7, 3, 11);
    clear.uintValue = Core::UIntColor(0x10203040u, 0x50607080u, 0x90a0b0c0u, 0xd0e0f000u);
    ASSERT_TRUE(capture.captureClearTextureRectUInt(s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, s_CommandIrDestination, clear));

    ASSERT_EQ(capture.recordCount(), 1u);
    const BinaryByteView bytes = capture.commandBytes();
    Core::GpuCommandIrStreamReader reader(bytes);
    ASSERT_TRUE(reader.nextBuiltinTask());

    clear.rect = Core::Rect(4, 4, 0, 1);
    EXPECT_FALSE(capture.captureClearTextureRectUInt(s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, s_CommandIrDestination, clear));
    EXPECT_EQ(capture.recordCount(), 1u);

    Core::GraphicsBytes downgradedRectBytes(testArena.arena);
    CopyCommandIrBytes(bytes, downgradedRectBytes);
    WriteCommandIrPod(offsetof(Core::GpuCommandIrStreamHeader, version), static_cast<u16>(Core::s_GpuCommandIrStreamVersion - 1u), downgradedRectBytes);
    Core::GpuCommandIrStreamReader downgradedRectReader(BinaryByteView{ downgradedRectBytes.data(), downgradedRectBytes.size(), });
    const auto terminal1 = downgradedRectReader.nextBuiltinTask();
    ASSERT_FALSE(terminal1);
    EXPECT_EQ(terminal1.error(), Core::GpuCommandIrStreamReadStatus::Error);
    EXPECT_EQ(downgradedRectReader.validation().error, Core::GpuCommandIrStreamValidationError::UnsupportedVersion);
}

TEST(GpuCommandIrCapture, RollbackPreservesExactMixedRecordPrefixAtRecordBoundaries){
    TestArena testArena;
    Core::GpuCommandIrCapture capture(testArena.arena);
    const Core::GpuTaskId task{ .generation = 17u, .index = 4u };
    const Core::GpuSubmissionPacketId packet{ .generation = 17u, .index = 2u };
    const Core::GpuPhysicalQueueId queue{ .index = 1u, .deviceGeneration = 3u };
    const Core::GpuGraphResourceId source{ .generation = 17u, .index = 5u };
    const Core::GpuGraphResourceId destination{ .generation = 17u, .index = 6u };

    Core::TextureSlice sourceSlice;
    sourceSlice.setOrigin(1u, 2u, 3u).setSize(4u, 5u, 6u).setMipLevel(7u).setArraySlice(8u);
    Core::TextureSlice destinationSlice;
    destinationSlice.setOrigin(9u, 10u, 11u).setSize(12u, 13u, 14u).setMipLevel(15u).setArraySlice(16u);
    Core::GpuClearTextureTaskDesc clearTexture;
    clearTexture.destination = destination;
    clearTexture.subresources = Core::TextureSubresourceSet(2u, 3u, 4u, 5u);
    clearTexture.valueType = Core::GpuClearTextureTaskValueType::DepthStencil;
    clearTexture.floatValue = Core::Color(0.25f, 0.5f, 0.75f, 1.f);
    clearTexture.uintValue = Core::UIntColor(2u, 3u, 5u, 7u);
    clearTexture.intValue = Core::IntColor(-2, -3, -5, -7);
    clearTexture.depthValue = 0.125f;
    clearTexture.stencilValue = 19u;
    clearTexture.clearDepth = true;
    clearTexture.clearStencil = true;

    ASSERT_TRUE(capture.captureCopyBuffer(task, packet, queue, source, 16u, destination, 32u, 64u));
    ASSERT_TRUE(capture.captureCopyTexture(task, packet, queue, source, sourceSlice, destination, destinationSlice));
    ASSERT_TRUE(capture.captureClearBuffer(task, packet, queue, destination, 0xdecafbadU));
    ASSERT_TRUE(capture.captureClearTexture(task, packet, queue, destination, clearTexture));

    const BinaryByteView bytes = capture.commandBytes();
    const usize copyTextureEnd = sizeof(Core::GpuCommandIrStreamHeader) + sizeof(Core::GpuCommandIrCopyBufferRecord) + sizeof(Core::GpuCommandIrCopyTextureRecord);
    ASSERT_EQ(capture.recordCount(), 4u);

    Core::GraphicsBytes expectedPrefix(testArena.arena);
    expectedPrefix.resize(copyTextureEnd);
    NWB_MEMCPY(expectedPrefix.data(), expectedPrefix.size(), bytes.data(), expectedPrefix.size());
    capture.rollback(2u);
    const BinaryByteView rolledBackBytes = capture.commandBytes();
    EXPECT_EQ(capture.recordCount(), 2u);
    EXPECT_EQ(capture.graphGeneration(), task.generation);
    EXPECT_EQ(rolledBackBytes.size(), expectedPrefix.size());
    // Rollback rewrites the header count/payload; the two surviving records stay an exact byte prefix of the capture.
    EXPECT_EQ(NWB_MEMCMP(rolledBackBytes.data() + sizeof(Core::GpuCommandIrStreamHeader), expectedPrefix.data() + sizeof(Core::GpuCommandIrStreamHeader), expectedPrefix.size() - sizeof(Core::GpuCommandIrStreamHeader)), 0);

    usize cursor = 0u;
    const auto streamHeader = ReadPOD<Core::GpuCommandIrStreamHeader>(rolledBackBytes, cursor);
    ASSERT_TRUE(streamHeader);
    EXPECT_EQ(streamHeader->recordCount, 2u);
    EXPECT_EQ(streamHeader->commandBytes, sizeof(Core::GpuCommandIrCopyBufferRecord) + sizeof(Core::GpuCommandIrCopyTextureRecord));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

