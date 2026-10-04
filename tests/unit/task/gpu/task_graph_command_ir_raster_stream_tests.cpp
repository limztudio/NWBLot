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
static constexpr Graphics::GpuGraphPipelineId s_Pipeline{ .generation = s_CommandIrTask.generation, .index = 2u };


Graphics::GpuCommandIrRasterStateDesc RasterState(){
    Graphics::GpuCommandIrRasterStateDesc state;
    state.pipeline = s_Pipeline;
    state.colorAttachment = s_CommandIrDestination;
    state.viewport = Graphics::Viewport(0.f, 64.f, 0.f, 48.f, 0.f, 1.f);
    state.scissor = Graphics::Rect(4, 60, 3, 45);
    state.blendConstantColor = Graphics::Color(0.25f, 0.5f, 0.75f, 1.f);
    state.dynamicStencilRefValue = 7u;
    state.hasScissor = true;
    state.vertexBuffers.push_back(Graphics::GpuCommandIrRasterVertexOwner{
        .resource = s_CommandIrSource,
        .buffer = {},
        .slot = 3u,
        .offset = 16u,
    });
    state.indexResource = Graphics::GpuGraphResourceId{ .generation = s_CommandIrTask.generation, .index = 9u };
    state.indexFormat = Graphics::Format::R32_UINT;
    state.indexOffset = 4u;
    return state;
}

TEST(GpuCommandIrRasterStream, PreservesCapturedPushBytesAfterCallerMutationAcrossRasterRecords){
    TaskGraphTestUtils::TestArena testArena;
    Graphics::GpuCommandIrCapture capture(testArena.arena);
    Graphics::GpuCommandIrOwnedStream owned(testArena.arena);
    const Graphics::GpuCommandIrRasterStateDesc state = RasterState();
    u8 pushBytes[] = { 0x11u, 0x22u, 0x33u, 0x44u, 0x55u, 0x66u, 0x77u, 0x88u };
    const u8 expectedPush[] = { 0x11u, 0x22u, 0x33u, 0x44u, 0x55u, 0x66u, 0x77u, 0x88u };
    Graphics::DrawArguments draw;
    draw.setVertexCount(6u).setInstanceCount(2u).setStartIndexLocation(5u).setStartVertexLocation(3u).setStartInstanceLocation(1u);

    ASSERT_TRUE(capture.captureSetGraphicsState(s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, state));
    ASSERT_TRUE(capture.capturePushConstants(s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue,
        BinaryByteView{ pushBytes, sizeof(pushBytes) }));
    for(u8& value : pushBytes)
        value = 0u;
    ASSERT_TRUE(capture.captureDraw(s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, draw, true));
    ASSERT_TRUE(capture.captureEndRenderPass(s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue));
    ASSERT_TRUE(capture.exportOwned(owned));

    const BinaryByteView bytes = owned.bytes();
    Graphics::GpuCommandIrStreamHeader header;
    usize cursor = 0u;
    ASSERT_TRUE(ReadPOD(bytes, cursor, header));
    EXPECT_EQ(header.version, 5u);
    EXPECT_EQ(header.recordCount, 4u);
    EXPECT_TRUE(Graphics::ValidateGpuCommandIrStream(bytes).valid());

    Graphics::GpuCommandIrStreamReader reader(bytes);
    Graphics::GpuCommandIrDecodedRecord decoded;
    ASSERT_EQ(reader.next(decoded), Graphics::GpuCommandIrStreamReadStatus::Record);
    EXPECT_EQ(decoded.opcode, Graphics::GpuCommandIrWireOpcode::SetGraphicsState);
    EXPECT_EQ(decoded.raster.task, s_CommandIrTask);
    EXPECT_EQ(decoded.raster.packet, s_CommandIrPacket);
    EXPECT_EQ(decoded.raster.queue, s_CommandIrQueue);
    EXPECT_EQ(decoded.raster.pipeline, state.pipeline);
    EXPECT_EQ(decoded.raster.colorAttachment, state.colorAttachment);
    EXPECT_EQ(decoded.raster.viewport, state.viewport);
    EXPECT_EQ(decoded.raster.scissor, state.scissor);
    EXPECT_EQ(decoded.raster.blendConstantColor, state.blendConstantColor);
    ASSERT_EQ(decoded.raster.vertexBuffers.size(), 1u);
    EXPECT_EQ(decoded.raster.vertexBuffers[0].resource, s_CommandIrSource);
    EXPECT_EQ(decoded.raster.vertexBuffers[0].slot, 3u);
    EXPECT_EQ(decoded.raster.vertexBuffers[0].offset, 16u);
    EXPECT_EQ(decoded.raster.indexResource, state.indexResource);
    EXPECT_EQ(decoded.raster.indexFormat, Graphics::Format::R32_UINT);
    EXPECT_EQ(decoded.raster.indexOffset, 4u);

    ASSERT_EQ(reader.next(decoded), Graphics::GpuCommandIrStreamReadStatus::Record);
    EXPECT_EQ(decoded.opcode, Graphics::GpuCommandIrWireOpcode::SetPushConstants);
    ASSERT_EQ(decoded.raster.blobSizeBytes, sizeof(expectedPush));
    const BinaryByteView blob = reader.blobBytes();
    ASSERT_LE(decoded.raster.blobOffsetBytes, blob.size());
    ASSERT_LE(decoded.raster.blobSizeBytes, blob.size() - decoded.raster.blobOffsetBytes);
    EXPECT_EQ(GLB_MEMCMP(blob.data() + decoded.raster.blobOffsetBytes, expectedPush, sizeof(expectedPush)), 0);

    ASSERT_EQ(reader.next(decoded), Graphics::GpuCommandIrStreamReadStatus::Record);
    EXPECT_EQ(decoded.opcode, Graphics::GpuCommandIrWireOpcode::DrawIndexed);
    EXPECT_EQ(decoded.raster.drawArguments.vertexCount, draw.vertexCount);
    EXPECT_EQ(decoded.raster.drawArguments.instanceCount, draw.instanceCount);
    EXPECT_EQ(decoded.raster.drawArguments.startIndexLocation, draw.startIndexLocation);
    EXPECT_EQ(decoded.raster.drawArguments.startVertexLocation, draw.startVertexLocation);
    EXPECT_EQ(decoded.raster.drawArguments.startInstanceLocation, draw.startInstanceLocation);
    ASSERT_EQ(reader.next(decoded), Graphics::GpuCommandIrStreamReadStatus::Record);
    EXPECT_EQ(decoded.opcode, Graphics::GpuCommandIrWireOpcode::EndRenderPass);
    EXPECT_EQ(reader.next(decoded), Graphics::GpuCommandIrStreamReadStatus::End);
    EXPECT_TRUE(reader.validation().valid());
}

