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


TEST(GpuCommandIrStreamReader, EmptyAndExhaustedStreamsReportEndWithoutProducingARecord){
    TestArena testArena;
    Core::GpuCommandIrCapture emptyCapture(testArena.arena);
    Core::GpuCommandIrStreamReader emptyReader(emptyCapture.commandBytes());

    const auto terminal1 = emptyReader.nextBuiltinTask();
    ASSERT_FALSE(terminal1);
    EXPECT_EQ(terminal1.error(), Core::GpuCommandIrStreamReadStatus::End);
    EXPECT_TRUE(emptyReader.validation().valid());

    Core::GpuCommandIrCapture capture(testArena.arena);
    ASSERT_TRUE(CaptureAllBuiltinCommandIrRecords(capture));
    Core::GpuCommandIrStreamReader reader(capture.commandBytes());
    for(u32 recordIndex = 0u; recordIndex < 4u; ++recordIndex)
        ASSERT_TRUE(reader.nextBuiltinTask());

    const auto terminal2 = reader.nextBuiltinTask();
    ASSERT_FALSE(terminal2);
    EXPECT_EQ(terminal2.error(), Core::GpuCommandIrStreamReadStatus::End);
    const auto terminal3 = reader.nextBuiltinTask();
    ASSERT_FALSE(terminal3);
    EXPECT_EQ(terminal3.error(), Core::GpuCommandIrStreamReadStatus::End);

    EXPECT_TRUE(reader.validation().complete);
    EXPECT_TRUE(reader.validation().valid());
    EXPECT_EQ(reader.validation().byteOffset, capture.commandBytes().size());
    EXPECT_EQ(reader.validation().recordIndex, 4u);
}

