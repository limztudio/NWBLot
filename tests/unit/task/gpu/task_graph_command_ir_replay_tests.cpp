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


namespace __hidden_task_graph_command_ir_replay_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuCommandIrReplay, RequiresExactPhysicalQueueBeyondBroadQueueClass){
    const Graphics::GpuPhysicalQueueInfo packetQueue = GraphicsQueue();
    const Graphics::GpuPhysicalQueueInfo sameClassOtherQueue = GraphicsQueue(1u);

    Graphics::CommandListParameters exactDescription;
    exactDescription.setPhysicalQueue(packetQueue.id);
    EXPECT_EQ(
        Graphics::GpuCommandIrDetail::ValidateReplayCommandListQueue(
            exactDescription,
            packetQueue.id,
            packetQueue.queueClass
        ),
        Graphics::GpuCommandIrReplayError::None
    );

    Graphics::CommandListParameters sameClassWrongDescription;
    sameClassWrongDescription.setPhysicalQueue(sameClassOtherQueue.id);
    EXPECT_EQ(
        Graphics::GpuCommandIrDetail::ValidateReplayCommandListQueue(
            sameClassWrongDescription,
            packetQueue.id,
            packetQueue.queueClass
        ),
        Graphics::GpuCommandIrReplayError::CommandListQueueMismatch
    );

    Graphics::CommandListParameters wrongClassDescription;
    wrongClassDescription.setPhysicalQueue(packetQueue.id);
    wrongClassDescription.queueType = Graphics::CommandQueue::Compute;
    EXPECT_EQ(
        Graphics::GpuCommandIrDetail::ValidateReplayCommandListQueue(
            wrongClassDescription,
            packetQueue.id,
            packetQueue.queueClass
        ),
        Graphics::GpuCommandIrReplayError::CommandListQueueMismatch
    );
}

