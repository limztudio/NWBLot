// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"
#include "task_graph_command_ir_test_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_command_ir_replay_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuCommandIrReplay, RequiresExactPhysicalQueueBeyondBroadQueueClass){
    const Core::GpuPhysicalQueueInfo packetQueue = GraphicsQueue();
    const Core::GpuPhysicalQueueInfo sameClassOtherQueue = GraphicsQueue(1u);

    Core::CommandListParameters exactDescription;
    exactDescription.setPhysicalQueue(packetQueue.id);
    EXPECT_EQ(Core::GpuCommandIrDetail::ValidateReplayCommandListQueue(exactDescription, packetQueue.id, packetQueue.queueClass), Core::GpuCommandIrReplayError::None);

    Core::CommandListParameters sameClassWrongDescription;
    sameClassWrongDescription.setPhysicalQueue(sameClassOtherQueue.id);
    EXPECT_EQ(Core::GpuCommandIrDetail::ValidateReplayCommandListQueue(sameClassWrongDescription, packetQueue.id, packetQueue.queueClass), Core::GpuCommandIrReplayError::CommandListQueueMismatch);

    Core::CommandListParameters wrongClassDescription;
    wrongClassDescription.setPhysicalQueue(packetQueue.id);
    wrongClassDescription.queueType = Core::CommandQueue::Compute;
    EXPECT_EQ(Core::GpuCommandIrDetail::ValidateReplayCommandListQueue(wrongClassDescription, packetQueue.id, packetQueue.queueClass), Core::GpuCommandIrReplayError::CommandListQueueMismatch);
}

