// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"
#include "task_graph_command_ir_test_utils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_command_ir_reader_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuCommandIrStreamReader, DecodesTheCompleteBuiltinPodStream){
    TestArena testArena;
    Graphics::GpuCommandIrCapture emptyCapture(testArena.arena);
    Graphics::GpuCommandIrStreamReader emptyReader(emptyCapture.commandBytes());
    Graphics::GpuCommandIrBuiltinTaskRecord emptyOutput;
    emptyOutput.task = Graphics::GpuTaskId{ 99u, 98u };
    EXPECT_EQ(emptyReader.next(emptyOutput), Graphics::GpuCommandIrStreamReadStatus::End);
    EXPECT_TRUE(emptyReader.validation().valid());
    EXPECT_EQ(emptyReader.graphGeneration(), 0u);
    EXPECT_EQ(emptyReader.planGeneration(), 0u);
    EXPECT_EQ(emptyReader.recordCount(), 0u);
    EXPECT_EQ(emptyOutput.task.index, 99u);
    EXPECT_EQ(emptyOutput.task.generation, 98u);

    Graphics::GpuCommandIrCapture capture(testArena.arena);
    ASSERT_TRUE(CaptureAllBuiltinCommandIrRecords(capture));
    const BinaryByteView bytes = capture.commandBytes();

    const Graphics::GpuCommandIrStreamValidationResult completeValidation = Graphics::ValidateGpuCommandIrStream(bytes);
    EXPECT_TRUE(completeValidation.complete);
    EXPECT_TRUE(completeValidation.valid());
    EXPECT_FALSE(completeValidation.failed());
    EXPECT_EQ(completeValidation.byteOffset, bytes.size());
    EXPECT_EQ(completeValidation.recordIndex, 4u);

    Graphics::GpuCommandIrStreamReader reader(bytes);
    EXPECT_FALSE(reader.validation().complete);
    EXPECT_FALSE(reader.validation().valid());
    EXPECT_FALSE(reader.validation().failed());
    EXPECT_EQ(reader.graphGeneration(), s_CommandIrTask.generation);
    EXPECT_EQ(reader.planGeneration(), s_CommandIrPacket.generation);
    EXPECT_EQ(reader.recordCount(), 4u);

    Graphics::GpuCommandIrBuiltinTaskRecord record;
    ASSERT_EQ(reader.next(record), Graphics::GpuCommandIrStreamReadStatus::Record);
    EXPECT_EQ(record.opcode, Graphics::GpuCommandIrOpcode::CopyBuffer);
    EXPECT_EQ(record.task, s_CommandIrTask);
    EXPECT_EQ(record.packet, s_CommandIrPacket);
    EXPECT_EQ(record.queue, s_CommandIrQueue);
    EXPECT_EQ(record.source, s_CommandIrSource);
    EXPECT_EQ(record.destination, s_CommandIrDestination);
    EXPECT_EQ(record.sourceOffsetBytes, 16u);
    EXPECT_EQ(record.destinationOffsetBytes, 32u);
    EXPECT_EQ(record.dataSizeBytes, 64u);

    ASSERT_EQ(reader.next(record), Graphics::GpuCommandIrStreamReadStatus::Record);
    EXPECT_EQ(record.opcode, Graphics::GpuCommandIrOpcode::CopyTexture);
    EXPECT_EQ(record.task.generation, s_CommandIrTask.generation);
    EXPECT_EQ(record.source, s_CommandIrSource);
    EXPECT_EQ(record.destination, s_CommandIrDestination);
    EXPECT_EQ(record.sourceSlice.x, 1u);
    EXPECT_EQ(record.sourceSlice.width, 4u);
    EXPECT_EQ(record.sourceSlice.mipLevel, 7u);
    EXPECT_EQ(record.sourceSlice.arraySlice, 8u);
    EXPECT_EQ(record.destinationSlice.x, 9u);
    EXPECT_EQ(record.destinationSlice.width, 12u);
    EXPECT_EQ(record.destinationSlice.mipLevel, 15u);
    EXPECT_EQ(record.destinationSlice.arraySlice, 16u);

    ASSERT_EQ(reader.next(record), Graphics::GpuCommandIrStreamReadStatus::Record);
    EXPECT_EQ(record.opcode, Graphics::GpuCommandIrOpcode::ClearBuffer);
    EXPECT_EQ(record.destination, s_CommandIrDestination);
    EXPECT_EQ(record.uintClearValue, Graphics::UIntColor(0xdecafbadU));

    ASSERT_EQ(reader.next(record), Graphics::GpuCommandIrStreamReadStatus::Record);
    EXPECT_EQ(record.opcode, Graphics::GpuCommandIrOpcode::ClearTexture);
    EXPECT_EQ(record.destination, s_CommandIrDestination);
    EXPECT_EQ(record.destinationSubresources, Graphics::TextureSubresourceSet(2u, 3u, 4u, 5u));
    EXPECT_EQ(record.clearTextureValueType, Graphics::GpuClearTextureTaskValueType::DepthStencil);
    EXPECT_EQ(record.floatClearValue, Graphics::Color(0.25f, 0.5f, 0.75f, 1.f));
    EXPECT_EQ(record.uintClearValue, Graphics::UIntColor(2u, 3u, 5u, 7u));
    EXPECT_EQ(record.intClearValue, Graphics::IntColor(-2, -3, -5, -7));
    EXPECT_EQ(record.depthClearValue, 0.125f);
    EXPECT_EQ(record.stencilClearValue, 19u);
    EXPECT_TRUE(record.clearDepth);
    EXPECT_TRUE(record.clearStencil);

    EXPECT_EQ(reader.next(record), Graphics::GpuCommandIrStreamReadStatus::End);
    EXPECT_EQ(reader.next(record), Graphics::GpuCommandIrStreamReadStatus::End);
    EXPECT_TRUE(reader.validation().complete);
    EXPECT_TRUE(reader.validation().valid());
    EXPECT_EQ(reader.validation().byteOffset, bytes.size());
    EXPECT_EQ(reader.validation().recordIndex, 4u);
}

