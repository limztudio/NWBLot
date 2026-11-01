// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"
#include "task_graph_command_ir_test_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_command_ir_upload_stream_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;

inline constexpr Core::GpuUploadBlobId s_BufferBlob{ .generation = s_CommandIrTask.generation, .index = 7u };
inline constexpr Core::GpuUploadBlobId s_TextureBlob{ .generation = s_CommandIrTask.generation, .index = 8u };
inline constexpr usize s_SecondUploadOffset = sizeof(Core::GpuCommandIrStreamHeader) + sizeof(Core::GpuCommandIrUploadBufferRecord);
inline constexpr usize s_TextureByteCount = 96u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 CaptureTwoUploads(const BinaryByteView bufferBytes, const BinaryByteView textureBytes, Core::GpuCommandIrCapture& capture){
    Core::TextureSlice slice;
    slice.setOrigin(0u, 0u, 0u).setSize(4u, 3u, 2u).setMipLevel(1u).setArraySlice(2u);
    return
        capture.captureUploadBuffer(s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, s_BufferBlob, s_CommandIrDestination, 32u, bufferBytes, Core::ResourceStates::ShaderResource) &&
        capture.captureUploadTexture(s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, s_TextureBlob, s_CommandIrDestination, slice, 16u, 48u, Core::TextureUploadAspect::Color, textureBytes, Core::ResourceStates::ShaderResource)
    ;
}


TEST(GpuCommandIrUploadStream, PreservesBufferAndPitchedTextureBlobsAfterCallerMutation){
    TestArena testArena;
    Core::GpuCommandIrCapture capture(testArena.arena);
    Array<u8, 5> bufferBytes{ 11u, 12u, 13u, 14u, 15u };
    Array<u8, s_TextureByteCount> textureBytes{};
    for(usize index = 0u; index < s_TextureByteCount; ++index)
        textureBytes[index] = static_cast<u8>(index + 1u);
    ASSERT_TRUE(CaptureTwoUploads(BinaryByteView{ bufferBytes.data(), bufferBytes.size() }, BinaryByteView{ textureBytes.data(), textureBytes.size() }, capture));

    bufferBytes[0] = 99u;
    textureBytes[0] = 99u;
    const BinaryByteView bytes = capture.commandBytes();
    const auto validation = Core::ValidateGpuCommandIrStream(bytes);
    ASSERT_TRUE(validation.valid());

    Core::GpuCommandIrStreamReader reader(bytes);
    const BinaryByteView blobs = reader.blobBytes();
    ASSERT_EQ(blobs.size(), bufferBytes.size() + textureBytes.size());
    ASSERT_NE(blobs.data(), nullptr);
    EXPECT_EQ(blobs.data()[0], 11u);
    EXPECT_EQ(blobs.data()[bufferBytes.size()], 1u);
}

TEST(GpuCommandIrUploadStream, LateBadBlobOffsetRejectsWithoutOverwritingTheFailingRecord){
    TestArena testArena;
    Core::GpuCommandIrCapture capture(testArena.arena);
    const Array<u8, 4> bufferBytes{ 1u, 2u, 3u, 4u };
    const Array<u8, s_TextureByteCount> textureBytes{};
    ASSERT_TRUE(CaptureTwoUploads(BinaryByteView{ bufferBytes.data(), bufferBytes.size() }, BinaryByteView{ textureBytes.data(), textureBytes.size() }, capture));
    Core::GraphicsBytes bytes(testArena.arena);
    CopyCommandIrBytes(capture.commandBytes(), bytes);
    WriteCommandIrPod(s_SecondUploadOffset + offsetof(Core::GpuCommandIrUploadTextureRecord, blobOffsetBytes), Limit<u64>::s_Max, bytes);
    const BinaryByteView malformed{ bytes.data(), bytes.size() };
    const auto validation = Core::ValidateGpuCommandIrStream(malformed);
    EXPECT_TRUE(validation.failed());
    EXPECT_EQ(validation.error, Core::GpuCommandIrStreamValidationError::InvalidBlobRange);
    EXPECT_EQ(validation.recordIndex, 1u);
    EXPECT_EQ(validation.byteOffset, s_SecondUploadOffset);

    Core::GpuCommandIrStreamReader reader(malformed);
    Expected<Core::GpuCommandIrBuiltinTaskRecord, Core::GpuCommandIrStreamReadStatus::Enum> output = MakeUnexpected(Core::GpuCommandIrStreamReadStatus::End);
    ASSERT_TRUE((output = reader.nextBuiltinTask()));
    EXPECT_EQ(output->opcode, Core::GpuCommandIrOpcode::UploadBuffer);

    const auto terminal1 = reader.nextBuiltinTask();
    ASSERT_FALSE(terminal1);
    EXPECT_EQ(terminal1.error(), Core::GpuCommandIrStreamReadStatus::Error);

    const auto terminal2 = reader.nextBuiltinTask();
    ASSERT_FALSE(terminal2);
    EXPECT_EQ(terminal2.error(), Core::GpuCommandIrStreamReadStatus::Error);
    EXPECT_TRUE(reader.validation().failed());
}

