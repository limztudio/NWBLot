// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

#include <tests/unit/task/gpu/case_types.adhd.h>

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
    Core::GpuTaskGraph graph(testArena.arena);
    const auto colorResult = AddTextureMetadata(graph, Name(TaskGraphTestUtils::s_GpuCaseCommandIrRasterColor), "Raster Color", Core::ResourceStates::RenderTarget);
    ASSERT_TRUE(colorResult);
    const Core::GpuGraphResourceId color = *colorResult;
    const auto pipelineResult = AddPipelineMetadata(graph, Name(TaskGraphTestUtils::s_GpuCaseCommandIrRasterPipeline), "Raster Pipeline", Core::GpuGraphPipelineType::Graphics);
    ASSERT_TRUE(pipelineResult);
    const Core::GpuGraphPipelineId pipeline = *pipelineResult;
    ASSERT_TRUE(color.valid());
    ASSERT_TRUE(pipeline.valid());
    const Core::GpuTaskResourceUse use{ .resource = color, .range = {}, .requiredState = Core::ResourceStates::RenderTarget, .access = Core::GpuTaskResourceAccess::Write, };
    Core::GpuTaskDesc description;
    description.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseCommandIrRasterDraw)).setMarkerLabel("Raster Draw").setResourceUses(Span(&use, 1u));
    const auto taskResult = graph.addTask(description, GraphicsCommands());
    ASSERT_TRUE(taskResult);
    const Core::GpuTaskId task = *taskResult;
    ASSERT_TRUE(task.valid());
    SingleQueueCompile compiled(testArena);
    ASSERT_TRUE(compiled.compile(graph));
    const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
    const Core::GpuCompiledGraph::ReadView plan(compiled.compiledGraph);
    const Core::GpuSubmissionPacketId packet = plan.packetForTask(task);
    ASSERT_TRUE(packet.valid());
    const Core::GpuPhysicalQueueId queue = plan.packet(packet).plan->queue;

    Core::GpuCommandIrRasterStateDesc state;
    state.pipeline = pipeline;
    state.colorAttachment = color;
    state.viewport = Core::Viewport(64.f, 48.f);
    const Array<u8, 4> pushBytes = { 0x11u, 0x22u, 0x33u, 0x44u };
    Core::GpuCommandIrCapture validCapture(testArena.arena);
    ASSERT_TRUE(validCapture.captureSetGraphicsState(task, packet, queue, state));
    ASSERT_TRUE(validCapture.captureEndRenderPass(task, packet, queue));
    const auto valid = validCapture.exportOwned(testArena.arena);
    ASSERT_TRUE(valid);
    const Core::GpuCommandIrReplayResult accepted = Core::PreflightGpuCommandIrPacket(*valid, declarations, plan, packet);
    EXPECT_EQ(accepted.error, Core::GpuCommandIrReplayError::None);
    EXPECT_TRUE(accepted.streamValidation.valid());

    // Converting INT32_MAX to f32 rounds up beyond the scissor range.
    for(const b8 vertical : { false, true }){
        for(const b8 overLimit : { false, true }){
            for(const b8 explicitScissor : { false, true }){
                Core::GpuCommandIrRasterStateDesc boundary = state;
                const f32 maximum = overLimit ? 2147483648.f : 2147483520.f;
                boundary.viewport = vertical ? Core::Viewport(0.f, 64.f, 0.f, maximum, 0.f, 1.f) : Core::Viewport(0.f, maximum, 0.f, 48.f, 0.f, 1.f);
                boundary.hasScissor = explicitScissor;
                boundary.scissor = Core::Rect(0, 1, 0, 1);
                Core::GpuCommandIrCapture boundaryCapture(testArena.arena);
                ASSERT_TRUE(boundaryCapture.captureSetGraphicsState(task, packet, queue, boundary));
                ASSERT_TRUE(boundaryCapture.captureEndRenderPass(task, packet, queue));
                const auto boundaryStream = boundaryCapture.exportOwned(testArena.arena);
                ASSERT_TRUE(boundaryStream);
                const auto boundaryResult = Core::PreflightGpuCommandIrPacket(*boundaryStream, declarations, plan, packet);
                EXPECT_EQ(boundaryResult.error, overLimit && !explicitScissor ? Core::GpuCommandIrReplayError::InvalidRasterState : Core::GpuCommandIrReplayError::None);
            }
        }
    }

    Core::GpuCommandIrCapture invalidCapture(testArena.arena);
    ASSERT_TRUE(invalidCapture.captureSetGraphicsState(task, packet, queue, state));
    ASSERT_TRUE(invalidCapture.capturePushConstants(task, packet, queue, BinaryByteView{ pushBytes.data(), sizeof(pushBytes) }));
    ASSERT_TRUE(invalidCapture.captureDraw(task, packet, queue, Core::DrawArguments().setVertexCount(3u), false));
    ASSERT_TRUE(invalidCapture.captureEndRenderPass(task, packet, queue));
    const auto invalid = invalidCapture.exportOwned(testArena.arena);
    ASSERT_TRUE(invalid);
    const Core::GpuCommandIrReplayResult rejected = Core::PreflightGpuCommandIrPacket(*invalid, declarations, plan, packet);
    EXPECT_EQ(rejected.error, Core::GpuCommandIrReplayError::InvalidRasterDraw);
    EXPECT_EQ(rejected.recordIndex, 2u);
    EXPECT_TRUE(rejected.streamValidation.valid());

    const Core::GpuCommandIrReplayResult raw = Core::PreflightGpuCommandIrPacket(valid->bytes(), declarations, plan, packet);
    EXPECT_EQ(raw.error, Core::GpuCommandIrReplayError::MissingOwnedSidecar);
    EXPECT_TRUE(raw.streamValidation.valid());

    Core::GpuCommandIrDetail::RasterReplayState graphState;
    Core::GpuCommandIrRasterTaskRecord stateRecord;
    stateRecord.opcode = Core::GpuCommandIrWireOpcode::SetGraphicsState;
    stateRecord.task = task;
    stateRecord.pipeline = pipeline;
    stateRecord.colorAttachment = color;
    stateRecord.viewport = state.viewport;
    ASSERT_EQ(Core::GpuCommandIrDetail::ValidateRasterGraphOperation(stateRecord, declarations, declarations.taskAt(task.index), graphState), Core::GpuCommandIrReplayError::None);
    Core::GpuCommandIrRasterTaskRecord heapRecord;
    heapRecord.opcode = Core::GpuCommandIrWireOpcode::BindGraphicsHeap;
    heapRecord.task = task;
    heapRecord.pipeline = pipeline;
    ASSERT_EQ(Core::GpuCommandIrDetail::ValidateRasterGraphOperation(heapRecord, declarations, declarations.taskAt(task.index), graphState), Core::GpuCommandIrReplayError::None);
    Core::GpuCommandIrRasterTaskRecord pushRecord;
    pushRecord.opcode = Core::GpuCommandIrWireOpcode::SetPushConstants;
    pushRecord.task = task;
    pushRecord.blobSizeBytes = sizeof(pushBytes);
    ASSERT_EQ(Core::GpuCommandIrDetail::ValidateRasterGraphOperation(pushRecord, declarations, declarations.taskAt(task.index), graphState), Core::GpuCommandIrReplayError::None);
    Core::GpuCommandIrRasterTaskRecord indexedDraw;
    indexedDraw.opcode = Core::GpuCommandIrWireOpcode::DrawIndexed;
    indexedDraw.task = task;
    indexedDraw.drawArguments.setVertexCount(3u);
    EXPECT_EQ(Core::GpuCommandIrDetail::ValidateRasterGraphOperation(indexedDraw, declarations, declarations.taskAt(task.index), graphState), Core::GpuCommandIrReplayError::InvalidRasterDraw);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

