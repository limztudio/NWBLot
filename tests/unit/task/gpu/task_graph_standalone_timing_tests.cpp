// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_standalone_timing_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;

[[nodiscard]] static Graphics::GpuTaskSchedulingHint BoundaryScheduling(){
    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    return scheduling;
}

[[nodiscard]] static bool HasPacketDependency(
    const Graphics::GpuCompiledPacketView& consumer,
    const Graphics::GpuSubmissionPacketId producer
){
    if(!consumer.valid())
        return false;
    for(u32 index = 0u; index < consumer.plan->dependencyCount; ++index){
        if(consumer.dependencies[index].producer == producer)
            return true;
    }
    return false;
}

static void CheckStandalonePresentationTiming(const bool withContributor){
    SCOPED_TRACE(withContributor ? "with contributor" : "without contributor");
    TaskGraphTestUtils::TestArena testArena;
    Graphics::GraphicsAllocator graphicsAllocator(testArena.arena);
    Core::CpuTaskScheduler cpuScheduler(0u);
    Graphics::GraphicsBackend::VulkanContext context(graphicsAllocator, cpuScheduler, 1u);
    Graphics::GraphicsBackend::VulkanAllocator allocator(context);
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), DedicatedTransferQueue() };
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues) };
    const Graphics::GpuGraphResourceId backBuffer = AddPresentationTexture(
        testArena, context, allocator, graph,
        Name("tests/task_graph/standalone_ui_backbuffer"), "Standalone UI Back Buffer",
        Graphics::ResourceStates::Unknown
    );
    ASSERT_TRUE(backBuffer.valid());

    const Graphics::GpuTaskCommandRequirements primaryGraphics{
        .requiredCapabilities = Graphics::GpuQueueCapability::Graphics,
        .requiresPrimaryGraphicsQueue = true,
    };
    const Graphics::GpuTaskId prelude = AddTaskWithCommands(
        graph, Name("tests/task_graph/standalone_ui_timing_begin"), "Standalone UI Timing Begin",
        primaryGraphics, BoundaryScheduling(), { .policy = Graphics::GpuTaskTimingPolicy::PacketOnly }
    );
    ASSERT_TRUE(prelude.valid());
    ASSERT_EQ(prelude.index, 0u);
    ASSERT_TRUE(graph.setNormalExecutionPrelude(prelude));

    const Graphics::GpuTaskCommandRequirements transfer{
        .requiredCapabilities = Graphics::GpuQueueCapability::Transfer,
        .externalQueue = queues[1u].id,
    };
    const Graphics::GpuTaskCommandRequirements graphics{
        .requiredCapabilities = Graphics::GpuQueueCapability::Graphics,
    };
    const Graphics::GpuTaskId upload = AddTaskWithCommands(
        graph, Name("tests/task_graph/standalone_ui_upload"), "Standalone UI Upload", transfer
    );
    const Graphics::GpuTaskId clear = AddTaskWithCommands(
        graph, Name("tests/task_graph/standalone_ui_clear"), "Standalone UI Clear", graphics
    );
    ASSERT_TRUE(upload.valid());
    ASSERT_TRUE(clear.valid());
    const Graphics::GpuTaskId rasterDependencies[] = { upload, clear };
    const Graphics::GpuTaskId raster = AddTaskWithCommands(
        graph, Name("tests/task_graph/standalone_ui_raster"), "Standalone UI Raster",
        graphics, {}, {}, rasterDependencies, LengthOf(rasterDependencies)
    );
    ASSERT_TRUE(raster.valid());

    const Graphics::GpuTaskResourceUse backBufferWrite{
        .resource = backBuffer,
        .range = {},
        .requiredState = Graphics::ResourceStates::RenderTarget,
        .access = Graphics::GpuTaskResourceAccess::Write,
    };
    Graphics::GpuTaskDesc outputDesc;
    outputDesc.setIdentity(Name("tests/task_graph/standalone_ui_output"))
        .setMarkerLabel("Standalone UI Output")
        .setDependencies(&raster, 1u)
        .setResourceUses(&backBufferWrite, 1u)
    ;
    const Graphics::GpuTaskId output = graph.addTask(outputDesc, primaryGraphics);
    ASSERT_TRUE(output.valid());

    Graphics::GpuTaskId timingEndDependency = output;
    if(withContributor){
        Graphics::GpuTaskDesc overlayDesc;
        overlayDesc.setIdentity(Name("tests/task_graph/standalone_ui_overlay"))
            .setMarkerLabel("Standalone UI Overlay")
            .setDependencies(&output, 1u)
            .setResourceUses(&backBufferWrite, 1u)
        ;
        const Graphics::GpuTaskId overlay = graph.addTask(overlayDesc, primaryGraphics);
        ASSERT_TRUE(overlay.valid());
        timingEndDependency = overlay;
    }
    const Graphics::GpuTaskCommandRequirements timingEnd{
        .requiresPrimaryGraphicsQueue = true,
    };
    const Graphics::GpuTaskId end = AddTaskWithCommands(
        graph, Name("tests/task_graph/standalone_ui_timing_end"), "Standalone UI Timing End",
        timingEnd, BoundaryScheduling(), {}, &timingEndDependency, 1u
    );
    ASSERT_TRUE(end.valid());
    ASSERT_TRUE(graph.declarePresentEndpoint({ .producer = end, .backBuffer = backBuffer }));

    Graphics::GpuTaskSchedulingHint recoveryScheduling = BoundaryScheduling();
    recoveryScheduling.joinsAcceptedQueueFrontier = true;
    recoveryScheduling.isRecoverySubmission = true;
    const Graphics::GpuTaskId recovery = AddTaskWithCommands(
        graph, Name("tests/task_graph/standalone_ui_recovery"), "Standalone UI Recovery",
        timingEnd, recoveryScheduling
    );
    ASSERT_TRUE(recovery.valid());

    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        const Graphics::GpuTaskGraphTaskView uploadView = declarations.taskAt(upload.index);
        const Graphics::GpuTaskGraphTaskView clearView = declarations.taskAt(clear.index);
        const Graphics::GpuTaskGraphTaskView endView = declarations.taskAt(end.index);
        const Graphics::GpuTaskGraphTaskView recoveryView = declarations.taskAt(recovery.index);
        ASSERT_EQ(uploadView.dependencyCount, 1u);
        ASSERT_EQ(clearView.dependencyCount, 1u);
        ASSERT_EQ(endView.dependencyCount, 1u);
        EXPECT_EQ(uploadView.dependencies[0u], prelude);
        EXPECT_EQ(clearView.dependencies[0u], prelude);
        EXPECT_EQ(endView.dependencies[0u], timingEndDependency);
        EXPECT_EQ(recoveryView.dependencyCount, 0u);
    }

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    Graphics::GpuTaskGraphCompileOptions options;
    options.packetTimingEnvelope = { .firstTask = prelude, .lastTask = end };
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, options));
    const Graphics::GpuCompiledGraph::ReadView plan(compiledGraph);
    ASSERT_TRUE(plan.valid());
    const Graphics::GpuSubmissionPacketId preludePacket = plan.packetForTask(prelude);
    const Graphics::GpuSubmissionPacketId uploadPacket = plan.packetForTask(upload);
    const Graphics::GpuSubmissionPacketId clearPacket = plan.packetForTask(clear);
    const Graphics::GpuSubmissionPacketId outputPacket = plan.packetForTask(output);
    const Graphics::GpuSubmissionPacketId dependencyPacket = plan.packetForTask(timingEndDependency);
    const Graphics::GpuSubmissionPacketId endPacket = plan.packetForTask(end);
    const Graphics::GpuSubmissionPacketId recoveryPacket = plan.packetForTask(recovery);
    ASSERT_TRUE(preludePacket.valid());
    ASSERT_TRUE(uploadPacket.valid());
    ASSERT_TRUE(clearPacket.valid());
    ASSERT_TRUE(outputPacket.valid());
    ASSERT_TRUE(dependencyPacket.valid());
    ASSERT_TRUE(endPacket.valid());
    ASSERT_TRUE(recoveryPacket.valid());
    EXPECT_EQ(preludePacket.index, 0u);
    EXPECT_LT(preludePacket.index, uploadPacket.index);
    EXPECT_LT(preludePacket.index, clearPacket.index);
    EXPECT_LT(outputPacket.index, endPacket.index);
    EXPECT_LT(dependencyPacket.index, endPacket.index);
    EXPECT_NE(outputPacket, endPacket);
    EXPECT_EQ(recoveryPacket.index, plan.packetCount() - 1u);
    const Graphics::GpuCompiledTaskView preludePlan = plan.findTask(prelude);
    ASSERT_TRUE(preludePlan.valid());
    EXPECT_EQ(preludePlan.plan->timingPolicy, Graphics::GpuTaskTimingPolicy::PacketOnly);
    EXPECT_TRUE(plan.packet(preludePacket).plan->recordsTiming);
    EXPECT_TRUE(plan.packet(endPacket).plan->recordsTiming);
    const Graphics::GpuSubmissionPacketRange timingEnvelope = plan.packetTimingEnvelopeRange();
    ASSERT_TRUE(timingEnvelope.valid());
    EXPECT_EQ(timingEnvelope.first, preludePacket);
    EXPECT_EQ(timingEnvelope.packetCount, endPacket.index - preludePacket.index + 1u);
    EXPECT_EQ(plan.packet(preludePacket).plan->queue, queues[0u].id);
    EXPECT_EQ(plan.packet(uploadPacket).plan->queue, queues[1u].id);
    EXPECT_EQ(plan.packet(endPacket).plan->queue, queues[0u].id);
    EXPECT_EQ(plan.packet(recoveryPacket).plan->queue, queues[0u].id);
    EXPECT_TRUE(HasPacketDependency(plan.packet(uploadPacket), preludePacket));
    EXPECT_TRUE(HasPacketDependency(plan.packet(clearPacket), preludePacket));
    EXPECT_FALSE(HasPacketDependency(plan.packet(uploadPacket), clearPacket));
    EXPECT_FALSE(HasPacketDependency(plan.packet(clearPacket), uploadPacket));
    const Graphics::GpuCompiledPresentEndpoint* const endpoint = plan.presentEndpoint();
    ASSERT_NE(endpoint, nullptr);
    EXPECT_EQ(endpoint->producer, end);
    EXPECT_EQ(endpoint->backBuffer, backBuffer);
    EXPECT_EQ(endpoint->packet, endPacket);
    EXPECT_EQ(endpoint->queue, queues[0u].id);
    const Graphics::GpuCompiledPacketView recoveryPlan = plan.packet(recoveryPacket);
    ASSERT_TRUE(recoveryPlan.valid());
    EXPECT_EQ(recoveryPlan.plan->dependencyCount, 0u);
    EXPECT_EQ(recoveryPlan.plan->externalDependencyCount, 0u);
    EXPECT_TRUE(recoveryPlan.plan->joinsAcceptedQueueFrontier);
    EXPECT_TRUE(recoveryPlan.plan->isRecoverySubmission);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuTaskGraph, StandaloneUiFrameTimingEnclosesPresentationWithAndWithoutContributor){
    CheckStandalonePresentationTiming(false);
    CheckStandalonePresentationTiming(true);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

