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


TEST(GpuCommandIrCapture, RetainsBuiltInRecordsForOneGraphAndPlanGeneration){
    TestArena testArena;
    Graphics::GpuCommandIrCapture capture(testArena.arena);
    const Graphics::GpuTaskId task{ 4u, 17u };
    const Graphics::GpuSubmissionPacketId packet{ 2u, 23u };
    const Graphics::GpuPhysicalQueueId queue{ 1u, 3u };
    const Graphics::GpuGraphResourceId source{ 5u, 17u };
    const Graphics::GpuGraphResourceId destination{ 6u, 17u };

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
    // This legacy record keeps descriptor fields verbatim; the byte stream canonicalizes them separately.
    clearTexture.clearDepth = true;
    clearTexture.clearStencil = true;
    ASSERT_TRUE(capture.captureClearTexture(task, packet, queue, destination, clearTexture));

    ASSERT_EQ(capture.recordCount(), 2u);
    EXPECT_EQ(capture.graphGeneration(), task.generation);
    EXPECT_EQ(capture.planGeneration(), packet.generation);
    const Graphics::GpuCommandIrBuiltinTaskRecord* const copyRecord = capture.recordAt(0u);
    ASSERT_NE(copyRecord, nullptr);
    EXPECT_EQ(copyRecord->opcode, Graphics::GpuCommandIrOpcode::CopyBuffer);
    EXPECT_EQ(copyRecord->task, task);
    EXPECT_EQ(copyRecord->packet, packet);
    EXPECT_EQ(copyRecord->queue, queue);
    EXPECT_EQ(copyRecord->source, source);
    EXPECT_EQ(copyRecord->destination, destination);
    EXPECT_EQ(copyRecord->sourceOffsetBytes, 16u);
    EXPECT_EQ(copyRecord->destinationOffsetBytes, 32u);
    EXPECT_EQ(copyRecord->dataSizeBytes, 64u);

    const Graphics::GpuCommandIrBuiltinTaskRecord* const clearRecord = capture.recordAt(1u);
    ASSERT_NE(clearRecord, nullptr);
    EXPECT_EQ(clearRecord->opcode, Graphics::GpuCommandIrOpcode::ClearTexture);
    EXPECT_EQ(clearRecord->destination, destination);
    EXPECT_EQ(clearRecord->destinationSubresources, clearTexture.subresources);
    EXPECT_EQ(clearRecord->clearTextureValueType, clearTexture.valueType);
    EXPECT_EQ(clearRecord->floatClearValue, clearTexture.floatValue);
    EXPECT_TRUE(clearRecord->clearDepth);
    EXPECT_TRUE(clearRecord->clearStencil);

    {
        const BinaryByteView capturedBytes = capture.commandBytes();
        usize captureCursor = sizeof(Graphics::GpuCommandIrStreamHeader);
        Graphics::GpuCommandIrCopyBufferRecord capturedCopy;
        Graphics::GpuCommandIrClearTextureRecord capturedClear;
        ASSERT_TRUE(ReadPOD(capturedBytes, captureCursor, capturedCopy));
        ASSERT_TRUE(ReadPOD(capturedBytes, captureCursor, capturedClear));
        EXPECT_EQ(capturedClear.clearTextureValueType, clearTexture.valueType);
        EXPECT_EQ(capturedClear.clearFlags, Graphics::GpuCommandIrClearTextureFlag::None);
        EXPECT_EQ(captureCursor, capturedBytes.size());
    }

    const BinaryByteView bytesBeforeRejectedRecord = capture.commandBytes();
    Graphics::GraphicsBytes streamBeforeRejectedRecord(testArena.arena);
    streamBeforeRejectedRecord.resize(bytesBeforeRejectedRecord.size());
    NWB_MEMCPY(
        streamBeforeRejectedRecord.data(),
        streamBeforeRejectedRecord.size(),
        bytesBeforeRejectedRecord.data(),
        streamBeforeRejectedRecord.size()
    );
    const Graphics::GpuTaskId anotherGraphTask{ task.index, task.generation + 1u };
    const Graphics::GpuSubmissionPacketId anotherGraphPacket{ packet.index, packet.generation + 1u };
    const Graphics::GpuGraphResourceId anotherGraphResource{ destination.index, destination.generation + 1u };
    EXPECT_FALSE(capture.captureClearBuffer(
        anotherGraphTask,
        anotherGraphPacket,
        queue,
        anotherGraphResource,
        0xdecafbadU
    ));
    EXPECT_FALSE(capture.captureClearBuffer(
        task,
        Graphics::GpuSubmissionPacketId{ packet.index, packet.generation + 2u },
        queue,
        destination,
        0xdecafbadU
    ));
    EXPECT_EQ(capture.recordCount(), 2u);
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

    capture.reset();
    EXPECT_EQ(capture.recordCount(), 0u);
    EXPECT_EQ(capture.graphGeneration(), 0u);
    EXPECT_EQ(capture.planGeneration(), 0u);
    EXPECT_EQ(capture.recordAt(0u), nullptr);
    const BinaryByteView resetBytes = capture.commandBytes();
    usize resetCursor = 0u;
    Graphics::GpuCommandIrStreamHeader resetHeader;
    ASSERT_TRUE(ReadPOD(resetBytes, resetCursor, resetHeader));
    EXPECT_EQ(resetHeader.magic, Graphics::s_GpuCommandIrStreamMagic);
    EXPECT_EQ(resetHeader.graphGeneration, 0u);
    EXPECT_EQ(resetHeader.planGeneration, 0u);
    EXPECT_EQ(resetHeader.recordCount, 0u);
    EXPECT_EQ(resetHeader.payloadBytes, 0u);
    EXPECT_EQ(resetCursor, resetBytes.size());
}