TEST(GpuCommandIrStreamReader, DecodesCanonicalColorAndSingleAspectClearRecords){
    TestArena testArena;
    Graphics::GpuCommandIrCapture capture(testArena.arena);
    Graphics::GpuClearTextureTaskDesc clearTexture;
    clearTexture.destination = s_CommandIrDestination;
    clearTexture.subresources = Graphics::TextureSubresourceSet(1u, 2u, 3u, 4u);
    // The legacy capture record keeps these descriptor values, while the POD stream must canonicalize them away for
    // color clears because native color-clear lowering ignores depth/stencil aspect selection.
    clearTexture.clearDepth = true;
    clearTexture.clearStencil = true;
    clearTexture.valueType = Graphics::GpuClearTextureTaskValueType::Float;
    clearTexture.floatValue = Graphics::Color(0.1f, 0.2f, 0.3f, 0.4f);
    ASSERT_TRUE(capture.captureClearTexture(s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, s_CommandIrDestination, clearTexture));
    clearTexture.valueType = Graphics::GpuClearTextureTaskValueType::UInt;
    clearTexture.uintValue = Graphics::UIntColor(11u, 12u, 13u, 14u);
    ASSERT_TRUE(capture.captureClearTexture(s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, s_CommandIrDestination, clearTexture));
    clearTexture.valueType = Graphics::GpuClearTextureTaskValueType::Int;
    clearTexture.intValue = Graphics::IntColor(-11, -12, -13, -14);
    ASSERT_TRUE(capture.captureClearTexture(s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, s_CommandIrDestination, clearTexture));
    clearTexture.valueType = Graphics::GpuClearTextureTaskValueType::DepthStencil;
    clearTexture.depthValue = 0.75f;
    clearTexture.stencilValue = 23u;
    clearTexture.clearStencil = false;
    ASSERT_TRUE(capture.captureClearTexture(s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, s_CommandIrDestination, clearTexture));

    Graphics::GpuCommandIrStreamReader reader(capture.commandBytes());
    Graphics::GpuCommandIrBuiltinTaskRecord record;
    ASSERT_EQ(reader.next(record), Graphics::GpuCommandIrStreamReadStatus::Record);
    EXPECT_EQ(record.clearTextureValueType, Graphics::GpuClearTextureTaskValueType::Float);
    EXPECT_EQ(record.floatClearValue, clearTexture.floatValue);
    EXPECT_FALSE(record.clearDepth);
    EXPECT_FALSE(record.clearStencil);
    ASSERT_EQ(reader.next(record), Graphics::GpuCommandIrStreamReadStatus::Record);
    EXPECT_EQ(record.clearTextureValueType, Graphics::GpuClearTextureTaskValueType::UInt);
    EXPECT_EQ(record.uintClearValue, Graphics::UIntColor(11u, 12u, 13u, 14u));
    EXPECT_FALSE(record.clearDepth);
    EXPECT_FALSE(record.clearStencil);
    ASSERT_EQ(reader.next(record), Graphics::GpuCommandIrStreamReadStatus::Record);
    EXPECT_EQ(record.clearTextureValueType, Graphics::GpuClearTextureTaskValueType::Int);
    EXPECT_EQ(record.intClearValue, Graphics::IntColor(-11, -12, -13, -14));
    EXPECT_FALSE(record.clearDepth);
    EXPECT_FALSE(record.clearStencil);
    ASSERT_EQ(reader.next(record), Graphics::GpuCommandIrStreamReadStatus::Record);
    EXPECT_EQ(record.clearTextureValueType, Graphics::GpuClearTextureTaskValueType::DepthStencil);
    EXPECT_EQ(record.depthClearValue, 0.75f);
    EXPECT_EQ(record.stencilClearValue, 23u);
    EXPECT_TRUE(record.clearDepth);
    EXPECT_FALSE(record.clearStencil);
    EXPECT_EQ(reader.next(record), Graphics::GpuCommandIrStreamReadStatus::End);
    EXPECT_TRUE(reader.validation().valid());
}