TEST(GpuCommandIrReplay, PreflightsTheWholeStreamAgainstTheCompiledPacketBeforeLowering){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId source = AddBufferMetadata(
        graph,
        Name("tests/command_ir_replay/source"),
        "Replay Source"
    );
    const Graphics::GpuGraphResourceId destination = AddBufferMetadata(
        graph,
        Name("tests/command_ir_replay/destination"),
        "Replay Destination"
    );
    ASSERT_TRUE(source.valid());
    ASSERT_TRUE(destination.valid());

    const Graphics::GpuTaskResourceUse uses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = source,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopySource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = destination,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc desc;
    desc
        .setIdentity(Name("tests/command_ir_replay/copy"))
        .setMarkerLabel("Replay Copy")
        .setResourceUses(uses, LengthOf(uses))
    ;
    const Graphics::GpuTaskId task = graph.addTask(desc, Graphics::GpuTaskCommandRequirements{ Graphics::GpuQueueCapability::Transfer });
    ASSERT_TRUE(task.valid());
    const Graphics::GpuTaskId secondDependencies[] = { task };
    Graphics::GpuTaskDesc secondDesc = desc;
    secondDesc
        .setIdentity(Name("tests/command_ir_replay/copy_second"))
        .setMarkerLabel("Replay Copy Second")
        .setDependencies(secondDependencies, LengthOf(secondDependencies))
    ;
    const Graphics::GpuTaskId secondTask = graph.addTask(
        secondDesc,
        Graphics::GpuTaskCommandRequirements{ Graphics::GpuQueueCapability::Transfer }
    );
    ASSERT_TRUE(secondTask.valid());

    SingleQueueCompile singleQueueCompile(testArena);
    ASSERT_TRUE(singleQueueCompile.compile(graph));
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(singleQueueCompile.compiledGraph);

    const Graphics::GpuSubmissionPacketId packet = compiledPlan.packetForTask(task);
    const Graphics::GpuSubmissionPacketId secondPacket = compiledPlan.packetForTask(secondTask);
    ASSERT_TRUE(packet.valid());
    ASSERT_TRUE(secondPacket.valid());
    ASSERT_NE(secondPacket, packet);
    const Graphics::GpuPhysicalQueueId queue = compiledPlan.packet(packet).plan->queue;
    ASSERT_EQ(compiledPlan.packet(secondPacket).plan->queue, queue);

    Graphics::GpuCommandIrCapture capture(testArena.arena);
    ASSERT_TRUE(capture.captureCopyBuffer(task, packet, queue, source, 0u, destination, 0u, 4u));
    ASSERT_TRUE(capture.captureCopyBuffer(secondTask, secondPacket, queue, source, 0u, destination, 0u, 4u));
    ASSERT_EQ(capture.recordCount(), s_ExpectedDualCount);
    const Graphics::GpuCommandIrReplayResult validContext = Graphics::PreflightGpuCommandIrPacket(
        capture.commandBytes(),
        declarations,
        compiledPlan,
        packet
    );
    // Metadata-only imports cannot be lowered, but preflight has already established the stream, graph,
    // packet, queue, task order, resource kinds, and declared CopySource/CopyDest uses before that boundary.
    EXPECT_EQ(validContext.error, Graphics::GpuCommandIrReplayError::MissingBackendResource);
    EXPECT_TRUE(validContext.streamValidation.valid());
    EXPECT_EQ(validContext.recordIndex, 0u);

    // A normal capture concatenates packet bodies. Selecting the second packet must skip the first record rather
    // than rejecting the full frame artifact before its requested packet is reached.
    const Graphics::GpuCommandIrReplayResult secondPacketContext = Graphics::PreflightGpuCommandIrPacket(
        capture.commandBytes(),
        declarations,
        compiledPlan,
        secondPacket
    );
    EXPECT_EQ(secondPacketContext.error, Graphics::GpuCommandIrReplayError::MissingBackendResource);
    EXPECT_TRUE(secondPacketContext.streamValidation.valid());
    EXPECT_EQ(secondPacketContext.recordIndex, 1u);

    const Graphics::GpuCommandIrReplayResult invalidPacket = Graphics::PreflightGpuCommandIrPacket(
        capture.commandBytes(),
        declarations,
        compiledPlan,
        Graphics::GpuSubmissionPacketId{ .generation = packet.generation, .index = Limit<u32>::s_Max - 1u }
    );
    EXPECT_EQ(invalidPacket.error, Graphics::GpuCommandIrReplayError::InvalidPacket);
    EXPECT_TRUE(invalidPacket.streamValidation.valid());

    Graphics::GpuCommandIrCapture wrongQueueCapture(testArena.arena);
    ASSERT_TRUE(wrongQueueCapture.captureCopyBuffer(
        task,
        packet,
        Graphics::GpuPhysicalQueueId{ .index = static_cast<u16>(queue.index + 1u), .deviceGeneration = queue.deviceGeneration },
        source,
        0u,
        destination,
        0u,
        4u
    ));
    const Graphics::GpuCommandIrReplayResult wrongQueue = Graphics::PreflightGpuCommandIrPacket(
        wrongQueueCapture.commandBytes(),
        declarations,
        compiledPlan,
        packet
    );
    EXPECT_EQ(wrongQueue.error, Graphics::GpuCommandIrReplayError::RecordQueueMismatch);
    EXPECT_TRUE(wrongQueue.streamValidation.valid());
    EXPECT_EQ(wrongQueue.recordIndex, 0u);

    const Graphics::GpuCommandIrReplayResult malformed = Graphics::PreflightGpuCommandIrPacket(
        BinaryByteView{},
        declarations,
        compiledPlan,
        packet
    );
    EXPECT_EQ(malformed.error, Graphics::GpuCommandIrReplayError::InvalidStream);
    EXPECT_EQ(malformed.streamValidation.error, Graphics::GpuCommandIrStreamValidationError::TruncatedStreamHeader);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