TEST(GpuCommandIrCapture, RejectsNonEmptyCaptureFromDifferentRecordingAttempt){
    TestArena testArena;
    Graphics::GpuCommandIrCapture capture(testArena.arena);
    const Graphics::GpuTaskId task{ 4u, 17u };
    const Graphics::GpuSubmissionPacketId packet{ 2u, 23u };
    const Graphics::GpuPhysicalQueueId queue{ 1u, 3u };
    const Graphics::GpuGraphResourceId source{ 5u, 17u };
    const Graphics::GpuGraphResourceId destination{ 6u, 17u };

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

TEST(GpuCommandIrCapture, EncodesVersionedRectUIntTextureClearAndRejectsPrePlanIdentityStreams){
    TestArena testArena;
    Graphics::GpuCommandIrCapture capture(testArena.arena);
    Graphics::GpuClearTextureRectUIntTaskDesc clear;
    clear.destination = s_CommandIrDestination;
    clear.subresources = Graphics::TextureSubresourceSet(2u, 3u, 4u, 5u);
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
    const Graphics::GpuCommandIrBuiltinTaskRecord* const captured = capture.recordAt(0u);
    ASSERT_NE(captured, nullptr);
    EXPECT_EQ(captured->opcode, Graphics::GpuCommandIrOpcode::ClearTextureRectUInt);
    EXPECT_EQ(captured->destination, s_CommandIrDestination);
    EXPECT_EQ(captured->destinationSubresources, clear.subresources);
    EXPECT_EQ(captured->clearRect, clear.rect);
    EXPECT_EQ(captured->uintClearValue, clear.uintValue);

    const BinaryByteView bytes = capture.commandBytes();
    usize cursor = 0u;
    Graphics::GpuCommandIrStreamHeader header;
    Graphics::GpuCommandIrClearTextureRectUIntRecord encoded;
    ASSERT_TRUE(ReadPOD(bytes, cursor, header));
    ASSERT_TRUE(ReadPOD(bytes, cursor, encoded));
    EXPECT_EQ(header.version, Graphics::s_GpuCommandIrStreamVersion);
    EXPECT_EQ(header.planGeneration, s_CommandIrPacket.generation);
    EXPECT_EQ(encoded.header.opcode, Graphics::GpuCommandIrWireOpcode::ClearTextureRectUInt);
    EXPECT_EQ(encoded.header.byteSize, sizeof(encoded));
    EXPECT_EQ(encoded.context.taskIndex, s_CommandIrTask.index);
    EXPECT_EQ(encoded.destinationResourceIndex, s_CommandIrDestination.index);
    EXPECT_EQ(encoded.clearRect.minX, clear.rect.minX);
    EXPECT_EQ(encoded.clearRect.maxX, clear.rect.maxX);
    EXPECT_EQ(encoded.clearRect.minY, clear.rect.minY);
    EXPECT_EQ(encoded.clearRect.maxY, clear.rect.maxY);
    EXPECT_EQ(encoded.uintClearValue.r, clear.uintValue.r);
    EXPECT_EQ(encoded.uintClearValue.g, clear.uintValue.g);
    EXPECT_EQ(encoded.uintClearValue.b, clear.uintValue.b);
    EXPECT_EQ(encoded.uintClearValue.a, clear.uintValue.a);
    EXPECT_EQ(cursor, bytes.size());

    Graphics::GpuCommandIrStreamReader reader(bytes);
    Graphics::GpuCommandIrBuiltinTaskRecord decoded;
    ASSERT_EQ(reader.next(decoded), Graphics::GpuCommandIrStreamReadStatus::Record);
    EXPECT_EQ(decoded.opcode, Graphics::GpuCommandIrOpcode::ClearTextureRectUInt);
    EXPECT_EQ(decoded.clearRect, clear.rect);
    EXPECT_EQ(decoded.uintClearValue, clear.uintValue);
    EXPECT_EQ(reader.next(decoded), Graphics::GpuCommandIrStreamReadStatus::End);
    EXPECT_TRUE(reader.validation().valid());

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
        static_cast<u16>(Graphics::s_GpuCommandIrStreamFirstSupportedVersion - 1u)
    );
    Graphics::GpuCommandIrStreamReader downgradedRectReader(BinaryByteView{
        downgradedRectBytes.data(),
        downgradedRectBytes.size(),
    });
    EXPECT_EQ(downgradedRectReader.next(decoded), Graphics::GpuCommandIrStreamReadStatus::Error);
    EXPECT_EQ(
        downgradedRectReader.validation().error,
        Graphics::GpuCommandIrStreamValidationError::UnsupportedVersion
    );
}

