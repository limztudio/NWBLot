// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"
#include "task_graph_command_ir_test_utils.h"

#include <core/task/gpu/capture/command_ir_raster.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_command_ir_raster_stream_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
static constexpr Core::GpuGraphPipelineId s_Pipeline{ .generation = s_CommandIrTask.generation, .index = 2u };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Core::GpuCommandIrRasterStateDesc RasterState()noexcept{
    Core::GpuCommandIrRasterStateDesc state;
    state.pipeline = s_Pipeline;
    state.colorAttachment = s_CommandIrDestination;
    state.viewport = Core::Viewport(0.f, 64.f, 0.f, 48.f, 0.f, 1.f);
    state.scissor = Core::Rect(4, 60, 3, 45);
    state.blendConstantColor = Core::Color(0.25f, 0.5f, 0.75f, 1.f);
    state.dynamicStencilRefValue = 7u;
    state.hasScissor = true;
    state.vertexBuffers.push_back(Core::GpuCommandIrRasterVertexOwner{ .resource = s_CommandIrSource, .buffer = {}, .slot = 3u, .offset = 16u, });
    state.indexResource = Core::GpuGraphResourceId{ .generation = s_CommandIrTask.generation, .index = 9u };
    state.indexFormat = Core::Format::R32_UINT;
    state.indexOffset = 4u;
    return state;
}

TEST(GpuCommandIrRasterStream, PreservesCapturedPushBytesAfterCallerMutationAcrossRasterRecords){
    TaskGraphTestUtils::TestArena testArena;
    Core::GpuCommandIrCapture capture(testArena.arena);
    const Core::GpuCommandIrRasterStateDesc state = RasterState();
    Array<u8, 8> pushBytes = { 0x11u, 0x22u, 0x33u, 0x44u, 0x55u, 0x66u, 0x77u, 0x88u };
    const Array<u8, 8> expectedPush = { 0x11u, 0x22u, 0x33u, 0x44u, 0x55u, 0x66u, 0x77u, 0x88u };
    Core::DrawArguments draw;
    draw.setVertexCount(6u).setInstanceCount(2u).setStartIndexLocation(5u).setStartVertexLocation(3u).setStartInstanceLocation(1u);

    ASSERT_TRUE(capture.captureSetGraphicsState(s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, state));
    ASSERT_TRUE(capture.capturePushConstants(s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, BinaryByteView{ pushBytes.data(), sizeof(pushBytes) }));
    for(u8& value : pushBytes)
        value = 0u;
    ASSERT_TRUE(capture.captureDraw(s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, draw, true));
    ASSERT_TRUE(capture.captureEndRenderPass(s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue));
    const auto owned = capture.exportOwned(testArena.arena);
    ASSERT_TRUE(owned);

    const BinaryByteView bytes = owned->bytes();
    EXPECT_TRUE(Core::ValidateGpuCommandIrStream(bytes).valid());

    Core::GpuCommandIrStreamReader reader(bytes);
    Expected<Core::GpuCommandIrDecodedRecord, Core::GpuCommandIrStreamReadStatus::Enum> decoded = MakeUnexpected(Core::GpuCommandIrStreamReadStatus::End);
    ASSERT_TRUE((decoded = reader.next()));
    ASSERT_TRUE((decoded = reader.next()));
    ASSERT_EQ(decoded->raster.blobSizeBytes, sizeof(expectedPush));
    const BinaryByteView blob = reader.blobBytes();
    ASSERT_LE(decoded->raster.blobOffsetBytes, blob.size());
    ASSERT_LE(decoded->raster.blobSizeBytes, blob.size() - decoded->raster.blobOffsetBytes);
    EXPECT_EQ(NWB_MEMCMP(blob.data() + decoded->raster.blobOffsetBytes, expectedPush.data(), sizeof(expectedPush)), 0);

}

TEST(GpuCommandIrRasterStream, RejectsLateOutOfBlobPushAndEarlierVersion){
    TaskGraphTestUtils::TestArena testArena;
    Core::GpuCommandIrCapture capture(testArena.arena);
    const Core::GpuCommandIrRasterStateDesc state = RasterState();
    const Array<u8, 4> pushBytes = { 0x31u, 0x41u, 0x59u, 0x26u };
    ASSERT_TRUE(capture.captureSetGraphicsState(s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, state));
    ASSERT_TRUE(capture.capturePushConstants(s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, BinaryByteView{ pushBytes.data(), sizeof(pushBytes) }));
    Core::GraphicsBytes malformed(testArena.arena);
    CopyCommandIrBytes(capture.commandBytes(), malformed);
    const usize secondRecord = sizeof(Core::GpuCommandIrStreamHeader) + sizeof(Core::GpuCommandIrSetGraphicsStateRecord);
    WriteCommandIrPod(secondRecord + offsetof(Core::GpuCommandIrSetPushConstantsRecord, blobOffsetBytes), Limit<u64>::s_Max, malformed);
    const Core::GpuCommandIrStreamValidationResult invalid = Core::ValidateGpuCommandIrStream(BinaryByteView{ malformed.data(), malformed.size() });
    EXPECT_EQ(invalid.error, Core::GpuCommandIrStreamValidationError::InvalidBlobRange);
    EXPECT_EQ(invalid.recordIndex, 1u);

    WriteCommandIrPod(offsetof(Core::GpuCommandIrStreamHeader, version), static_cast<u16>(4u), malformed);
    const Core::GpuCommandIrStreamValidationResult oldVersion = Core::ValidateGpuCommandIrStream(BinaryByteView{ malformed.data(), malformed.size() });
    EXPECT_EQ(oldVersion.error, Core::GpuCommandIrStreamValidationError::UnsupportedVersion);
}

TEST(GpuCommandIrRasterStream, RejectsCorruptedViewportBeforeExposingRasterRecord){
    TaskGraphTestUtils::TestArena testArena;
    Core::GpuCommandIrCapture capture(testArena.arena);
    const Core::GpuCommandIrRasterStateDesc state = RasterState();
    ASSERT_TRUE(capture.captureSetGraphicsState(s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, state));
    Core::GraphicsBytes malformed(testArena.arena);
    CopyCommandIrBytes(capture.commandBytes(), malformed);
    const usize maxXOffset = sizeof(Core::GpuCommandIrStreamHeader) + offsetof(Core::GpuCommandIrSetGraphicsStateRecord, viewportMaxX);
    WriteCommandIrPod(maxXOffset, 0.f, malformed);
    Core::GpuCommandIrStreamReader reader(BinaryByteView{ malformed.data(), malformed.size() });

    const auto terminal1 = reader.next();
    ASSERT_FALSE(terminal1);
    EXPECT_EQ(terminal1.error(), Core::GpuCommandIrStreamReadStatus::Error);
    EXPECT_EQ(reader.validation().error, Core::GpuCommandIrStreamValidationError::InvalidRecord);
    EXPECT_EQ(reader.validation().recordIndex, 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