TEST(GpuCommandIrUploadStream, BlobSizeOverflowAndTruncationRejectTheSecondRecord){
    TestArena testArena;
    Core::GpuCommandIrCapture capture(testArena.arena);
    const Array<u8, 4> bufferBytes{ 1u, 2u, 3u, 4u };
    const Array<u8, s_TextureByteCount> textureBytes{};
    ASSERT_TRUE(CaptureTwoUploads(BinaryByteView{ bufferBytes.data(), bufferBytes.size() }, BinaryByteView{ textureBytes.data(), textureBytes.size() }, capture));

    Core::GraphicsBytes bytes(testArena.arena);
    CopyCommandIrBytes(capture.commandBytes(), bytes);
    WriteCommandIrPod(s_SecondUploadOffset + offsetof(Core::GpuCommandIrUploadTextureRecord, blobSizeBytes), Limit<u64>::s_Max, bytes);
    auto validation = Core::ValidateGpuCommandIrStream(BinaryByteView{ bytes.data(), bytes.size() });
    EXPECT_EQ(validation.error, Core::GpuCommandIrStreamValidationError::InvalidBlobRange);
    EXPECT_EQ(validation.recordIndex, 1u);

    CopyCommandIrBytes(capture.commandBytes(), bytes);
    bytes.resize(bytes.size() - 1u);
    WriteCommandIrPod(offsetof(Core::GpuCommandIrStreamHeader, blobBytes), static_cast<u64>(bufferBytes.size() + textureBytes.size() - 1u), bytes);
    validation = Core::ValidateGpuCommandIrStream(BinaryByteView{ bytes.data(), bytes.size() });
    EXPECT_EQ(validation.error, Core::GpuCommandIrStreamValidationError::InvalidBlobRange);
    EXPECT_EQ(validation.recordIndex, 1u);
    EXPECT_EQ(validation.byteOffset, s_SecondUploadOffset);

    CopyCommandIrBytes(capture.commandBytes(), bytes);
    bytes.resize(bytes.size() - 1u);
    validation = Core::ValidateGpuCommandIrStream(BinaryByteView{ bytes.data(), bytes.size() });
    EXPECT_EQ(validation.error, Core::GpuCommandIrStreamValidationError::PayloadSizeMismatch);
    EXPECT_EQ(validation.recordIndex, Limit<u64>::s_Max);
}

TEST(GpuCommandIrUploadStream, TruncatedLateRecordDoesNotPublishPartialOutput){
    TestArena testArena;
    Core::GpuCommandIrCapture capture(testArena.arena);
    const Array<u8, 4> uploadBytes{ 1u, 2u, 3u, 4u };
    ASSERT_TRUE(capture.captureClearBuffer(s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, s_CommandIrDestination, 0xdecafbadU));
    ASSERT_TRUE(capture.captureUploadBuffer(s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, s_BufferBlob, s_CommandIrDestination, 32u, BinaryByteView{ uploadBytes.data(), uploadBytes.size() }, Core::ResourceStates::ShaderResource));
    Core::GraphicsBytes bytes(testArena.arena);
    CopyCommandIrBytes(capture.commandBytes(), bytes);
    const usize secondOffset = sizeof(Core::GpuCommandIrStreamHeader) + sizeof(Core::GpuCommandIrClearBufferRecord);
    bytes.resize(secondOffset + sizeof(Core::GpuCommandIrHeader));
    WriteCommandIrPod(offsetof(Core::GpuCommandIrStreamHeader, commandBytes), static_cast<u64>(sizeof(Core::GpuCommandIrClearBufferRecord) + sizeof(Core::GpuCommandIrHeader)), bytes);
    WriteCommandIrPod(offsetof(Core::GpuCommandIrStreamHeader, blobBytes), u64(0u), bytes);
    const BinaryByteView malformed{ bytes.data(), bytes.size() };
    const auto validation = Core::ValidateGpuCommandIrStream(malformed);
    EXPECT_EQ(validation.error, Core::GpuCommandIrStreamValidationError::TruncatedRecord);
    EXPECT_EQ(validation.recordIndex, 1u);
    EXPECT_EQ(validation.byteOffset, secondOffset);

    Core::GpuCommandIrStreamReader reader(malformed);
    Expected<Core::GpuCommandIrBuiltinTaskRecord, Core::GpuCommandIrStreamReadStatus::Enum> output = MakeUnexpected(Core::GpuCommandIrStreamReadStatus::End);
    ASSERT_TRUE((output = reader.nextBuiltinTask()));
    EXPECT_EQ(output->opcode, Core::GpuCommandIrOpcode::ClearBuffer);

    const auto terminal3 = reader.nextBuiltinTask();
    ASSERT_FALSE(terminal3);
    EXPECT_EQ(terminal3.error(), Core::GpuCommandIrStreamReadStatus::Error);
}

TEST(GpuCommandIrUploadStream, EarlierWireVersionRejectsBeforeAnyRecordOrBlobIsExposed){
    TestArena testArena;
    Core::GpuCommandIrCapture capture(testArena.arena);
    const Array<u8, 4> uploadBytes{ 1u, 2u, 3u, 4u };
    ASSERT_TRUE(capture.captureUploadBuffer(s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, s_BufferBlob, s_CommandIrDestination, 32u, BinaryByteView{ uploadBytes.data(), uploadBytes.size() }, Core::ResourceStates::ShaderResource));
    Core::GraphicsBytes bytes(testArena.arena);
    CopyCommandIrBytes(capture.commandBytes(), bytes);
    WriteCommandIrPod(offsetof(Core::GpuCommandIrStreamHeaderPrefix, version), static_cast<u16>(Core::s_GpuCommandIrStreamVersion - 1u), bytes);
    const BinaryByteView oldVersion{ bytes.data(), bytes.size() };
    const auto validation = Core::ValidateGpuCommandIrStream(oldVersion);
    EXPECT_EQ(validation.error, Core::GpuCommandIrStreamValidationError::UnsupportedVersion);
    EXPECT_EQ(validation.recordIndex, Limit<u64>::s_Max);

    Core::GpuCommandIrStreamReader reader(oldVersion);

    const auto terminal4 = reader.nextBuiltinTask();
    ASSERT_FALSE(terminal4);
    EXPECT_EQ(terminal4.error(), Core::GpuCommandIrStreamReadStatus::Error);

    EXPECT_TRUE(reader.blobBytes().empty());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