TEST(GpuCommandIrReplay, PreflightsTheWholeStreamAgainstTheCompiledPacketBeforeLowering){
    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto sourceResult = AddBufferMetadata(graph, Name("tests/command_ir_replay/source"), "Replay Source");
    ASSERT_TRUE(sourceResult);
    const Core::GpuGraphResourceId source = *sourceResult;
    const auto destinationResult = AddBufferMetadata(graph, Name("tests/command_ir_replay/destination"), "Replay Destination");
    ASSERT_TRUE(destinationResult);
    const Core::GpuGraphResourceId destination = *destinationResult;
    ASSERT_TRUE(source.valid());
    ASSERT_TRUE(destination.valid());

    const Array<Core::GpuTaskResourceUse, 2> uses = {
        Core::GpuTaskResourceUse{ .resource = source, .range = {}, .requiredState = Core::ResourceStates::CopySource, .access = Core::GpuTaskResourceAccess::Read, },
        Core::GpuTaskResourceUse{ .resource = destination, .range = {}, .requiredState = Core::ResourceStates::CopyDest, .access = Core::GpuTaskResourceAccess::Write, },
    };
    Core::GpuTaskDesc desc;
    desc.setIdentity(Name("tests/command_ir_replay/copy")).setMarkerLabel("Replay Copy").setResourceUses(uses);
    const auto taskResult = graph.addTask(desc, Core::GpuTaskCommandRequirements{ Core::GpuQueueCapability::Transfer });
    ASSERT_TRUE(taskResult);
    const Core::GpuTaskId task = *taskResult;
    ASSERT_TRUE(task.valid());
    const Array<Core::GpuTaskId, 1> secondDependencies = { task };
    Core::GpuTaskDesc secondDesc = desc;
    secondDesc.setIdentity(Name("tests/command_ir_replay/copy_second")).setMarkerLabel("Replay Copy Second").setDependencies(secondDependencies);
    const auto secondTaskResult = graph.addTask(secondDesc, Core::GpuTaskCommandRequirements{ Core::GpuQueueCapability::Transfer });
    ASSERT_TRUE(secondTaskResult);
    const Core::GpuTaskId secondTask = *secondTaskResult;
    ASSERT_TRUE(secondTask.valid());

    SingleQueueCompile singleQueueCompile(testArena);
    ASSERT_TRUE(singleQueueCompile.compile(graph));
    const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
    const Core::GpuCompiledGraph::ReadView compiledPlan(singleQueueCompile.compiledGraph);

    const Core::GpuSubmissionPacketId packet = compiledPlan.packetForTask(task);
    const Core::GpuSubmissionPacketId secondPacket = compiledPlan.packetForTask(secondTask);
    ASSERT_TRUE(packet.valid());
    ASSERT_TRUE(secondPacket.valid());
    ASSERT_NE(secondPacket, packet);
    const Core::GpuPhysicalQueueId queue = compiledPlan.packet(packet).plan->queue;
    ASSERT_EQ(compiledPlan.packet(secondPacket).plan->queue, queue);

    Core::GpuCommandIrCapture capture(testArena.arena);
    ASSERT_TRUE(capture.captureCopyBuffer(task, packet, queue, source, 0u, destination, 0u, 4u));
    ASSERT_TRUE(capture.captureCopyBuffer(secondTask, secondPacket, queue, source, 0u, destination, 0u, 4u));
    ASSERT_EQ(capture.recordCount(), 2u);
    const Core::GpuCommandIrReplayResult validContext = Core::PreflightGpuCommandIrPacket(capture.commandBytes(), declarations, compiledPlan, packet);
    // Metadata-only imports never lower. Preflight already set stream, graph, packet, queue, order, kinds, uses.
    EXPECT_EQ(validContext.error, Core::GpuCommandIrReplayError::MissingBackendResource);
    EXPECT_TRUE(validContext.streamValidation.valid());
    EXPECT_EQ(validContext.recordIndex, 0u);

    // Selecting the second packet skips the unlowerable first packet.
    const Core::GpuCommandIrReplayResult secondPacketContext = Core::PreflightGpuCommandIrPacket(capture.commandBytes(), declarations, compiledPlan, secondPacket);
    EXPECT_EQ(secondPacketContext.error, Core::GpuCommandIrReplayError::MissingBackendResource);
    EXPECT_TRUE(secondPacketContext.streamValidation.valid());
    EXPECT_EQ(secondPacketContext.recordIndex, 1u);

    const Core::GpuCommandIrReplayResult invalidPacket = Core::PreflightGpuCommandIrPacket(capture.commandBytes(), declarations, compiledPlan, Core::GpuSubmissionPacketId{ .generation = packet.generation, .index = Limit<u32>::s_Max - 1u });
    EXPECT_EQ(invalidPacket.error, Core::GpuCommandIrReplayError::InvalidPacket);
    EXPECT_TRUE(invalidPacket.streamValidation.valid());

    Core::GpuCommandIrCapture wrongQueueCapture(testArena.arena);
    ASSERT_TRUE(wrongQueueCapture.captureCopyBuffer(task, packet, Core::GpuPhysicalQueueId{ .index = static_cast<u16>(queue.index + 1u), .deviceGeneration = queue.deviceGeneration }, source, 0u, destination, 0u, 4u));
    const Core::GpuCommandIrReplayResult wrongQueue = Core::PreflightGpuCommandIrPacket(wrongQueueCapture.commandBytes(), declarations, compiledPlan, packet);
    EXPECT_EQ(wrongQueue.error, Core::GpuCommandIrReplayError::RecordQueueMismatch);
    EXPECT_TRUE(wrongQueue.streamValidation.valid());
    EXPECT_EQ(wrongQueue.recordIndex, 0u);

    const Core::GpuCommandIrReplayResult malformed = Core::PreflightGpuCommandIrPacket(BinaryByteView{}, declarations, compiledPlan, packet);
    EXPECT_EQ(malformed.error, Core::GpuCommandIrReplayError::InvalidStream);
    EXPECT_EQ(malformed.streamValidation.error, Core::GpuCommandIrStreamValidationError::TruncatedStreamHeader);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