TEST(GpuCommandIrCapture, EncodesBuiltInsAsLinearPodRecordsAndRollsBackAtRecordBoundaries){
    TestArena testArena;
    Graphics::GpuCommandIrCapture capture(testArena.arena);
    const Graphics::GpuTaskId task{ 4u, 17u };
    const Graphics::GpuSubmissionPacketId packet{ 2u, 17u };
    const Graphics::GpuPhysicalQueueId queue{ 1u, 3u };
    const Graphics::GpuGraphResourceId source{ 5u, 17u };
    const Graphics::GpuGraphResourceId destination{ 6u, 17u };

    const BinaryByteView emptyBytes = capture.commandBytes();
    ASSERT_EQ(emptyBytes.size(), sizeof(Graphics::GpuCommandIrStreamHeader));
    usize cursor = 0u;
    Graphics::GpuCommandIrStreamHeader streamHeader;
    ASSERT_TRUE(ReadPOD(emptyBytes, cursor, streamHeader));
    EXPECT_EQ(streamHeader.magic, Graphics::s_GpuCommandIrStreamMagic);
    EXPECT_EQ(streamHeader.version, Graphics::s_GpuCommandIrStreamVersion);
    EXPECT_EQ(streamHeader.reserved, 0u);
    EXPECT_EQ(streamHeader.graphGeneration, 0u);
    EXPECT_EQ(streamHeader.planGeneration, 0u);
    EXPECT_EQ(streamHeader.recordCount, 0u);
    EXPECT_EQ(streamHeader.payloadBytes, 0u);
    EXPECT_EQ(cursor, emptyBytes.size());

    Graphics::TextureSlice sourceSlice;
    sourceSlice
        .setOrigin(1u, 2u, 3u)
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
    clearTexture.subresources = Graphics::TextureSubresourceSet(2u, 3u, 4u, 5u);
    clearTexture.valueType = Graphics::GpuClearTextureTaskValueType::DepthStencil;
    clearTexture.floatValue = Graphics::Color(0.25f, 0.5f, 0.75f, 1.f);
    clearTexture.uintValue = Graphics::UIntColor(2u, 3u, 5u, 7u);
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
    cursor = 0u;
    ASSERT_TRUE(ReadPOD(bytes, cursor, streamHeader));
    EXPECT_EQ(streamHeader.magic, Graphics::s_GpuCommandIrStreamMagic);
    EXPECT_EQ(streamHeader.version, Graphics::s_GpuCommandIrStreamVersion);
    EXPECT_EQ(streamHeader.reserved, 0u);
    EXPECT_EQ(streamHeader.graphGeneration, task.generation);
    EXPECT_EQ(streamHeader.planGeneration, packet.generation);
    EXPECT_EQ(streamHeader.recordCount, 4u);
    EXPECT_EQ(
        streamHeader.payloadBytes,
        sizeof(Graphics::GpuCommandIrCopyBufferRecord)
            + sizeof(Graphics::GpuCommandIrCopyTextureRecord)
            + sizeof(Graphics::GpuCommandIrClearBufferRecord)
            + sizeof(Graphics::GpuCommandIrClearTextureRecord)
    );
    const usize copyTextureEnd = cursor
        + sizeof(Graphics::GpuCommandIrCopyBufferRecord)
        + sizeof(Graphics::GpuCommandIrCopyTextureRecord)
    ;

    Graphics::GpuCommandIrCopyBufferRecord copyBuffer;
    ASSERT_TRUE(ReadPOD(bytes, cursor, copyBuffer));
    EXPECT_EQ(copyBuffer.header.opcode, Graphics::GpuCommandIrWireOpcode::CopyBuffer);
    EXPECT_EQ(copyBuffer.header.byteSize, sizeof(copyBuffer));
    EXPECT_EQ(copyBuffer.context.taskIndex, task.index);
    EXPECT_EQ(copyBuffer.context.packetIndex, packet.index);
    EXPECT_EQ(copyBuffer.context.queueIndex, queue.index);
    EXPECT_EQ(copyBuffer.context.queueDeviceGeneration, queue.deviceGeneration);
    EXPECT_EQ(copyBuffer.sourceResourceIndex, source.index);
    EXPECT_EQ(copyBuffer.destinationResourceIndex, destination.index);
    EXPECT_EQ(copyBuffer.sourceOffsetBytes, 16u);
    EXPECT_EQ(copyBuffer.destinationOffsetBytes, 32u);
    EXPECT_EQ(copyBuffer.dataSizeBytes, 64u);

    Graphics::GpuCommandIrCopyTextureRecord copyTexture;
    ASSERT_TRUE(ReadPOD(bytes, cursor, copyTexture));
    EXPECT_EQ(copyTexture.header.opcode, Graphics::GpuCommandIrWireOpcode::CopyTexture);
    EXPECT_EQ(copyTexture.header.byteSize, sizeof(copyTexture));
    EXPECT_EQ(copyTexture.context.taskIndex, task.index);
    EXPECT_EQ(copyTexture.context.packetIndex, packet.index);
    EXPECT_EQ(copyTexture.context.queueIndex, queue.index);
    EXPECT_EQ(copyTexture.context.queueDeviceGeneration, queue.deviceGeneration);
    EXPECT_EQ(copyTexture.sourceResourceIndex, source.index);
    EXPECT_EQ(copyTexture.destinationResourceIndex, destination.index);
    EXPECT_EQ(copyTexture.sourceSlice.x, sourceSlice.x);
    EXPECT_EQ(copyTexture.sourceSlice.y, sourceSlice.y);
    EXPECT_EQ(copyTexture.sourceSlice.z, sourceSlice.z);
    EXPECT_EQ(copyTexture.sourceSlice.width, sourceSlice.width);
    EXPECT_EQ(copyTexture.sourceSlice.height, sourceSlice.height);
    EXPECT_EQ(copyTexture.sourceSlice.depth, sourceSlice.depth);
    EXPECT_EQ(copyTexture.sourceSlice.mipLevel, sourceSlice.mipLevel);
    EXPECT_EQ(copyTexture.sourceSlice.arraySlice, sourceSlice.arraySlice);
    EXPECT_EQ(copyTexture.destinationSlice.x, destinationSlice.x);
    EXPECT_EQ(copyTexture.destinationSlice.y, destinationSlice.y);
    EXPECT_EQ(copyTexture.destinationSlice.z, destinationSlice.z);
    EXPECT_EQ(copyTexture.destinationSlice.width, destinationSlice.width);
    EXPECT_EQ(copyTexture.destinationSlice.height, destinationSlice.height);
    EXPECT_EQ(copyTexture.destinationSlice.depth, destinationSlice.depth);
    EXPECT_EQ(copyTexture.destinationSlice.mipLevel, destinationSlice.mipLevel);
    EXPECT_EQ(copyTexture.destinationSlice.arraySlice, destinationSlice.arraySlice);

    Graphics::GpuCommandIrClearBufferRecord clearBuffer;
    ASSERT_TRUE(ReadPOD(bytes, cursor, clearBuffer));
    EXPECT_EQ(clearBuffer.header.opcode, Graphics::GpuCommandIrWireOpcode::ClearBuffer);
    EXPECT_EQ(clearBuffer.header.byteSize, sizeof(clearBuffer));
    EXPECT_EQ(clearBuffer.context.taskIndex, task.index);
    EXPECT_EQ(clearBuffer.destinationResourceIndex, destination.index);
    EXPECT_EQ(clearBuffer.clearValue, 0xdecafbadU);

    Graphics::GpuCommandIrClearTextureRecord clearTextureRecord;
    ASSERT_TRUE(ReadPOD(bytes, cursor, clearTextureRecord));
    EXPECT_EQ(clearTextureRecord.header.opcode, Graphics::GpuCommandIrWireOpcode::ClearTexture);
    EXPECT_EQ(clearTextureRecord.header.byteSize, sizeof(clearTextureRecord));
    EXPECT_EQ(clearTextureRecord.context.taskIndex, task.index);
    EXPECT_EQ(clearTextureRecord.context.packetIndex, packet.index);
    EXPECT_EQ(clearTextureRecord.context.queueIndex, queue.index);
    EXPECT_EQ(clearTextureRecord.context.queueDeviceGeneration, queue.deviceGeneration);
    EXPECT_EQ(clearTextureRecord.destinationResourceIndex, destination.index);
    EXPECT_EQ(clearTextureRecord.destinationSubresources.baseMipLevel, clearTexture.subresources.baseMipLevel);
    EXPECT_EQ(clearTextureRecord.destinationSubresources.numMipLevels, clearTexture.subresources.numMipLevels);
    EXPECT_EQ(clearTextureRecord.destinationSubresources.baseArraySlice, clearTexture.subresources.baseArraySlice);
    EXPECT_EQ(clearTextureRecord.destinationSubresources.numArraySlices, clearTexture.subresources.numArraySlices);
    EXPECT_EQ(clearTextureRecord.floatClearValue.r, clearTexture.floatValue.r);
    EXPECT_EQ(clearTextureRecord.floatClearValue.g, clearTexture.floatValue.g);
    EXPECT_EQ(clearTextureRecord.floatClearValue.b, clearTexture.floatValue.b);
    EXPECT_EQ(clearTextureRecord.floatClearValue.a, clearTexture.floatValue.a);
    EXPECT_EQ(clearTextureRecord.uintClearValue.r, clearTexture.uintValue.r);
    EXPECT_EQ(clearTextureRecord.uintClearValue.g, clearTexture.uintValue.g);
    EXPECT_EQ(clearTextureRecord.uintClearValue.b, clearTexture.uintValue.b);
    EXPECT_EQ(clearTextureRecord.uintClearValue.a, clearTexture.uintValue.a);
    EXPECT_EQ(clearTextureRecord.intClearValue.r, clearTexture.intValue.r);
    EXPECT_EQ(clearTextureRecord.intClearValue.g, clearTexture.intValue.g);
    EXPECT_EQ(clearTextureRecord.intClearValue.b, clearTexture.intValue.b);
    EXPECT_EQ(clearTextureRecord.intClearValue.a, clearTexture.intValue.a);
    EXPECT_EQ(clearTextureRecord.depthClearValue, clearTexture.depthValue);
    EXPECT_EQ(clearTextureRecord.stencilClearValue, clearTexture.stencilValue);
    EXPECT_EQ(clearTextureRecord.clearTextureValueType, clearTexture.valueType);
    EXPECT_EQ(
        clearTextureRecord.clearFlags,
        static_cast<Graphics::GpuCommandIrClearTextureFlag::Mask>(
            Graphics::GpuCommandIrClearTextureFlag::ClearDepth | Graphics::GpuCommandIrClearTextureFlag::ClearStencil
        )
    );
    EXPECT_EQ(clearTextureRecord.reserved, 0u);
    EXPECT_EQ(cursor, bytes.size());

    Graphics::GraphicsBytes expectedPrefix(testArena.arena);
    expectedPrefix.resize(copyTextureEnd);
    NWB_MEMCPY(expectedPrefix.data(), expectedPrefix.size(), bytes.data(), expectedPrefix.size());
    capture.rollback(2u);
    const BinaryByteView rolledBackBytes = capture.commandBytes();
    EXPECT_EQ(capture.recordCount(), 2u);
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

    cursor = 0u;
    ASSERT_TRUE(ReadPOD(rolledBackBytes, cursor, streamHeader));
    EXPECT_EQ(streamHeader.recordCount, 2u);
    EXPECT_EQ(
        streamHeader.payloadBytes,
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

