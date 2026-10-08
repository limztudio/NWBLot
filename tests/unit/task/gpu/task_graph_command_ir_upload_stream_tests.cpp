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

inline constexpr Graphics::GpuUploadBlobId s_BufferBlob{ .generation = s_CommandIrTask.generation, .index = 7u };
inline constexpr Graphics::GpuUploadBlobId s_TextureBlob{ .generation = s_CommandIrTask.generation, .index = 8u };
inline constexpr usize s_SecondUploadOffset = sizeof(Graphics::GpuCommandIrStreamHeader)
    + sizeof(Graphics::GpuCommandIrUploadBufferRecord);
inline constexpr usize s_TextureByteCount = 96u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool CaptureTwoUploads(Graphics::GpuCommandIrCapture& capture,
    const BinaryByteView bufferBytes, const BinaryByteView textureBytes
){
    Graphics::TextureSlice slice;
    slice
        .setOrigin(0u, 0u, 0u)
        .setSize(4u, 3u, 2u)
        .setMipLevel(1u)
        .setArraySlice(2u)
    ;
    return capture.captureUploadBuffer(
        s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, s_BufferBlob, s_CommandIrDestination,
        32u, bufferBytes, Graphics::ResourceStates::ShaderResource
    ) && capture.captureUploadTexture(
        s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, s_TextureBlob, s_CommandIrDestination,
        slice, 16u, 48u, Graphics::TextureUploadAspect::Color, textureBytes,
        Graphics::ResourceStates::ShaderResource
    );
}


TEST(GpuCommandIrUploadStream, PreservesBufferAndPitchedTextureBlobsAfterCallerMutation){
    TestArena testArena;
    Graphics::GpuCommandIrCapture capture(testArena.arena);
    u8 bufferBytes[]{ 11u, 12u, 13u, 14u, 15u };
    u8 textureBytes[s_TextureByteCount]{};
    for(usize index = 0u; index < s_TextureByteCount; ++index)
        textureBytes[index] = static_cast<u8>(index + 1u);
    ASSERT_TRUE(CaptureTwoUploads(
        capture, BinaryByteView{ bufferBytes, sizeof(bufferBytes) },
        BinaryByteView{ textureBytes, sizeof(textureBytes) }
    ));

    bufferBytes[0] = 99u;
    textureBytes[0] = 99u;
    const BinaryByteView bytes = capture.commandBytes();
    const auto validation = Graphics::ValidateGpuCommandIrStream(bytes);
    ASSERT_TRUE(validation.valid());

    Graphics::GpuCommandIrStreamReader reader(bytes);
    const BinaryByteView blobs = reader.blobBytes();
    ASSERT_EQ(blobs.size(), sizeof(bufferBytes) + sizeof(textureBytes));
    ASSERT_NE(blobs.data(), nullptr);
    EXPECT_EQ(blobs.data()[0], 11u);
    EXPECT_EQ(blobs.data()[sizeof(bufferBytes)], 1u);

}