TEST(GpuCommandIrStreamReader, RejectsMalformedHeadersBeforeReadingRecords){
    TestArena testArena;
    Graphics::GpuCommandIrCapture capture(testArena.arena);
    ASSERT_TRUE(CaptureAllBuiltinCommandIrRecords(capture));
    const BinaryByteView validBytes = capture.commandBytes();

    const Graphics::GpuCommandIrStreamValidationResult nullData = Graphics::ValidateGpuCommandIrStream(
        BinaryByteView{ nullptr, 1u }
    );
    EXPECT_EQ(nullData.error, Graphics::GpuCommandIrStreamValidationError::NullData);
    EXPECT_TRUE(nullData.complete);
    EXPECT_FALSE(nullData.valid());
    EXPECT_EQ(nullData.recordIndex, Limit<u64>::s_Max);

    const Graphics::GpuCommandIrStreamValidationResult truncatedHeader = Graphics::ValidateGpuCommandIrStream(
        BinaryByteView{ validBytes.data(), sizeof(Graphics::GpuCommandIrStreamHeader) - 1u }
    );
    EXPECT_EQ(truncatedHeader.error, Graphics::GpuCommandIrStreamValidationError::TruncatedStreamHeader);
    EXPECT_TRUE(truncatedHeader.complete);
    EXPECT_FALSE(truncatedHeader.valid());

    const auto expectHeaderError = [&testArena, validBytes](
        const auto& mutate,
        const Graphics::GpuCommandIrStreamValidationError::Enum expectedError
    ){
        Graphics::GraphicsBytes corruptedBytes(testArena.arena);
        CopyCommandIrBytes(corruptedBytes, validBytes);
        mutate(corruptedBytes);
        const Graphics::GpuCommandIrStreamValidationResult result = Graphics::ValidateGpuCommandIrStream(
            BinaryByteView{ corruptedBytes.data(), corruptedBytes.size() }
        );
        EXPECT_EQ(result.error, expectedError);
        EXPECT_TRUE(result.complete);
        EXPECT_TRUE(result.failed());
        EXPECT_FALSE(result.valid());
        EXPECT_EQ(result.byteOffset, 0u);
        EXPECT_EQ(result.recordIndex, Limit<u64>::s_Max);
    };

    expectHeaderError([](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(bytes, offsetof(Graphics::GpuCommandIrStreamHeader, magic), 0u);
    }, Graphics::GpuCommandIrStreamValidationError::InvalidMagic);
    expectHeaderError([](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(bytes, offsetof(Graphics::GpuCommandIrStreamHeader, version), static_cast<u16>(99u));
    }, Graphics::GpuCommandIrStreamValidationError::UnsupportedVersion);
    expectHeaderError([](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(bytes, offsetof(Graphics::GpuCommandIrStreamHeader, reserved), static_cast<u16>(1u));
    }, Graphics::GpuCommandIrStreamValidationError::InvalidHeaderReserved);
    expectHeaderError([](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(
            bytes,
            offsetof(Graphics::GpuCommandIrStreamHeader, payloadBytes),
            static_cast<u64>(bytes.size())
        );
    }, Graphics::GpuCommandIrStreamValidationError::PayloadSizeMismatch);
    expectHeaderError([](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(bytes, offsetof(Graphics::GpuCommandIrStreamHeader, graphGeneration), 0u);
    }, Graphics::GpuCommandIrStreamValidationError::InvalidGraphGeneration);
    expectHeaderError([](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(bytes, offsetof(Graphics::GpuCommandIrStreamHeader, planGeneration), 0u);
    }, Graphics::GpuCommandIrStreamValidationError::InvalidPlanGeneration);
    expectHeaderError([](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(bytes, offsetof(Graphics::GpuCommandIrStreamHeader, recordCount), 0u);
    }, Graphics::GpuCommandIrStreamValidationError::InvalidGraphGeneration);
    expectHeaderError([](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(bytes, offsetof(Graphics::GpuCommandIrStreamHeader, recordCount), 64u);
    }, Graphics::GpuCommandIrStreamValidationError::InvalidRecordCount);
}

