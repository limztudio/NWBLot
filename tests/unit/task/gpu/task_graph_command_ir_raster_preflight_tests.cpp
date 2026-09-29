// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

#include <core/task/gpu/capture/command_ir_raster.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_command_ir_raster_preflight_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;


TEST(GpuCommandIrRasterPreflight, DrawWithoutHeapBindingRejectsWholePacket){
    TaskGraphTestUtils::TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId color = AddTextureMetadata(
        graph,
        Name("tests/command_ir_raster/color"),
        "Raster Color",
        Graphics::ResourceStates::RenderTarget
    );
    const Graphics::GpuGraphPipelineId pipeline = AddPipelineMetadata(
        graph,
        Name("tests/command_ir_raster/pipeline"),
        "Raster Pipeline",
        Graphics::GpuGraphPipelineType::Graphics
    );
    ASSERT_TRUE(color.valid());
    ASSERT_TRUE(pipeline.valid());
    const Graphics::GpuTaskResourceUse use{
        .resource = color,
        .range = {},
        .requiredState = Graphics::ResourceStates::RenderTarget,
        .access = Graphics::GpuTaskResourceAccess::Write,
    };
    Graphics::GpuTaskDesc description;
    description
        .setIdentity(Name("tests/command_ir_raster/draw"))
        .setMarkerLabel("Raster Draw")
        .setResourceUses(&use, 1u)
    ;
    const Graphics::GpuTaskId task = graph.addTask(description, GraphicsCommands());
    ASSERT_TRUE(task.valid());
    SingleQueueCompile compiled(testArena);
    ASSERT_TRUE(compiled.compile(graph));
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
    const Graphics::GpuCompiledGraph::ReadView plan(compiled.compiledGraph);
    const Graphics::GpuSubmissionPacketId packet = plan.packetForTask(task);
    ASSERT_TRUE(packet.valid());
    const Graphics::GpuPhysicalQueueId queue = plan.packet(packet).plan->queue;

    Graphics::GpuCommandIrRasterStateDesc state;
    state.pipeline = pipeline;
    state.colorAttachment = color;
    state.viewport = Graphics::Viewport(64.f, 48.f);
    const u8 pushBytes[] = { 0x11u, 0x22u, 0x33u, 0x44u };
    Graphics::GpuCommandIrCapture validCapture(testArena.arena);
    Graphics::GpuCommandIrOwnedStream valid(testArena.arena);
    ASSERT_TRUE(validCapture.captureSetGraphicsState(task, packet, queue, state));
    ASSERT_TRUE(validCapture.captureEndRenderPass(task, packet, queue));
    ASSERT_TRUE(validCapture.exportOwned(valid));
    const Graphics::GpuCommandIrReplayResult accepted = Graphics::PreflightGpuCommandIrPacket(
        valid, declarations, plan, packet);
    EXPECT_EQ(accepted.error, Graphics::GpuCommandIrReplayError::None);
    EXPECT_TRUE(accepted.streamValidation.valid());

    Graphics::GpuCommandIrCapture invalidCapture(testArena.arena);
    Graphics::GpuCommandIrOwnedStream invalid(testArena.arena);
    ASSERT_TRUE(invalidCapture.captureSetGraphicsState(task, packet, queue, state));
    ASSERT_TRUE(invalidCapture.capturePushConstants(task, packet, queue, BinaryByteView{ pushBytes, sizeof(pushBytes) }));
    ASSERT_TRUE(invalidCapture.captureDraw(task, packet, queue, Graphics::DrawArguments().setVertexCount(3u), false));
    ASSERT_TRUE(invalidCapture.captureEndRenderPass(task, packet, queue));
    ASSERT_TRUE(invalidCapture.exportOwned(invalid));
    const Graphics::GpuCommandIrReplayResult rejected = Graphics::PreflightGpuCommandIrPacket(
        invalid, declarations, plan, packet);
    EXPECT_EQ(rejected.error, Graphics::GpuCommandIrReplayError::InvalidRasterDraw);
    EXPECT_EQ(rejected.recordIndex, 2u);
    EXPECT_TRUE(rejected.streamValidation.valid());

    const Graphics::GpuCommandIrReplayResult raw = Graphics::PreflightGpuCommandIrPacket(
        valid.bytes(), declarations, plan, packet);
    EXPECT_EQ(raw.error, Graphics::GpuCommandIrReplayError::MissingOwnedSidecar);
    EXPECT_TRUE(raw.streamValidation.valid());

    Graphics::GpuCommandIrDetail::RasterReplayState graphState;
    Graphics::GpuCommandIrRasterTaskRecord stateRecord;
    stateRecord.opcode = Graphics::GpuCommandIrWireOpcode::SetGraphicsState;
    stateRecord.task = task;
    stateRecord.pipeline = pipeline;
    stateRecord.colorAttachment = color;
    stateRecord.viewport = state.viewport;
    ASSERT_EQ(
        Graphics::GpuCommandIrDetail::ValidateRasterGraphOperation(
            stateRecord, declarations, declarations.taskAt(task.index), graphState),
        Graphics::GpuCommandIrReplayError::None
    );
    Graphics::GpuCommandIrRasterTaskRecord heapRecord;
    heapRecord.opcode = Graphics::GpuCommandIrWireOpcode::BindGraphicsHeap;
    heapRecord.task = task;
    heapRecord.pipeline = pipeline;
    ASSERT_EQ(
        Graphics::GpuCommandIrDetail::ValidateRasterGraphOperation(
            heapRecord, declarations, declarations.taskAt(task.index), graphState),
        Graphics::GpuCommandIrReplayError::None
    );
    Graphics::GpuCommandIrRasterTaskRecord pushRecord;
    pushRecord.opcode = Graphics::GpuCommandIrWireOpcode::SetPushConstants;
    pushRecord.task = task;
    pushRecord.blobSizeBytes = sizeof(pushBytes);
    ASSERT_EQ(
        Graphics::GpuCommandIrDetail::ValidateRasterGraphOperation(
            pushRecord, declarations, declarations.taskAt(task.index), graphState),
        Graphics::GpuCommandIrReplayError::None
    );
    Graphics::GpuCommandIrRasterTaskRecord indexedDraw;
    indexedDraw.opcode = Graphics::GpuCommandIrWireOpcode::DrawIndexed;
    indexedDraw.task = task;
    indexedDraw.drawArguments.setVertexCount(3u);
    EXPECT_EQ(
        Graphics::GpuCommandIrDetail::ValidateRasterGraphOperation(
            indexedDraw, declarations, declarations.taskAt(task.index), graphState),
        Graphics::GpuCommandIrReplayError::InvalidRasterDraw
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