TEST(GpuCommandIrStreamReader, RejectsMalformedHeadersBeforeReadingRecords){
    TestArena testArena;
    Core::GpuCommandIrCapture capture(testArena.arena);
    ASSERT_TRUE(CaptureAllBuiltinCommandIrRecords(capture));
    const BinaryByteView validBytes = capture.commandBytes();

    const Core::GpuCommandIrStreamValidationResult nullData = Core::ValidateGpuCommandIrStream(BinaryByteView{ nullptr, 1u });
    EXPECT_EQ(nullData.error, Core::GpuCommandIrStreamValidationError::NullData);
    EXPECT_TRUE(nullData.complete);
    EXPECT_FALSE(nullData.valid());
    EXPECT_EQ(nullData.recordIndex, Limit<u64>::s_Max);

    const Core::GpuCommandIrStreamValidationResult truncatedHeader = Core::ValidateGpuCommandIrStream(BinaryByteView{ validBytes.data(), sizeof(Core::GpuCommandIrStreamHeader) - 1u });
    EXPECT_EQ(truncatedHeader.error, Core::GpuCommandIrStreamValidationError::TruncatedStreamHeader);
    EXPECT_TRUE(truncatedHeader.complete);
    EXPECT_FALSE(truncatedHeader.valid());

    const auto expectHeaderError = [&testArena, validBytes](const auto& mutate, const Core::GpuCommandIrStreamValidationError::Enum expectedError){
        Core::GraphicsBytes corruptedBytes(testArena.arena);
        CopyCommandIrBytes(validBytes, corruptedBytes);
        mutate(corruptedBytes);
        const Core::GpuCommandIrStreamValidationResult result = Core::ValidateGpuCommandIrStream(BinaryByteView{ corruptedBytes.data(), corruptedBytes.size() });
        EXPECT_EQ(result.error, expectedError);
        EXPECT_TRUE(result.complete);
        EXPECT_TRUE(result.failed());
        EXPECT_FALSE(result.valid());
        EXPECT_EQ(result.byteOffset, 0u);
        EXPECT_EQ(result.recordIndex, Limit<u64>::s_Max);
    };

    expectHeaderError([](Core::GraphicsBytes& bytes)noexcept(noexcept(WriteCommandIrPod(offsetof(Core::GpuCommandIrStreamHeader, magic), 0u, bytes))){
        WriteCommandIrPod(offsetof(Core::GpuCommandIrStreamHeader, magic), 0u, bytes);
    }, Core::GpuCommandIrStreamValidationError::InvalidMagic
    );
    for(u16 version = 0u; version < Core::s_GpuCommandIrStreamVersion; ++version){
        expectHeaderError([version](Core::GraphicsBytes& bytes)noexcept(noexcept(WriteCommandIrPod(offsetof(Core::GpuCommandIrStreamHeader, version), version, bytes))){
            WriteCommandIrPod(offsetof(Core::GpuCommandIrStreamHeader, version), version, bytes);
        }, Core::GpuCommandIrStreamValidationError::UnsupportedVersion
        );
    }
    expectHeaderError([](Core::GraphicsBytes& bytes)noexcept(noexcept(WriteCommandIrPod(offsetof(Core::GpuCommandIrStreamHeader, version), static_cast<u16>(Core::s_GpuCommandIrStreamVersion + 1u), bytes))){
        WriteCommandIrPod(offsetof(Core::GpuCommandIrStreamHeader, version), static_cast<u16>(Core::s_GpuCommandIrStreamVersion + 1u), bytes);
    }, Core::GpuCommandIrStreamValidationError::UnsupportedVersion
    );
    expectHeaderError([](Core::GraphicsBytes& bytes)noexcept(noexcept(WriteCommandIrPod(offsetof(Core::GpuCommandIrStreamHeader, version), Limit<u16>::s_Max, bytes))){
        WriteCommandIrPod(offsetof(Core::GpuCommandIrStreamHeader, version), Limit<u16>::s_Max, bytes);
    }, Core::GpuCommandIrStreamValidationError::UnsupportedVersion
    );
    expectHeaderError([](Core::GraphicsBytes& bytes)noexcept(noexcept(WriteCommandIrPod(offsetof(Core::GpuCommandIrStreamHeader, reserved), static_cast<u16>(1u), bytes))){
        WriteCommandIrPod(offsetof(Core::GpuCommandIrStreamHeader, reserved), static_cast<u16>(1u), bytes);
    }, Core::GpuCommandIrStreamValidationError::InvalidHeaderReserved
    );
    expectHeaderError([](Core::GraphicsBytes& bytes)noexcept(noexcept(WriteCommandIrPod(offsetof(Core::GpuCommandIrStreamHeader, commandBytes), static_cast<u64>(bytes.size()), bytes))){
        WriteCommandIrPod(offsetof(Core::GpuCommandIrStreamHeader, commandBytes), static_cast<u64>(bytes.size()), bytes);
    }, Core::GpuCommandIrStreamValidationError::PayloadSizeMismatch
    );
    expectHeaderError([](Core::GraphicsBytes& bytes)noexcept(noexcept(WriteCommandIrPod(offsetof(Core::GpuCommandIrStreamHeader, graphGeneration), 0u, bytes))){
        WriteCommandIrPod(offsetof(Core::GpuCommandIrStreamHeader, graphGeneration), 0u, bytes);
    }, Core::GpuCommandIrStreamValidationError::InvalidGraphGeneration
    );
    expectHeaderError([](Core::GraphicsBytes& bytes)noexcept(noexcept(WriteCommandIrPod(offsetof(Core::GpuCommandIrStreamHeader, planGeneration), 0u, bytes))){
        WriteCommandIrPod(offsetof(Core::GpuCommandIrStreamHeader, planGeneration), 0u, bytes);
    }, Core::GpuCommandIrStreamValidationError::InvalidPlanGeneration
    );
    expectHeaderError([](Core::GraphicsBytes& bytes)noexcept(noexcept(WriteCommandIrPod(offsetof(Core::GpuCommandIrStreamHeader, recordCount), 0u, bytes))){
        WriteCommandIrPod(offsetof(Core::GpuCommandIrStreamHeader, recordCount), 0u, bytes);
    }, Core::GpuCommandIrStreamValidationError::InvalidGraphGeneration
    );
    expectHeaderError([](Core::GraphicsBytes& bytes)noexcept(noexcept(WriteCommandIrPod(offsetof(Core::GpuCommandIrStreamHeader, recordCount), 64u, bytes))){
        WriteCommandIrPod(offsetof(Core::GpuCommandIrStreamHeader, recordCount), 64u, bytes);
    }, Core::GpuCommandIrStreamValidationError::InvalidRecordCount
    );
}