TEST(GpuCommandIrUploadStream, LateBadBlobOffsetRejectsWithoutOverwritingTheFailingRecord){
    TestArena testArena;
    Graphics::GpuCommandIrCapture capture(testArena.arena);
    const u8 bufferBytes[]{ 1u, 2u, 3u, 4u };
    const u8 textureBytes[s_TextureByteCount]{};
    ASSERT_TRUE(CaptureTwoUploads(
        capture, BinaryByteView{ bufferBytes, sizeof(bufferBytes) },
        BinaryByteView{ textureBytes, sizeof(textureBytes) }
    ));
    Graphics::GraphicsBytes bytes(testArena.arena);
    CopyCommandIrBytes(bytes, capture.commandBytes());
    WriteCommandIrPod(
        bytes, s_SecondUploadOffset + offsetof(Graphics::GpuCommandIrUploadTextureRecord, blobOffsetBytes),
        Limit<u64>::s_Max
    );
    const BinaryByteView malformed{ bytes.data(), bytes.size() };
    const auto validation = Graphics::ValidateGpuCommandIrStream(malformed);
    EXPECT_TRUE(validation.failed());
    EXPECT_EQ(validation.error, Graphics::GpuCommandIrStreamValidationError::InvalidBlobRange);
    EXPECT_EQ(validation.recordIndex, 1u);
    EXPECT_EQ(validation.byteOffset, s_SecondUploadOffset);

    Graphics::GpuCommandIrStreamReader reader(malformed);
    Expected<Graphics::GpuCommandIrBuiltinTaskRecord, Graphics::GpuCommandIrStreamReadStatus::Enum> output = MakeUnexpected(Graphics::GpuCommandIrStreamReadStatus::End);
    ASSERT_TRUE((output = reader.nextBuiltinTask()));
    EXPECT_EQ(output->opcode, Graphics::GpuCommandIrOpcode::UploadBuffer);


    const auto terminal1 = reader.nextBuiltinTask();
    ASSERT_FALSE(terminal1);
    EXPECT_EQ(terminal1.error(), Graphics::GpuCommandIrStreamReadStatus::Error);


    const auto terminal2 = reader.nextBuiltinTask();
    ASSERT_FALSE(terminal2);
    EXPECT_EQ(terminal2.error(), Graphics::GpuCommandIrStreamReadStatus::Error);
    EXPECT_TRUE(reader.validation().failed());
}

TEST(GpuCommandIrUploadStream, BlobSizeOverflowAndTruncationRejectTheSecondRecord){
    TestArena testArena;
    Graphics::GpuCommandIrCapture capture(testArena.arena);
    const u8 bufferBytes[]{ 1u, 2u, 3u, 4u };
    const u8 textureBytes[s_TextureByteCount]{};
    ASSERT_TRUE(CaptureTwoUploads(
        capture, BinaryByteView{ bufferBytes, sizeof(bufferBytes) },
        BinaryByteView{ textureBytes, sizeof(textureBytes) }
    ));

    Graphics::GraphicsBytes bytes(testArena.arena);
    CopyCommandIrBytes(bytes, capture.commandBytes());
    WriteCommandIrPod(
        bytes, s_SecondUploadOffset + offsetof(Graphics::GpuCommandIrUploadTextureRecord, blobSizeBytes),
        Limit<u64>::s_Max
    );
    auto validation = Graphics::ValidateGpuCommandIrStream(BinaryByteView{ bytes.data(), bytes.size() });
    EXPECT_EQ(validation.error, Graphics::GpuCommandIrStreamValidationError::InvalidBlobRange);
    EXPECT_EQ(validation.recordIndex, 1u);

    CopyCommandIrBytes(bytes, capture.commandBytes());
    bytes.resize(bytes.size() - 1u);
    WriteCommandIrPod(
        bytes, offsetof(Graphics::GpuCommandIrStreamHeader, blobBytes),
        static_cast<u64>(sizeof(bufferBytes) + sizeof(textureBytes) - 1u)
    );
    validation = Graphics::ValidateGpuCommandIrStream(BinaryByteView{ bytes.data(), bytes.size() });
    EXPECT_EQ(validation.error, Graphics::GpuCommandIrStreamValidationError::InvalidBlobRange);
    EXPECT_EQ(validation.recordIndex, 1u);
    EXPECT_EQ(validation.byteOffset, s_SecondUploadOffset);

    CopyCommandIrBytes(bytes, capture.commandBytes());
    bytes.resize(bytes.size() - 1u);
    validation = Graphics::ValidateGpuCommandIrStream(BinaryByteView{ bytes.data(), bytes.size() });
    EXPECT_EQ(validation.error, Graphics::GpuCommandIrStreamValidationError::PayloadSizeMismatch);
    EXPECT_EQ(validation.recordIndex, Limit<u64>::s_Max);
}