TEST(GpuCommandIrStreamReader, RejectsMalformedRecordsWithoutPublishingPartialOutput){
    TestArena testArena;
    Graphics::GpuCommandIrCapture capture(testArena.arena);
    ASSERT_TRUE(CaptureAllBuiltinCommandIrRecords(capture));
    const BinaryByteView validBytes = capture.commandBytes();

    const auto expectRecordError = [&testArena, validBytes](
        const usize validPrefixCount,
        const usize expectedByteOffset,
        const auto& mutate,
        const Graphics::GpuCommandIrStreamValidationError::Enum expectedError
    ){
        Graphics::GraphicsBytes corruptedBytes(testArena.arena);
        CopyCommandIrBytes(corruptedBytes, validBytes);
        mutate(corruptedBytes);

        Graphics::GpuCommandIrStreamReader reader(BinaryByteView{ corruptedBytes.data(), corruptedBytes.size() });
        Graphics::GpuCommandIrBuiltinTaskRecord output;
        for(usize recordIndex = 0u; recordIndex < validPrefixCount; ++recordIndex)
            ASSERT_EQ(reader.next(output), Graphics::GpuCommandIrStreamReadStatus::Record);

        output.opcode = Graphics::GpuCommandIrOpcode::ClearTexture;
        output.task = Graphics::GpuTaskId{ 91u, 92u };
        output.packet = Graphics::GpuSubmissionPacketId{ 93u, 92u };
        output.queue = Graphics::GpuPhysicalQueueId{ 94u, 95u };
        output.source = Graphics::GpuGraphResourceId{ 96u, 92u };
        output.destination = Graphics::GpuGraphResourceId{ 97u, 92u };
        output.dataSizeBytes = 98u;
        output.clearDepth = true;
        output.clearStencil = true;
        EXPECT_EQ(reader.next(output), Graphics::GpuCommandIrStreamReadStatus::Error);
        EXPECT_EQ(output.opcode, Graphics::GpuCommandIrOpcode::ClearTexture);
        EXPECT_EQ(output.task.index, 91u);
        EXPECT_EQ(output.task.generation, 92u);
        EXPECT_EQ(output.packet.index, 93u);
        EXPECT_EQ(output.packet.generation, 92u);
        EXPECT_EQ(output.queue.index, 94u);
        EXPECT_EQ(output.queue.deviceGeneration, 95u);
        EXPECT_EQ(output.source.index, 96u);
        EXPECT_EQ(output.source.generation, 92u);
        EXPECT_EQ(output.destination.index, 97u);
        EXPECT_EQ(output.destination.generation, 92u);
        EXPECT_EQ(output.dataSizeBytes, 98u);
        EXPECT_TRUE(output.clearDepth);
        EXPECT_TRUE(output.clearStencil);
        EXPECT_EQ(reader.validation().error, expectedError);
        EXPECT_TRUE(reader.validation().complete);
        EXPECT_TRUE(reader.validation().failed());
        EXPECT_EQ(reader.validation().byteOffset, expectedByteOffset);
        EXPECT_EQ(reader.validation().recordIndex, validPrefixCount);
        EXPECT_EQ(reader.next(output), Graphics::GpuCommandIrStreamReadStatus::Error);
    };

    expectRecordError(1u, s_CommandIrCopyTextureOffset, [](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(
            bytes,
            s_CommandIrCopyTextureOffset + offsetof(Graphics::GpuCommandIrHeader, byteSize),
            static_cast<u16>(3u)
        );
    }, Graphics::GpuCommandIrStreamValidationError::InvalidRecordSize);
    expectRecordError(1u, s_CommandIrCopyTextureOffset, [](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(
            bytes,
            s_CommandIrCopyTextureOffset + offsetof(Graphics::GpuCommandIrHeader, byteSize),
            Limit<u16>::s_Max
        );
    }, Graphics::GpuCommandIrStreamValidationError::InvalidRecordSize);
    expectRecordError(1u, s_CommandIrCopyTextureOffset, [](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(
            bytes,
            s_CommandIrCopyTextureOffset + offsetof(Graphics::GpuCommandIrHeader, opcode),
            Graphics::GpuCommandIrWireOpcode::SetGraphicsState
        );
    }, Graphics::GpuCommandIrStreamValidationError::UnsupportedOpcode);
    expectRecordError(1u, s_CommandIrCopyTextureOffset, [](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(
            bytes,
            s_CommandIrCopyTextureOffset + offsetof(Graphics::GpuCommandIrHeader, opcode),
            Graphics::GpuCommandIrWireOpcode::kCount
        );
    }, Graphics::GpuCommandIrStreamValidationError::UnsupportedOpcode);
    expectRecordError(0u, s_CommandIrCopyBufferOffset, [](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(
            bytes,
            s_CommandIrCopyBufferOffset + offsetof(Graphics::GpuCommandIrCopyBufferRecord, dataSizeBytes),
            0u
        );
    }, Graphics::GpuCommandIrStreamValidationError::InvalidRecord);
    expectRecordError(0u, s_CommandIrCopyBufferOffset, [](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(
            bytes,
            s_CommandIrCopyBufferOffset + offsetof(Graphics::GpuCommandIrCopyBufferRecord, sourceResourceIndex),
            Limit<u32>::s_Max
        );
    }, Graphics::GpuCommandIrStreamValidationError::InvalidRecord);
    expectRecordError(0u, s_CommandIrCopyBufferOffset, [](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(
            bytes,
            s_CommandIrCopyBufferOffset
                + offsetof(Graphics::GpuCommandIrCopyBufferRecord, context)
                + offsetof(Graphics::GpuCommandIrRecordContext, queueDeviceGeneration),
            static_cast<u16>(0u)
        );
    }, Graphics::GpuCommandIrStreamValidationError::InvalidRecord);
    expectRecordError(1u, s_CommandIrCopyTextureOffset, [](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(
            bytes,
            s_CommandIrCopyTextureOffset + offsetof(Graphics::GpuCommandIrCopyTextureRecord, destinationResourceIndex),
            Limit<u32>::s_Max
        );
    }, Graphics::GpuCommandIrStreamValidationError::InvalidRecord);
    expectRecordError(2u, s_CommandIrClearBufferOffset, [](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(
            bytes,
            s_CommandIrClearBufferOffset + offsetof(Graphics::GpuCommandIrClearBufferRecord, destinationResourceIndex),
            Limit<u32>::s_Max
        );
    }, Graphics::GpuCommandIrStreamValidationError::InvalidRecord);
    expectRecordError(3u, s_CommandIrClearTextureOffset, [](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(
            bytes,
            s_CommandIrClearTextureOffset
                + offsetof(Graphics::GpuCommandIrClearTextureRecord, destinationSubresources)
                + offsetof(Graphics::GpuCommandIrTextureSubresourceSet, numMipLevels),
            0u
        );
    }, Graphics::GpuCommandIrStreamValidationError::InvalidRecord);
    expectRecordError(3u, s_CommandIrClearTextureOffset, [](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(
            bytes,
            s_CommandIrClearTextureOffset + offsetof(Graphics::GpuCommandIrClearTextureRecord, clearTextureValueType),
            static_cast<u8>(Graphics::GpuClearTextureTaskValueType::kCount)
        );
    }, Graphics::GpuCommandIrStreamValidationError::InvalidRecord);
    expectRecordError(3u, s_CommandIrClearTextureOffset, [](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(
            bytes,
            s_CommandIrClearTextureOffset + offsetof(Graphics::GpuCommandIrClearTextureRecord, clearFlags),
            static_cast<Graphics::GpuCommandIrClearTextureFlag::Mask>(4u)
        );
    }, Graphics::GpuCommandIrStreamValidationError::InvalidRecord);
    expectRecordError(3u, s_CommandIrClearTextureOffset, [](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(
            bytes,
            s_CommandIrClearTextureOffset + offsetof(Graphics::GpuCommandIrClearTextureRecord, clearFlags),
            Graphics::GpuCommandIrClearTextureFlag::None
        );
    }, Graphics::GpuCommandIrStreamValidationError::InvalidRecord);
    expectRecordError(3u, s_CommandIrClearTextureOffset, [](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(
            bytes,
            s_CommandIrClearTextureOffset + offsetof(Graphics::GpuCommandIrClearTextureRecord, clearTextureValueType),
            static_cast<u8>(Graphics::GpuClearTextureTaskValueType::Float)
        );
        WriteCommandIrPod(
            bytes,
            s_CommandIrClearTextureOffset + offsetof(Graphics::GpuCommandIrClearTextureRecord, clearFlags),
            Graphics::GpuCommandIrClearTextureFlag::ClearDepth
        );
    }, Graphics::GpuCommandIrStreamValidationError::InvalidRecord);
    expectRecordError(3u, s_CommandIrClearTextureOffset, [](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(
            bytes,
            s_CommandIrClearTextureOffset + offsetof(Graphics::GpuCommandIrClearTextureRecord, reserved),
            static_cast<u8>(1u)
        );
    }, Graphics::GpuCommandIrStreamValidationError::InvalidRecord);
    expectRecordError(3u, s_CommandIrClearTextureOffset, [](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(bytes, offsetof(Graphics::GpuCommandIrStreamHeader, recordCount), 3u);
    }, Graphics::GpuCommandIrStreamValidationError::TrailingPayload);
    expectRecordError(4u, validBytes.size(), [](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(bytes, offsetof(Graphics::GpuCommandIrStreamHeader, recordCount), 5u);
    }, Graphics::GpuCommandIrStreamValidationError::TruncatedRecord);
    expectRecordError(1u, s_CommandIrCopyTextureOffset, [](Graphics::GraphicsBytes& bytes){
        bytes.resize(s_CommandIrCopyTextureOffset + sizeof(Graphics::GpuCommandIrHeader));
        WriteCommandIrPod(
            bytes,
            offsetof(Graphics::GpuCommandIrStreamHeader, recordCount),
            2u
        );
        WriteCommandIrPod(
            bytes,
            offsetof(Graphics::GpuCommandIrStreamHeader, payloadBytes),
            static_cast<u64>(bytes.size() - sizeof(Graphics::GpuCommandIrStreamHeader))
        );
    }, Graphics::GpuCommandIrStreamValidationError::TruncatedRecord);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

