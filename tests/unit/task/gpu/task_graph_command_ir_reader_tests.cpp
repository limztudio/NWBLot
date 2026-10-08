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


namespace __hidden_task_graph_command_ir_reader_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuCommandIrStreamReader, EmptyAndExhaustedStreamsReportEndWithoutProducingARecord){
    TestArena testArena;
    Graphics::GpuCommandIrCapture emptyCapture(testArena.arena);
    Graphics::GpuCommandIrStreamReader emptyReader(emptyCapture.commandBytes());

    const auto terminal1 = emptyReader.nextBuiltinTask();
    ASSERT_FALSE(terminal1);
    EXPECT_EQ(terminal1.error(), Graphics::GpuCommandIrStreamReadStatus::End);
    EXPECT_TRUE(emptyReader.validation().valid());


    Graphics::GpuCommandIrCapture capture(testArena.arena);
    ASSERT_TRUE(CaptureAllBuiltinCommandIrRecords(capture));
    Graphics::GpuCommandIrStreamReader reader(capture.commandBytes());
    for(u32 recordIndex = 0u; recordIndex < 4u; ++recordIndex)
        ASSERT_TRUE(reader.nextBuiltinTask());

    const auto terminal2 = reader.nextBuiltinTask();
    ASSERT_FALSE(terminal2);
    EXPECT_EQ(terminal2.error(), Graphics::GpuCommandIrStreamReadStatus::End);
    const auto terminal3 = reader.nextBuiltinTask();
    ASSERT_FALSE(terminal3);
    EXPECT_EQ(terminal3.error(), Graphics::GpuCommandIrStreamReadStatus::End);

    EXPECT_TRUE(reader.validation().complete);
    EXPECT_TRUE(reader.validation().valid());
    EXPECT_EQ(reader.validation().byteOffset, capture.commandBytes().size());
    EXPECT_EQ(reader.validation().recordIndex, 4u);
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
    for(u16 version = 0u; version < Graphics::s_GpuCommandIrStreamVersion; ++version){
        expectHeaderError([version](Graphics::GraphicsBytes& bytes){
            WriteCommandIrPod(bytes, offsetof(Graphics::GpuCommandIrStreamHeader, version), version);
        }, Graphics::GpuCommandIrStreamValidationError::UnsupportedVersion);
    }
    expectHeaderError([](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(
            bytes,
            offsetof(Graphics::GpuCommandIrStreamHeader, version),
            static_cast<u16>(Graphics::s_GpuCommandIrStreamVersion + 1u)
        );
    }, Graphics::GpuCommandIrStreamValidationError::UnsupportedVersion);
    expectHeaderError([](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(bytes, offsetof(Graphics::GpuCommandIrStreamHeader, version), Limit<u16>::s_Max);
    }, Graphics::GpuCommandIrStreamValidationError::UnsupportedVersion);
    expectHeaderError([](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(bytes, offsetof(Graphics::GpuCommandIrStreamHeader, reserved), static_cast<u16>(1u));
    }, Graphics::GpuCommandIrStreamValidationError::InvalidHeaderReserved);
    expectHeaderError([](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(
            bytes,
            offsetof(Graphics::GpuCommandIrStreamHeader, commandBytes),
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

TEST(GpuCommandIrStreamReader, RejectsMalformedRecordsWithoutProducingPartialValues){
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
        for(usize recordIndex = 0u; recordIndex < validPrefixCount; ++recordIndex)
            ASSERT_TRUE(reader.nextBuiltinTask());


        const auto terminal4 = reader.nextBuiltinTask();
        ASSERT_FALSE(terminal4);
        EXPECT_EQ(terminal4.error(), Graphics::GpuCommandIrStreamReadStatus::Error);


        EXPECT_EQ(reader.validation().error, expectedError);
        EXPECT_TRUE(reader.validation().complete);
        EXPECT_TRUE(reader.validation().failed());
        EXPECT_EQ(reader.validation().byteOffset, expectedByteOffset);
        EXPECT_EQ(reader.validation().recordIndex, validPrefixCount);
        const auto terminal5 = reader.nextBuiltinTask();
        ASSERT_FALSE(terminal5);
        EXPECT_EQ(terminal5.error(), Graphics::GpuCommandIrStreamReadStatus::Error);
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
    expectRecordError(s_ExpectedDualCount, s_CommandIrClearBufferOffset, [](Graphics::GraphicsBytes& bytes){
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
            s_ExpectedDualCount
        );
        WriteCommandIrPod(
            bytes,
            offsetof(Graphics::GpuCommandIrStreamHeader, commandBytes),
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