TEST(GpuCommandIrStreamReader, RejectsMalformedRecordsWithoutProducingPartialValues){
    TestArena testArena;
    Core::GpuCommandIrCapture capture(testArena.arena);
    ASSERT_TRUE(CaptureAllBuiltinCommandIrRecords(capture));
    const BinaryByteView validBytes = capture.commandBytes();

    const auto expectRecordError = [&testArena, validBytes](const usize validPrefixCount, const usize expectedByteOffset, const auto& mutate, const Core::GpuCommandIrStreamValidationError::Enum expectedError){
        Core::GraphicsBytes corruptedBytes(testArena.arena);
        CopyCommandIrBytes(validBytes, corruptedBytes);
        mutate(corruptedBytes);

        Core::GpuCommandIrStreamReader reader(BinaryByteView{ corruptedBytes.data(), corruptedBytes.size() });
        for(usize recordIndex = 0u; recordIndex < validPrefixCount; ++recordIndex)
            ASSERT_TRUE(reader.nextBuiltinTask());

        const auto terminal4 = reader.nextBuiltinTask();
        ASSERT_FALSE(terminal4);
        EXPECT_EQ(terminal4.error(), Core::GpuCommandIrStreamReadStatus::Error);

        EXPECT_EQ(reader.validation().error, expectedError);
        EXPECT_TRUE(reader.validation().complete);
        EXPECT_TRUE(reader.validation().failed());
        EXPECT_EQ(reader.validation().byteOffset, expectedByteOffset);
        EXPECT_EQ(reader.validation().recordIndex, validPrefixCount);
        const auto terminal5 = reader.nextBuiltinTask();
        ASSERT_FALSE(terminal5);
        EXPECT_EQ(terminal5.error(), Core::GpuCommandIrStreamReadStatus::Error);
    };

    expectRecordError(1u, s_CommandIrCopyTextureOffset, [](Core::GraphicsBytes& bytes)noexcept(noexcept(WriteCommandIrPod(s_CommandIrCopyTextureOffset + offsetof(Core::GpuCommandIrHeader, byteSize), static_cast<u16>(3u), bytes))){
        WriteCommandIrPod(s_CommandIrCopyTextureOffset + offsetof(Core::GpuCommandIrHeader, byteSize), static_cast<u16>(3u), bytes);
    }, Core::GpuCommandIrStreamValidationError::InvalidRecordSize
    );
    expectRecordError(1u, s_CommandIrCopyTextureOffset, [](Core::GraphicsBytes& bytes)noexcept(noexcept(WriteCommandIrPod(s_CommandIrCopyTextureOffset + offsetof(Core::GpuCommandIrHeader, byteSize), Limit<u16>::s_Max, bytes))){
        WriteCommandIrPod(s_CommandIrCopyTextureOffset + offsetof(Core::GpuCommandIrHeader, byteSize), Limit<u16>::s_Max, bytes);
    }, Core::GpuCommandIrStreamValidationError::InvalidRecordSize
    );
    expectRecordError(1u, s_CommandIrCopyTextureOffset, [](Core::GraphicsBytes& bytes)noexcept(noexcept(WriteCommandIrPod(s_CommandIrCopyTextureOffset + offsetof(Core::GpuCommandIrHeader, opcode), Core::GpuCommandIrWireOpcode::SetGraphicsState, bytes))){
        WriteCommandIrPod(s_CommandIrCopyTextureOffset + offsetof(Core::GpuCommandIrHeader, opcode), Core::GpuCommandIrWireOpcode::SetGraphicsState, bytes);
    }, Core::GpuCommandIrStreamValidationError::UnsupportedOpcode
    );
    expectRecordError(1u, s_CommandIrCopyTextureOffset, [](Core::GraphicsBytes& bytes)noexcept(noexcept(WriteCommandIrPod(s_CommandIrCopyTextureOffset + offsetof(Core::GpuCommandIrHeader, opcode), Core::GpuCommandIrWireOpcode::Count, bytes))){
        WriteCommandIrPod(s_CommandIrCopyTextureOffset + offsetof(Core::GpuCommandIrHeader, opcode), Core::GpuCommandIrWireOpcode::Count, bytes);
    }, Core::GpuCommandIrStreamValidationError::UnsupportedOpcode
    );
    expectRecordError(0u, s_CommandIrCopyBufferOffset, [](Core::GraphicsBytes& bytes)noexcept(noexcept(WriteCommandIrPod(s_CommandIrCopyBufferOffset + offsetof(Core::GpuCommandIrCopyBufferRecord, dataSizeBytes), 0u, bytes))){
        WriteCommandIrPod(s_CommandIrCopyBufferOffset + offsetof(Core::GpuCommandIrCopyBufferRecord, dataSizeBytes), 0u, bytes);
    }, Core::GpuCommandIrStreamValidationError::InvalidRecord
    );
    expectRecordError(0u, s_CommandIrCopyBufferOffset, [](Core::GraphicsBytes& bytes)noexcept(noexcept(WriteCommandIrPod(s_CommandIrCopyBufferOffset + offsetof(Core::GpuCommandIrCopyBufferRecord, sourceResourceIndex), Limit<u32>::s_Max, bytes))){
        WriteCommandIrPod(s_CommandIrCopyBufferOffset + offsetof(Core::GpuCommandIrCopyBufferRecord, sourceResourceIndex), Limit<u32>::s_Max, bytes);
    }, Core::GpuCommandIrStreamValidationError::InvalidRecord
    );
    expectRecordError(0u, s_CommandIrCopyBufferOffset, [](Core::GraphicsBytes& bytes)noexcept(
        noexcept(WriteCommandIrPod(s_CommandIrCopyBufferOffset + offsetof(Core::GpuCommandIrCopyBufferRecord, context) + offsetof(Core::GpuCommandIrRecordContext, queueDeviceGeneration), static_cast<u16>(0u), bytes))
    ){
        WriteCommandIrPod(s_CommandIrCopyBufferOffset + offsetof(Core::GpuCommandIrCopyBufferRecord, context) + offsetof(Core::GpuCommandIrRecordContext, queueDeviceGeneration), static_cast<u16>(0u), bytes);
    }, Core::GpuCommandIrStreamValidationError::InvalidRecord
    );
    expectRecordError(1u, s_CommandIrCopyTextureOffset, [](Core::GraphicsBytes& bytes)noexcept(noexcept(WriteCommandIrPod(s_CommandIrCopyTextureOffset + offsetof(Core::GpuCommandIrCopyTextureRecord, destinationResourceIndex), Limit<u32>::s_Max, bytes))){
        WriteCommandIrPod(s_CommandIrCopyTextureOffset + offsetof(Core::GpuCommandIrCopyTextureRecord, destinationResourceIndex), Limit<u32>::s_Max, bytes);
    }, Core::GpuCommandIrStreamValidationError::InvalidRecord
    );
    expectRecordError(2u, s_CommandIrClearBufferOffset, [](Core::GraphicsBytes& bytes)noexcept(
        noexcept(WriteCommandIrPod(s_CommandIrClearBufferOffset + offsetof(Core::GpuCommandIrClearBufferRecord, destinationResourceIndex), Limit<u32>::s_Max, bytes))
    ){
        WriteCommandIrPod(s_CommandIrClearBufferOffset + offsetof(Core::GpuCommandIrClearBufferRecord, destinationResourceIndex), Limit<u32>::s_Max, bytes);
    }, Core::GpuCommandIrStreamValidationError::InvalidRecord
    );
    expectRecordError(3u, s_CommandIrClearTextureOffset, [](Core::GraphicsBytes& bytes)noexcept(
        noexcept(WriteCommandIrPod(s_CommandIrClearTextureOffset + offsetof(Core::GpuCommandIrClearTextureRecord, destinationSubresources) + offsetof(Core::GpuCommandIrTextureSubresourceSet, numMipLevels), 0u, bytes))
    ){
        WriteCommandIrPod(s_CommandIrClearTextureOffset + offsetof(Core::GpuCommandIrClearTextureRecord, destinationSubresources) + offsetof(Core::GpuCommandIrTextureSubresourceSet, numMipLevels), 0u, bytes);
    }, Core::GpuCommandIrStreamValidationError::InvalidRecord
    );
    expectRecordError(3u, s_CommandIrClearTextureOffset, [](Core::GraphicsBytes& bytes)noexcept(
        noexcept(WriteCommandIrPod(s_CommandIrClearTextureOffset + offsetof(Core::GpuCommandIrClearTextureRecord, clearTextureValueType), static_cast<u8>(Core::GpuClearTextureTaskValueType::Count), bytes))
    ){
        WriteCommandIrPod(s_CommandIrClearTextureOffset + offsetof(Core::GpuCommandIrClearTextureRecord, clearTextureValueType), static_cast<u8>(Core::GpuClearTextureTaskValueType::Count), bytes);
    }, Core::GpuCommandIrStreamValidationError::InvalidRecord
    );
    expectRecordError(3u, s_CommandIrClearTextureOffset, [](Core::GraphicsBytes& bytes)noexcept(
        noexcept(WriteCommandIrPod(s_CommandIrClearTextureOffset + offsetof(Core::GpuCommandIrClearTextureRecord, clearFlags), static_cast<Core::GpuCommandIrClearTextureFlag::Mask>(4u), bytes))
    ){
        WriteCommandIrPod(s_CommandIrClearTextureOffset + offsetof(Core::GpuCommandIrClearTextureRecord, clearFlags), static_cast<Core::GpuCommandIrClearTextureFlag::Mask>(4u), bytes);
    }, Core::GpuCommandIrStreamValidationError::InvalidRecord
    );
    expectRecordError(3u, s_CommandIrClearTextureOffset, [](Core::GraphicsBytes& bytes)noexcept(
        noexcept(WriteCommandIrPod(s_CommandIrClearTextureOffset + offsetof(Core::GpuCommandIrClearTextureRecord, clearFlags), Core::GpuCommandIrClearTextureFlag::None, bytes))
    ){
        WriteCommandIrPod(s_CommandIrClearTextureOffset + offsetof(Core::GpuCommandIrClearTextureRecord, clearFlags), Core::GpuCommandIrClearTextureFlag::None, bytes);
    }, Core::GpuCommandIrStreamValidationError::InvalidRecord
    );
    expectRecordError(3u, s_CommandIrClearTextureOffset, [](Core::GraphicsBytes& bytes)noexcept(
        noexcept(WriteCommandIrPod(s_CommandIrClearTextureOffset + offsetof(Core::GpuCommandIrClearTextureRecord, clearTextureValueType), static_cast<u8>(Core::GpuClearTextureTaskValueType::Float), bytes))
        && noexcept(WriteCommandIrPod(s_CommandIrClearTextureOffset + offsetof(Core::GpuCommandIrClearTextureRecord, clearFlags), Core::GpuCommandIrClearTextureFlag::ClearDepth, bytes))
    ){
        WriteCommandIrPod(s_CommandIrClearTextureOffset + offsetof(Core::GpuCommandIrClearTextureRecord, clearTextureValueType), static_cast<u8>(Core::GpuClearTextureTaskValueType::Float), bytes);
        WriteCommandIrPod(s_CommandIrClearTextureOffset + offsetof(Core::GpuCommandIrClearTextureRecord, clearFlags), Core::GpuCommandIrClearTextureFlag::ClearDepth, bytes);
    }, Core::GpuCommandIrStreamValidationError::InvalidRecord
    );
    expectRecordError(3u, s_CommandIrClearTextureOffset, [](Core::GraphicsBytes& bytes)noexcept(noexcept(WriteCommandIrPod(s_CommandIrClearTextureOffset + offsetof(Core::GpuCommandIrClearTextureRecord, reserved), static_cast<u8>(1u), bytes))){
        WriteCommandIrPod(s_CommandIrClearTextureOffset + offsetof(Core::GpuCommandIrClearTextureRecord, reserved), static_cast<u8>(1u), bytes);
    }, Core::GpuCommandIrStreamValidationError::InvalidRecord
    );
    expectRecordError(3u, s_CommandIrClearTextureOffset, [](Core::GraphicsBytes& bytes)noexcept(noexcept(WriteCommandIrPod(offsetof(Core::GpuCommandIrStreamHeader, recordCount), 3u, bytes))){
        WriteCommandIrPod(offsetof(Core::GpuCommandIrStreamHeader, recordCount), 3u, bytes);
    }, Core::GpuCommandIrStreamValidationError::TrailingPayload
    );
    expectRecordError(4u, validBytes.size(), [](Core::GraphicsBytes& bytes)noexcept(noexcept(WriteCommandIrPod(offsetof(Core::GpuCommandIrStreamHeader, recordCount), 5u, bytes))){
        WriteCommandIrPod(offsetof(Core::GpuCommandIrStreamHeader, recordCount), 5u, bytes);
    }, Core::GpuCommandIrStreamValidationError::TruncatedRecord
    );
    expectRecordError(1u, s_CommandIrCopyTextureOffset, [](Core::GraphicsBytes& bytes){
        bytes.resize(s_CommandIrCopyTextureOffset + sizeof(Core::GpuCommandIrHeader));
        WriteCommandIrPod(offsetof(Core::GpuCommandIrStreamHeader, recordCount), 2u, bytes);
        WriteCommandIrPod(offsetof(Core::GpuCommandIrStreamHeader, commandBytes), static_cast<u64>(bytes.size() - sizeof(Core::GpuCommandIrStreamHeader)), bytes);
    }, Core::GpuCommandIrStreamValidationError::TruncatedRecord
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