TEST(GpuCommandIrRasterStream, RejectsLateOutOfBlobPushAndEarlierVersion){
    TaskGraphTestUtils::TestArena testArena;
    Graphics::GpuCommandIrCapture capture(testArena.arena);
    const Graphics::GpuCommandIrRasterStateDesc state = RasterState();
    const u8 pushBytes[] = { 0x31u, 0x41u, 0x59u, 0x26u };
    ASSERT_TRUE(capture.captureSetGraphicsState(s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, state));
    ASSERT_TRUE(capture.capturePushConstants(s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue,
        BinaryByteView{ pushBytes, sizeof(pushBytes) }));
    Graphics::GraphicsBytes malformed(testArena.arena);
    CopyCommandIrBytes(malformed, capture.commandBytes());
    const usize secondRecord = sizeof(Graphics::GpuCommandIrStreamHeader)
        + sizeof(Graphics::GpuCommandIrSetGraphicsStateRecord);
    WriteCommandIrPod(malformed,
        secondRecord + offsetof(Graphics::GpuCommandIrSetPushConstantsRecord, blobOffsetBytes),
        Limit<u64>::s_Max);
    const Graphics::GpuCommandIrStreamValidationResult invalid = Graphics::ValidateGpuCommandIrStream(
        BinaryByteView{ malformed.data(), malformed.size() });
    EXPECT_EQ(invalid.error, Graphics::GpuCommandIrStreamValidationError::InvalidBlobRange);
    EXPECT_EQ(invalid.recordIndex, 1u);

    WriteCommandIrPod(malformed, offsetof(Graphics::GpuCommandIrStreamHeader, version), static_cast<u16>(4u));
    const Graphics::GpuCommandIrStreamValidationResult oldVersion = Graphics::ValidateGpuCommandIrStream(
        BinaryByteView{ malformed.data(), malformed.size() });
    EXPECT_EQ(oldVersion.error, Graphics::GpuCommandIrStreamValidationError::UnsupportedVersion);
}

TEST(GpuCommandIrRasterStream, RejectsCorruptedViewportBeforeExposingRasterRecord){
    TaskGraphTestUtils::TestArena testArena;
    Graphics::GpuCommandIrCapture capture(testArena.arena);
    const Graphics::GpuCommandIrRasterStateDesc state = RasterState();
    ASSERT_TRUE(capture.captureSetGraphicsState(s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, state));
    Graphics::GraphicsBytes malformed(testArena.arena);
    CopyCommandIrBytes(malformed, capture.commandBytes());
    const usize maxXOffset = sizeof(Graphics::GpuCommandIrStreamHeader)
        + offsetof(Graphics::GpuCommandIrSetGraphicsStateRecord, viewportMaxX);
    WriteCommandIrPod(malformed, maxXOffset, 0.f);
    Graphics::GpuCommandIrStreamReader reader(BinaryByteView{ malformed.data(), malformed.size() });
    Graphics::GpuCommandIrDecodedRecord decoded;
    decoded.opcode = Graphics::GpuCommandIrWireOpcode::Draw;
    EXPECT_EQ(reader.next(decoded), Graphics::GpuCommandIrStreamReadStatus::Error);
    EXPECT_EQ(decoded.opcode, Graphics::GpuCommandIrWireOpcode::Draw);
    EXPECT_EQ(reader.validation().error, Graphics::GpuCommandIrStreamValidationError::InvalidRecord);
    EXPECT_EQ(reader.validation().recordIndex, 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