TEST(GpuCommandIrUploadStream, TruncatedLateRecordDoesNotPublishPartialOutput){
    TestArena testArena;
    Graphics::GpuCommandIrCapture capture(testArena.arena);
    const u8 uploadBytes[]{ 1u, 2u, 3u, 4u };
    ASSERT_TRUE(capture.captureClearBuffer(
        s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, s_CommandIrDestination, 0xdecafbadU
    ));
    ASSERT_TRUE(capture.captureUploadBuffer(
        s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, s_BufferBlob, s_CommandIrDestination,
        32u, BinaryByteView{ uploadBytes, sizeof(uploadBytes) }, Graphics::ResourceStates::ShaderResource
    ));
    Graphics::GraphicsBytes bytes(testArena.arena);
    CopyCommandIrBytes(bytes, capture.commandBytes());
    const usize secondOffset = sizeof(Graphics::GpuCommandIrStreamHeader)
        + sizeof(Graphics::GpuCommandIrClearBufferRecord);
    bytes.resize(secondOffset + sizeof(Graphics::GpuCommandIrHeader));
    WriteCommandIrPod(
        bytes, offsetof(Graphics::GpuCommandIrStreamHeader, commandBytes),
        static_cast<u64>(sizeof(Graphics::GpuCommandIrClearBufferRecord) + sizeof(Graphics::GpuCommandIrHeader))
    );
    WriteCommandIrPod(bytes, offsetof(Graphics::GpuCommandIrStreamHeader, blobBytes), u64(0u));
    const BinaryByteView malformed{ bytes.data(), bytes.size() };
    const auto validation = Graphics::ValidateGpuCommandIrStream(malformed);
    EXPECT_EQ(validation.error, Graphics::GpuCommandIrStreamValidationError::TruncatedRecord);
    EXPECT_EQ(validation.recordIndex, 1u);
    EXPECT_EQ(validation.byteOffset, secondOffset);

    Graphics::GpuCommandIrStreamReader reader(malformed);
    Expected<Graphics::GpuCommandIrBuiltinTaskRecord, Graphics::GpuCommandIrStreamReadStatus::Enum> output = MakeUnexpected(Graphics::GpuCommandIrStreamReadStatus::End);
    ASSERT_TRUE((output = reader.nextBuiltinTask()));
    EXPECT_EQ(output->opcode, Graphics::GpuCommandIrOpcode::ClearBuffer);


    const auto terminal3 = reader.nextBuiltinTask();
    ASSERT_FALSE(terminal3);
    EXPECT_EQ(terminal3.error(), Graphics::GpuCommandIrStreamReadStatus::Error);


}

TEST(GpuCommandIrUploadStream, EarlierWireVersionRejectsBeforeAnyRecordOrBlobIsExposed){
    TestArena testArena;
    Graphics::GpuCommandIrCapture capture(testArena.arena);
    const u8 uploadBytes[]{ 1u, 2u, 3u, 4u };
    ASSERT_TRUE(capture.captureUploadBuffer(
        s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, s_BufferBlob, s_CommandIrDestination,
        32u, BinaryByteView{ uploadBytes, sizeof(uploadBytes) }, Graphics::ResourceStates::ShaderResource
    ));
    Graphics::GraphicsBytes bytes(testArena.arena);
    CopyCommandIrBytes(bytes, capture.commandBytes());
    WriteCommandIrPod(
        bytes, offsetof(Graphics::GpuCommandIrStreamHeaderPrefix, version),
        static_cast<u16>(Graphics::s_GpuCommandIrStreamVersion - 1u)
    );
    const BinaryByteView oldVersion{ bytes.data(), bytes.size() };
    const auto validation = Graphics::ValidateGpuCommandIrStream(oldVersion);
    EXPECT_EQ(validation.error, Graphics::GpuCommandIrStreamValidationError::UnsupportedVersion);
    EXPECT_EQ(validation.recordIndex, Limit<u64>::s_Max);

    Graphics::GpuCommandIrStreamReader reader(oldVersion);


    const auto terminal4 = reader.nextBuiltinTask();
    ASSERT_FALSE(terminal4);
    EXPECT_EQ(terminal4.error(), Graphics::GpuCommandIrStreamReadStatus::Error);

    EXPECT_TRUE(reader.blobBytes().empty());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

