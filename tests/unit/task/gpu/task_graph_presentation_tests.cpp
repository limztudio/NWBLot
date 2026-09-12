// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_presentation_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, CompilesPresentationEndpointAfterTerminalFinalizer){
    TestArena testArena;
    Graphics::GraphicsAllocator graphicsAllocator(testArena.arena);
    Core::CpuTaskScheduler cpuScheduler(0u);
    Graphics::GraphicsBackend::VulkanContext context(graphicsAllocator, cpuScheduler, 1u);
    Graphics::GraphicsBackend::VulkanAllocator allocator(context);
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId backbuffer = AddPresentationTexture(
        testArena,
        context,
        allocator,
        graph,
        Name("tests/task_graph/presentation_backbuffer"),
        "Presentation Back Buffer",
        Graphics::ResourceStates::Unknown
    );
    ASSERT_TRUE(backbuffer.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);

        EXPECT_NE(declarations.textureForResource(backbuffer), nullptr);
        EXPECT_EQ(declarations.resourceAt(backbuffer.index).initialState, Graphics::ResourceStates::Unknown);
        EXPECT_EQ(declarations.resourceAt(backbuffer.index).externalFinalState, Graphics::ResourceStates::Present);
    }

    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Graphics::GpuTaskCostHint::Medium;
    scheduling.overlapPreferred = false;
    scheduling.avoidQueueCrossing = true;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    const Graphics::GpuTaskResourceUse backbufferWrite[] = {
        Graphics::GpuTaskResourceUse{
            .resource = backbuffer,
            .range = {},
            .requiredState = Graphics::ResourceStates::RenderTarget,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };

    Graphics::GpuTaskDesc sceneOutputDesc;
    sceneOutputDesc
        .setIdentity(Name("tests/task_graph/scene_output"))
        .setMarkerLabel("Scene Output")
        .setQueue(graphicsRequest)
        .setScheduling(scheduling)
        .setResourceUses(backbufferWrite, LengthOf(backbufferWrite))
    ;
    const Graphics::GpuTaskId sceneOutput = graph.addTask(sceneOutputDesc);
    ASSERT_TRUE(sceneOutput.valid());

    Graphics::GpuTaskDesc overlayDesc;
    overlayDesc
        .setIdentity(Name("tests/task_graph/presentation_overlay"))
        .setMarkerLabel("Presentation Overlay")
        .setQueue(graphicsRequest)
        .setScheduling(scheduling)
        .setDependencies(&sceneOutput, 1u)
        .setResourceUses(backbufferWrite, LengthOf(backbufferWrite))
    ;
    const Graphics::GpuTaskId overlay = graph.addTask(overlayDesc);
    ASSERT_TRUE(overlay.valid());

    // The published producer only confirms that the final presentation contributor recorded. It deliberately has
    // no direct backbuffer use, so endpoint validation must follow the graph dependency closure instead.
    Graphics::GpuTaskDesc terminalDesc;
    terminalDesc
        .setIdentity(Name("tests/task_graph/presentation_terminal"))
        .setMarkerLabel("Presentation Terminal")
        .setQueue(graphicsRequest)
        .setScheduling(scheduling)
        .setDependencies(&overlay, 1u)
    ;
    const Graphics::GpuTaskId terminal = graph.addTask(terminalDesc);
    ASSERT_TRUE(terminal.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);

        EXPECT_EQ(declarations.taskAt(terminal.index).resourceUseCount, 0u);
    }

    // A diagnostic/history tail need not depend on the backbuffer, but declaration order keeps it outside the
    // terminal presentation span. This lets the renderer signal from the terminal packet while later graph-owned
    // maintenance work continues independently.
    Graphics::GpuTaskDesc lateTailDesc;
    lateTailDesc
        .setIdentity(Name("tests/task_graph/presentation_late_tail"))
        .setMarkerLabel("Presentation Late Tail")
        .setQueue(graphicsRequest)
        .setScheduling(scheduling)
    ;
    const Graphics::GpuTaskId lateTail = graph.addTask(lateTailDesc);
    ASSERT_TRUE(lateTail.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue() };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    {
        const Tests::GpuTaskGraphReadViews reads(graph, compiledGraph);

        ASSERT_TRUE(reads.valid());
        ASSERT_TRUE(analysis.validFor(reads.declarations));
        ASSERT_TRUE(assignments.validFor(reads.declarations));
    }
    ASSERT_TRUE(graph.declarePresentEndpoint(Graphics::GpuPresentEndpoint{
        .producer = terminal,
        .backBuffer = backbuffer,
    }));
    // Endpoint metadata is part of an immutable compiled plan. Adding it invalidates the prior no-endpoint plan
    // even though task/resource generations and counts did not change.
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

        EXPECT_FALSE(analysis.validFor(declarations));
        EXPECT_FALSE(assignments.validFor(declarations));
        EXPECT_FALSE(compiledPlan.validFor(declarations));
    }
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Tests::GpuTaskGraphReadViews reads(graph, compiledGraph);
    const Graphics::GpuCompiledGraph::ReadView& compiledPlan = reads.compiled;

    ASSERT_TRUE(reads.valid());
    const Graphics::GpuSubmissionPacketId sceneOutputPacket = compiledPlan.packetForTask(sceneOutput);
    const Graphics::GpuSubmissionPacketId overlayPacket = compiledPlan.packetForTask(overlay);
    const Graphics::GpuSubmissionPacketId terminalPacket = compiledPlan.packetForTask(terminal);
    const Graphics::GpuSubmissionPacketId lateTailPacket = compiledPlan.packetForTask(lateTail);
    ASSERT_TRUE(sceneOutputPacket.valid());
    ASSERT_TRUE(overlayPacket.valid());
    ASSERT_TRUE(terminalPacket.valid());
    ASSERT_TRUE(lateTailPacket.valid());
    EXPECT_NE(sceneOutputPacket, overlayPacket);
    EXPECT_NE(overlayPacket, terminalPacket);
    EXPECT_GT(lateTailPacket.index, terminalPacket.index);
    EXPECT_EQ(compiledPlan.packet(terminalPacket).plan->queue, queues[0].id);
    const Graphics::GpuSubmissionPacketRange presentationRange = compiledPlan.packetRange(
        sceneOutputPacket,
        terminalPacket
    );
    ASSERT_TRUE(presentationRange.valid());
    EXPECT_EQ(presentationRange.packetCount, 3u);
    const Graphics::GpuCompiledPacketView overlayPlan = compiledPlan.packet(overlayPacket);
    ASSERT_TRUE(overlayPlan.valid());
    ASSERT_EQ(overlayPlan.plan->taskCount, 1u);
    ASSERT_EQ(overlayPlan.plan->dependencyCount, 1u);
    EXPECT_EQ(overlayPlan.dependencies[0u].producer, sceneOutputPacket);
    const Graphics::GpuCompiledPresentEndpoint* const endpoint = compiledPlan.presentEndpoint();
    ASSERT_NE(endpoint, nullptr);
    EXPECT_TRUE(endpoint->valid());
    EXPECT_EQ(endpoint->producer, terminal);
    EXPECT_EQ(endpoint->backBuffer, backbuffer);
    EXPECT_EQ(endpoint->packet, terminalPacket);
    EXPECT_EQ(endpoint->queue, queues[0].id);

    const Graphics::GpuCompiledTaskView compiledOverlayView = compiledPlan.findTask(overlay);
    const Graphics::GpuCompiledTask* const compiledOverlay = compiledOverlayView.plan;
    ASSERT_NE(compiledOverlay, nullptr);
    const Graphics::GpuCompiledBarrier* const overlayEpilogue = compiledOverlayView.epilogueBarriers;
    ASSERT_NE(overlayEpilogue, nullptr);
    bool foundPresentExport = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledOverlay->epilogueBarrierCount; ++barrierIndex){
        const Graphics::GpuCompiledBarrier& barrier = overlayEpilogue[barrierIndex];
        foundPresentExport = foundPresentExport || (
            barrier.type == Graphics::GpuCompiledBarrierType::TextureStateExport
            && barrier.resource == backbuffer
            && barrier.before == Graphics::ResourceStates::RenderTarget
            && barrier.after == Graphics::ResourceStates::Present
            && barrier.sourceQueue == queues[0u].id
            && barrier.destinationQueue == queues[0u].id
        );
    }
    EXPECT_TRUE(foundPresentExport);
}

TEST(GpuTaskGraph, AcceptsPresentationEndpointFromPresentAcquisitionState){
    TestArena testArena;
    Graphics::GraphicsAllocator graphicsAllocator(testArena.arena);
    Core::CpuTaskScheduler cpuScheduler(0u);
    Graphics::GraphicsBackend::VulkanContext context(graphicsAllocator, cpuScheduler, 1u);
    Graphics::GraphicsBackend::VulkanAllocator allocator(context);
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId backbuffer = AddPresentationTexture(
        testArena,
        context,
        allocator,
        graph,
        Name("tests/task_graph/present_acquisition_backbuffer"),
        "Present Acquisition Back Buffer",
        Graphics::ResourceStates::Present
    );
    ASSERT_TRUE(backbuffer.valid());

    const Graphics::GpuTaskResourceUse writerUse{
        .resource = backbuffer,
        .range = {},
        .requiredState = Graphics::ResourceStates::RenderTarget,
        .access = Graphics::GpuTaskResourceAccess::Write,
    };
    Graphics::GpuTaskDesc writerDesc;
    writerDesc
        .setIdentity(Name("tests/task_graph/present_acquisition_writer"))
        .setMarkerLabel("Present Acquisition Writer")
        .setQueue(Graphics::GpuQueueRequest{
            Graphics::GpuQueueCapability::Graphics,
            Graphics::GpuQueuePreference::Graphics,
            false,
            false,
        })
        .setResourceUses(&writerUse, 1u)
    ;
    const Graphics::GpuTaskId writer = graph.addTask(writerDesc);
    ASSERT_TRUE(writer.valid());
    ASSERT_TRUE(graph.declarePresentEndpoint(Graphics::GpuPresentEndpoint{
        .producer = writer,
        .backBuffer = backbuffer,
    }));

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    ASSERT_NE(compiledPlan.presentEndpoint(), nullptr);
    EXPECT_EQ(compiledPlan.presentEndpoint()->producer, writer);
    EXPECT_EQ(compiledPlan.presentEndpoint()->backBuffer, backbuffer);
    EXPECT_EQ(declarations.resourceAt(backbuffer.index).initialState, Graphics::ResourceStates::Present);
    EXPECT_EQ(declarations.resourceAt(backbuffer.index).externalFinalState, Graphics::ResourceStates::Present);
}

TEST(GpuTaskGraph, RejectsInvalidPresentationEndpointContracts){
    TestArena testArena;
    Graphics::GraphicsAllocator graphicsAllocator(testArena.arena);
    Core::CpuTaskScheduler cpuScheduler(0u);
    Graphics::GraphicsBackend::VulkanContext context(graphicsAllocator, cpuScheduler, 1u);
    Graphics::GraphicsBackend::VulkanAllocator allocator(context);
    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    const auto expectInvalidEndpoint = [&](
        Graphics::GpuTaskGraph& graph,
        const Graphics::GpuTaskId producer,
        const Graphics::GpuTaskId relatedTask,
        const Graphics::GpuGraphResourceId resource
    ){
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
        EXPECT_FALSE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

        EXPECT_EQ(analysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::InvalidPresentationEndpoint);
        EXPECT_EQ(analysis.diagnostic().task, producer);
        EXPECT_EQ(analysis.diagnostic().relatedTask, relatedTask);
        EXPECT_EQ(analysis.diagnostic().resource, resource);
        EXPECT_FALSE(analysis.valid());
        EXPECT_FALSE(assignments.valid());
        EXPECT_FALSE(compiledPlan.valid());
    };

    {
        Graphics::GpuTaskGraph graph(testArena.arena);
        Graphics::GpuTaskGraph foreignGraph(testArena.arena);
        const Graphics::GpuGraphResourceId backbuffer = AddPresentationTexture(
            testArena,
            context,
            allocator,
            graph,
            Name("tests/task_graph/presentation_declaration_backbuffer"),
            "Presentation Declaration Back Buffer",
            Graphics::ResourceStates::Unknown
        );
        const Graphics::GpuTaskId producer = AddTaskWithQueue(
            graph,
            Name("tests/task_graph/presentation_declaration_terminal"),
            "Presentation Declaration Terminal",
            graphicsRequest
        );
        const Graphics::GpuGraphResourceId foreignBackbuffer = AddPresentationTexture(
            testArena,
            context,
            allocator,
            foreignGraph,
            Name("tests/task_graph/presentation_declaration_foreign_backbuffer"),
            "Presentation Declaration Foreign Back Buffer",
            Graphics::ResourceStates::Present
        );
        const Graphics::GpuTaskId foreignProducer = AddTaskWithQueue(
            foreignGraph,
            Name("tests/task_graph/presentation_declaration_foreign_terminal"),
            "Presentation Declaration Foreign Terminal",
            graphicsRequest
        );
        ASSERT_TRUE(backbuffer.valid());
        ASSERT_TRUE(producer.valid());
        ASSERT_TRUE(foreignBackbuffer.valid());
        ASSERT_TRUE(foreignProducer.valid());
        EXPECT_FALSE(graph.declarePresentEndpoint(Graphics::GpuPresentEndpoint{
            .producer = foreignProducer,
            .backBuffer = backbuffer,
        }));
        EXPECT_FALSE(graph.declarePresentEndpoint(Graphics::GpuPresentEndpoint{
            .producer = producer,
            .backBuffer = foreignBackbuffer,
        }));
        ASSERT_TRUE(graph.declarePresentEndpoint(Graphics::GpuPresentEndpoint{
            .producer = producer,
            .backBuffer = backbuffer,
        }));
        EXPECT_FALSE(graph.declarePresentEndpoint(Graphics::GpuPresentEndpoint{
            .producer = producer,
            .backBuffer = backbuffer,
        }));
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        const Graphics::GpuPresentEndpoint* const endpoint = declarations.presentEndpoint();

        ASSERT_NE(endpoint, nullptr);
        EXPECT_EQ(endpoint->producer, producer);
        EXPECT_EQ(endpoint->backBuffer, backbuffer);
    }

    {
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId backbuffer = AddHazardDomain(
            graph,
            Name("tests/task_graph/presentation_hazard_domain_backbuffer"),
            "Presentation Hazard Domain Back Buffer"
        );
        ASSERT_TRUE(backbuffer.valid());
        const Graphics::GpuTaskResourceUse writerUse{
            .resource = backbuffer,
            .range = {},
            .requiredState = Graphics::ResourceStates::RenderTarget,
            .access = Graphics::GpuTaskResourceAccess::Write,
        };
        Graphics::GpuTaskDesc writerDesc;
        writerDesc
            .setIdentity(Name("tests/task_graph/presentation_hazard_domain_writer"))
            .setMarkerLabel("Presentation Hazard Domain Writer")
            .setQueue(graphicsRequest)
            .setResourceUses(&writerUse, 1u)
        ;
        const Graphics::GpuTaskId writer = graph.addTask(writerDesc);
        ASSERT_TRUE(writer.valid());
        const Graphics::GpuTaskId producer = AddTaskWithQueue(
            graph,
            Name("tests/task_graph/presentation_hazard_domain_terminal"),
            "Presentation Hazard Domain Terminal",
            graphicsRequest,
            {},
            {},
            &writer,
            1u
        );
        ASSERT_TRUE(producer.valid());
        ASSERT_TRUE(graph.declarePresentEndpoint(Graphics::GpuPresentEndpoint{
            .producer = producer,
            .backBuffer = backbuffer,
        }));
        expectInvalidEndpoint(graph, producer, {}, backbuffer);
    }

    {
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId nonPresentationResource = AddBufferMetadata(
            graph,
            Name("tests/task_graph/presentation_invalid_buffer"),
            "Presentation Invalid Buffer"
        );
        ASSERT_TRUE(nonPresentationResource.valid());
        const Graphics::GpuTaskResourceUse writerUse{
            .resource = nonPresentationResource,
            .range = {},
            .requiredState = Graphics::ResourceStates::Common,
            .access = Graphics::GpuTaskResourceAccess::Write,
        };
        Graphics::GpuTaskDesc writerDesc;
        writerDesc
            .setIdentity(Name("tests/task_graph/presentation_invalid_buffer_writer"))
            .setMarkerLabel("Presentation Invalid Buffer Writer")
            .setQueue(graphicsRequest)
            .setResourceUses(&writerUse, 1u)
        ;
        const Graphics::GpuTaskId writer = graph.addTask(writerDesc);
        ASSERT_TRUE(writer.valid());
        const Graphics::GpuTaskId producer = AddTaskWithQueue(
            graph,
            Name("tests/task_graph/presentation_invalid_buffer_terminal"),
            "Presentation Invalid Buffer Terminal",
            graphicsRequest
        );
        ASSERT_TRUE(producer.valid());
        EXPECT_FALSE(graph.declarePresentEndpoint(Graphics::GpuPresentEndpoint{
            .producer = {},
            .backBuffer = nonPresentationResource,
        }));
        EXPECT_FALSE(graph.declarePresentEndpoint(Graphics::GpuPresentEndpoint{
            .producer = producer,
            .backBuffer = {},
        }));
        ASSERT_TRUE(graph.declarePresentEndpoint(Graphics::GpuPresentEndpoint{
            .producer = producer,
            .backBuffer = nonPresentationResource,
        }));
        expectInvalidEndpoint(graph, producer, {}, nonPresentationResource);
    }

    {
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId backbuffer = AddPresentationTexture(
            testArena,
            context,
            allocator,
            graph,
            Name("tests/task_graph/presentation_non_graphics_backbuffer"),
            "Presentation Non-Graphics Back Buffer",
            Graphics::ResourceStates::Unknown
        );
        ASSERT_TRUE(backbuffer.valid());
        const Graphics::GpuTaskResourceUse writerUse{
            .resource = backbuffer,
            .range = {},
            .requiredState = Graphics::ResourceStates::RenderTarget,
            .access = Graphics::GpuTaskResourceAccess::Write,
        };
        Graphics::GpuTaskDesc writerDesc;
        writerDesc
            .setIdentity(Name("tests/task_graph/presentation_non_graphics_writer"))
            .setMarkerLabel("Presentation Non-Graphics Writer")
            .setQueue(graphicsRequest)
            .setResourceUses(&writerUse, 1u)
        ;
        const Graphics::GpuTaskId writer = graph.addTask(writerDesc);
        ASSERT_TRUE(writer.valid());
        const Graphics::GpuQueueRequest transferRequest{
            Graphics::GpuQueueCapability::Transfer,
            Graphics::GpuQueuePreference::Transfer,
            false,
            false,
        };
        const Graphics::GpuTaskId producer = AddTaskWithQueue(
            graph,
            Name("tests/task_graph/presentation_non_graphics_terminal"),
            "Presentation Non-Graphics Terminal",
            transferRequest
        );
        ASSERT_TRUE(producer.valid());
        ASSERT_TRUE(graph.declarePresentEndpoint(Graphics::GpuPresentEndpoint{
            .producer = producer,
            .backBuffer = backbuffer,
        }));
        expectInvalidEndpoint(graph, producer, {}, backbuffer);
    }

    {
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId backbuffer = AddPresentationTexture(
            testArena,
            context,
            allocator,
            graph,
            Name("tests/task_graph/presentation_unwritten_backbuffer"),
            "Presentation Unwritten Back Buffer",
            Graphics::ResourceStates::Present
        );
        ASSERT_TRUE(backbuffer.valid());
        const Graphics::GpuTaskId producer = AddTaskWithQueue(
            graph,
            Name("tests/task_graph/presentation_unwritten_terminal"),
            "Presentation Unwritten Terminal",
            graphicsRequest
        );
        ASSERT_TRUE(producer.valid());
        ASSERT_TRUE(graph.declarePresentEndpoint(Graphics::GpuPresentEndpoint{
            .producer = producer,
            .backBuffer = backbuffer,
        }));
        expectInvalidEndpoint(graph, producer, {}, backbuffer);
    }

    {
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId backbuffer = AddPresentationTexture(
            testArena,
            context,
            allocator,
            graph,
            Name("tests/task_graph/presentation_disconnected_backbuffer"),
            "Presentation Disconnected Back Buffer",
            Graphics::ResourceStates::Unknown
        );
        ASSERT_TRUE(backbuffer.valid());
        const Graphics::GpuTaskResourceUse writerUse{
            .resource = backbuffer,
            .range = {},
            .requiredState = Graphics::ResourceStates::RenderTarget,
            .access = Graphics::GpuTaskResourceAccess::Write,
        };
        Graphics::GpuTaskDesc writerDesc;
        writerDesc
            .setIdentity(Name("tests/task_graph/presentation_disconnected_writer"))
            .setMarkerLabel("Presentation Disconnected Writer")
            .setQueue(graphicsRequest)
            .setResourceUses(&writerUse, 1u)
        ;
        const Graphics::GpuTaskId writer = graph.addTask(writerDesc);
        ASSERT_TRUE(writer.valid());
        const Graphics::GpuTaskId producer = AddTaskWithQueue(
            graph,
            Name("tests/task_graph/presentation_disconnected_terminal"),
            "Presentation Disconnected Terminal",
            graphicsRequest
        );
        ASSERT_TRUE(producer.valid());
        ASSERT_TRUE(graph.declarePresentEndpoint(Graphics::GpuPresentEndpoint{
            .producer = producer,
            .backBuffer = backbuffer,
        }));
        expectInvalidEndpoint(graph, producer, writer, backbuffer);
    }

    {
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId backbuffer = AddPresentationTexture(
            testArena,
            context,
            allocator,
            graph,
            Name("tests/task_graph/presentation_mixed_writer_backbuffer"),
            "Presentation Mixed Writer Back Buffer",
            Graphics::ResourceStates::Present
        );
        ASSERT_TRUE(backbuffer.valid());
        const Graphics::GpuTaskResourceUse writerUse{
            .resource = backbuffer,
            .range = {},
            .requiredState = Graphics::ResourceStates::RenderTarget,
            .access = Graphics::GpuTaskResourceAccess::Write,
        };
        Graphics::GpuTaskDesc reachableWriterDesc;
        reachableWriterDesc
            .setIdentity(Name("tests/task_graph/presentation_mixed_writer_reachable"))
            .setMarkerLabel("Presentation Mixed Writer Reachable")
            .setQueue(graphicsRequest)
            .setResourceUses(&writerUse, 1u)
        ;
        const Graphics::GpuTaskId reachableWriter = graph.addTask(reachableWriterDesc);
        ASSERT_TRUE(reachableWriter.valid());
        Graphics::GpuTaskDesc producerDesc;
        producerDesc
            .setIdentity(Name("tests/task_graph/presentation_mixed_writer_terminal"))
            .setMarkerLabel("Presentation Mixed Writer Terminal")
            .setQueue(graphicsRequest)
            .setDependencies(&reachableWriter, 1u)
        ;
        const Graphics::GpuTaskId producer = graph.addTask(producerDesc);
        ASSERT_TRUE(producer.valid());
        Graphics::GpuTaskDesc unrelatedWriterDesc;
        unrelatedWriterDesc
            .setIdentity(Name("tests/task_graph/presentation_mixed_writer_unrelated"))
            .setMarkerLabel("Presentation Mixed Writer Unrelated")
            .setQueue(graphicsRequest)
            .setResourceUses(&writerUse, 1u)
        ;
        const Graphics::GpuTaskId unrelatedWriter = graph.addTask(unrelatedWriterDesc);
        ASSERT_TRUE(unrelatedWriter.valid());
        ASSERT_TRUE(graph.declarePresentEndpoint(Graphics::GpuPresentEndpoint{
            .producer = producer,
            .backBuffer = backbuffer,
        }));
        expectInvalidEndpoint(graph, producer, unrelatedWriter, backbuffer);
    }
}

TEST(GpuTaskGraph, RejectsInvalidPresentationEndpointTextureContracts){
    TestArena testArena;
    Graphics::GraphicsAllocator graphicsAllocator(testArena.arena);
    Core::CpuTaskScheduler cpuScheduler(0u);
    Graphics::GraphicsBackend::VulkanContext context(graphicsAllocator, cpuScheduler, 1u);
    Graphics::GraphicsBackend::VulkanAllocator allocator(context);
    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    const auto expectInvalidEndpoint = [&](
        Graphics::GpuTaskGraph& graph,
        const Graphics::GpuTaskId producer,
        const Graphics::GpuTaskId relatedTask,
        const Graphics::GpuGraphResourceId resource
    ){
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
        EXPECT_FALSE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

        EXPECT_EQ(analysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::InvalidPresentationEndpoint);
        EXPECT_EQ(analysis.diagnostic().task, producer);
        EXPECT_EQ(analysis.diagnostic().relatedTask, relatedTask);
        EXPECT_EQ(analysis.diagnostic().resource, resource);
        EXPECT_FALSE(analysis.valid());
        EXPECT_FALSE(assignments.valid());
        EXPECT_FALSE(compiledPlan.valid());
    };
    const auto addTerminal = [&](Graphics::GpuTaskGraph& graph, const Graphics::GpuTaskId dependency){
        return AddTaskWithQueue(
            graph,
            Name("tests/task_graph/presentation_texture_contract_terminal"),
            "Presentation Texture Contract Terminal",
            graphicsRequest,
            {},
            {},
            dependency.valid() ? &dependency : nullptr,
            dependency.valid() ? 1u : 0u
        );
    };

    {
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId backbuffer = graph.importResource(
            Graphics::GpuGraphResourceDesc{}
                .setIdentity(Name("tests/task_graph/presentation_metadata_backbuffer"))
                .setMarkerLabel("Presentation Metadata Back Buffer")
                .setType(Graphics::GpuGraphResourceType::Texture)
                .setInitialState(Graphics::ResourceStates::Unknown)
                .setExternalFinalState(Graphics::ResourceStates::Present)
        );
        ASSERT_TRUE(backbuffer.valid());
        {
            const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);

            EXPECT_FALSE(declarations.resourceAt(backbuffer.index).hasBackendResource);
        }
        const Graphics::GpuTaskResourceUse writerUse{
            .resource = backbuffer,
            .range = {},
            .requiredState = Graphics::ResourceStates::RenderTarget,
            .access = Graphics::GpuTaskResourceAccess::Write,
        };
        Graphics::GpuTaskDesc writerDesc;
        writerDesc
            .setIdentity(Name("tests/task_graph/presentation_metadata_writer"))
            .setMarkerLabel("Presentation Metadata Writer")
            .setQueue(graphicsRequest)
            .setResourceUses(&writerUse, 1u)
        ;
        const Graphics::GpuTaskId writer = graph.addTask(writerDesc);
        const Graphics::GpuTaskId producer = addTerminal(graph, writer);
        ASSERT_TRUE(writer.valid());
        ASSERT_TRUE(producer.valid());
        ASSERT_TRUE(graph.declarePresentEndpoint(Graphics::GpuPresentEndpoint{
            .producer = producer,
            .backBuffer = backbuffer,
        }));
        expectInvalidEndpoint(graph, producer, {}, backbuffer);
    }

    const auto expectStateContractRejected = [&](
        const Graphics::ResourceStates::Mask initialState,
        const Graphics::ResourceStates::Mask externalFinalState,
        const Graphics::GpuPhysicalQueueId externalFinalReleaseDestinationQueue,
        const Name& identity,
        const AStringView label
    ){
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId backbuffer = AddPresentationTexture(
            testArena,
            context,
            allocator,
            graph,
            identity,
            label,
            initialState,
            externalFinalState,
            externalFinalReleaseDestinationQueue
        );
        ASSERT_TRUE(backbuffer.valid());
        const Graphics::GpuTaskResourceUse writerUse{
            .resource = backbuffer,
            .range = {},
            .requiredState = Graphics::ResourceStates::RenderTarget,
            .access = Graphics::GpuTaskResourceAccess::Write,
        };
        Graphics::GpuTaskDesc writerDesc;
        writerDesc
            .setIdentity(Name("tests/task_graph/presentation_state_contract_writer"))
            .setMarkerLabel("Presentation State Contract Writer")
            .setQueue(graphicsRequest)
            .setResourceUses(&writerUse, 1u)
        ;
        const Graphics::GpuTaskId writer = graph.addTask(writerDesc);
        const Graphics::GpuTaskId producer = addTerminal(graph, writer);
        ASSERT_TRUE(writer.valid());
        ASSERT_TRUE(producer.valid());
        ASSERT_TRUE(graph.declarePresentEndpoint(Graphics::GpuPresentEndpoint{
            .producer = producer,
            .backBuffer = backbuffer,
        }));
        expectInvalidEndpoint(graph, producer, {}, backbuffer);
    };
    expectStateContractRejected(
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::Present,
        {},
        Name("tests/task_graph/presentation_common_initial_backbuffer"),
        "Presentation Common Initial Back Buffer"
    );
    expectStateContractRejected(
        Graphics::ResourceStates::Unknown,
        Graphics::ResourceStates::Unknown,
        {},
        Name("tests/task_graph/presentation_missing_final_backbuffer"),
        "Presentation Missing Final Back Buffer"
    );
    expectStateContractRejected(
        Graphics::ResourceStates::Unknown,
        Graphics::ResourceStates::ShaderResource,
        {},
        Name("tests/task_graph/presentation_wrong_final_backbuffer"),
        "Presentation Wrong Final Back Buffer"
    );
    expectStateContractRejected(
        Graphics::ResourceStates::Unknown,
        Graphics::ResourceStates::Present,
        queue.id,
        Name("tests/task_graph/presentation_external_release_backbuffer"),
        "Presentation External Release Back Buffer"
    );

    const auto expectPresentWriteRejected = [&](
        const Graphics::ResourceStates::Mask writerState,
        const Name& identity,
        const AStringView label
    ){
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId backbuffer = AddPresentationTexture(
            testArena,
            context,
            allocator,
            graph,
            identity,
            label,
            Graphics::ResourceStates::Unknown
        );
        ASSERT_TRUE(backbuffer.valid());
        const Graphics::GpuTaskResourceUse writerUse{
            .resource = backbuffer,
            .range = {},
            .requiredState = writerState,
            .access = Graphics::GpuTaskResourceAccess::Write,
        };
        Graphics::GpuTaskDesc writerDesc;
        writerDesc
            .setIdentity(Name("tests/task_graph/presentation_present_state_writer"))
            .setMarkerLabel("Presentation Present State Writer")
            .setQueue(graphicsRequest)
            .setResourceUses(&writerUse, 1u)
        ;
        const Graphics::GpuTaskId writer = graph.addTask(writerDesc);
        const Graphics::GpuTaskId producer = addTerminal(graph, writer);
        ASSERT_TRUE(writer.valid());
        ASSERT_TRUE(producer.valid());
        ASSERT_TRUE(graph.declarePresentEndpoint(Graphics::GpuPresentEndpoint{
            .producer = producer,
            .backBuffer = backbuffer,
        }));
        expectInvalidEndpoint(graph, producer, writer, backbuffer);
    };
    expectPresentWriteRejected(
        Graphics::ResourceStates::Present,
        Name("tests/task_graph/presentation_exact_present_write_backbuffer"),
        "Presentation Exact Present Write Back Buffer"
    );
    expectPresentWriteRejected(
        Graphics::ResourceStates::RenderTarget | Graphics::ResourceStates::Present,
        Name("tests/task_graph/presentation_combined_present_write_backbuffer"),
        "Presentation Combined Present Write Back Buffer"
    );

    {
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId backbuffer = AddPresentationTexture(
            testArena,
            context,
            allocator,
            graph,
            Name("tests/task_graph/presentation_read_only_backbuffer"),
            "Presentation Read Only Back Buffer",
            Graphics::ResourceStates::Present
        );
        ASSERT_TRUE(backbuffer.valid());
        const Graphics::GpuTaskResourceUse readerUse{
            .resource = backbuffer,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        };
        Graphics::GpuTaskDesc readerDesc;
        readerDesc
            .setIdentity(Name("tests/task_graph/presentation_read_only_reader"))
            .setMarkerLabel("Presentation Read Only Reader")
            .setQueue(graphicsRequest)
            .setResourceUses(&readerUse, 1u)
        ;
        const Graphics::GpuTaskId reader = graph.addTask(readerDesc);
        const Graphics::GpuTaskId producer = addTerminal(graph, reader);
        ASSERT_TRUE(reader.valid());
        ASSERT_TRUE(producer.valid());
        ASSERT_TRUE(graph.declarePresentEndpoint(Graphics::GpuPresentEndpoint{
            .producer = producer,
            .backBuffer = backbuffer,
        }));
        expectInvalidEndpoint(graph, producer, {}, backbuffer);
    }

    {
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId backbuffer = AddPresentationTexture(
            testArena,
            context,
            allocator,
            graph,
            Name("tests/task_graph/presentation_later_read_backbuffer"),
            "Presentation Later Read Back Buffer",
            Graphics::ResourceStates::Unknown
        );
        ASSERT_TRUE(backbuffer.valid());
        const Graphics::GpuTaskResourceUse writerUse{
            .resource = backbuffer,
            .range = {},
            .requiredState = Graphics::ResourceStates::RenderTarget,
            .access = Graphics::GpuTaskResourceAccess::Write,
        };
        Graphics::GpuTaskDesc writerDesc;
        writerDesc
            .setIdentity(Name("tests/task_graph/presentation_later_read_writer"))
            .setMarkerLabel("Presentation Later Read Writer")
            .setQueue(graphicsRequest)
            .setResourceUses(&writerUse, 1u)
        ;
        const Graphics::GpuTaskId writer = graph.addTask(writerDesc);
        const Graphics::GpuTaskId producer = addTerminal(graph, writer);
        ASSERT_TRUE(writer.valid());
        ASSERT_TRUE(producer.valid());
        const Graphics::GpuTaskResourceUse laterReaderUse{
            .resource = backbuffer,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        };
        Graphics::GpuTaskDesc laterReaderDesc;
        laterReaderDesc
            .setIdentity(Name("tests/task_graph/presentation_later_read_reader"))
            .setMarkerLabel("Presentation Later Read Reader")
            .setQueue(graphicsRequest)
            .setResourceUses(&laterReaderUse, 1u)
        ;
        const Graphics::GpuTaskId laterReader = graph.addTask(laterReaderDesc);
        ASSERT_TRUE(laterReader.valid());
        ASSERT_TRUE(graph.declarePresentEndpoint(Graphics::GpuPresentEndpoint{
            .producer = producer,
            .backBuffer = backbuffer,
        }));
        expectInvalidEndpoint(graph, producer, laterReader, backbuffer);
    }
}

TEST(GpuTaskGraph, RejectsPresentationEndpointUsersOnDifferentGraphicsQueuesDuringFinalization){
    TestArena testArena;
    Graphics::GraphicsAllocator graphicsAllocator(testArena.arena);
    Core::CpuTaskScheduler cpuScheduler(0u);
    Graphics::GraphicsBackend::VulkanContext context(graphicsAllocator, cpuScheduler, 1u);
    Graphics::GraphicsBackend::VulkanAllocator allocator(context);
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId backbuffer = AddPresentationTexture(
        testArena,
        context,
        allocator,
        graph,
        Name("tests/task_graph/presentation_queue_mismatch_backbuffer"),
        "Presentation Queue Mismatch Back Buffer",
        Graphics::ResourceStates::Unknown
    );
    ASSERT_TRUE(backbuffer.valid());

    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint writerScheduling;
    writerScheduling.cost = Graphics::GpuTaskCostHint::Large;
    writerScheduling.allowSameClassQueueRouting = true;
    writerScheduling.allowPacketMerge = false;
    const Graphics::GpuTaskResourceUse writerUse{
        .resource = backbuffer,
        .range = {},
        .requiredState = Graphics::ResourceStates::RenderTarget,
        .access = Graphics::GpuTaskResourceAccess::Write,
    };
    Graphics::GpuTaskDesc writerDesc;
    writerDesc
        .setIdentity(Name("tests/task_graph/presentation_queue_mismatch_writer"))
        .setMarkerLabel("Presentation Queue Mismatch Writer")
        .setQueue(graphicsRequest)
        .setScheduling(writerScheduling)
        .setResourceUses(&writerUse, 1u)
    ;
    const Graphics::GpuTaskId writer = graph.addTask(writerDesc);
    ASSERT_TRUE(writer.valid());

    Graphics::GpuTaskSchedulingHint producerScheduling;
    producerScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    producerScheduling.allowSameClassQueueRouting = true;
    producerScheduling.allowPacketMerge = false;
    Graphics::GpuTaskDesc producerDesc;
    producerDesc
        .setIdentity(Name("tests/task_graph/presentation_queue_mismatch_terminal"))
        .setMarkerLabel("Presentation Queue Mismatch Terminal")
        .setQueue(graphicsRequest)
        .setScheduling(producerScheduling)
        .setDependencies(&writer, 1u)
    ;
    const Graphics::GpuTaskId producer = graph.addTask(producerDesc);
    ASSERT_TRUE(producer.valid());
    ASSERT_TRUE(graph.declarePresentEndpoint(Graphics::GpuPresentEndpoint{
        .producer = producer,
        .backBuffer = backbuffer,
    }));

    Graphics::GpuPhysicalQueueInfo secondaryGraphicsQueue = GraphicsQueue(1u);
    secondaryGraphicsQueue.queueIndex = 1u;
    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        secondaryGraphicsQueue,
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);

        EXPECT_TRUE(analysis.validFor(declarations));
    }
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    ASSERT_TRUE(Assign(graph, analysis, topology, assignments));
    const Graphics::GpuTaskQueueAssignment* const writerAssignment = assignments.find(writer);
    const Graphics::GpuTaskQueueAssignment* const producerAssignment = assignments.find(producer);
    ASSERT_NE(writerAssignment, nullptr);
    ASSERT_NE(producerAssignment, nullptr);
    EXPECT_EQ(writerAssignment->queue, queues[0u].id);
    EXPECT_EQ(producerAssignment->queue, queues[1u].id);
    EXPECT_NE(writerAssignment->queue, producerAssignment->queue);

    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    EXPECT_FALSE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    EXPECT_TRUE(analysis.validFor(declarations));
    EXPECT_FALSE(assignments.validFor(declarations));
    EXPECT_FALSE(compiledPlan.valid());
}

TEST(GpuTaskGraph, RoutesGraphOwnedSetupUploadsThroughTerminalPresentationSpan){
    TestArena testArena;
    Graphics::GraphicsAllocator graphicsAllocator(testArena.arena);
    Core::CpuTaskScheduler cpuScheduler(0u);
    Graphics::GraphicsBackend::VulkanContext context(graphicsAllocator, cpuScheduler, 1u);
    Graphics::GraphicsBackend::VulkanAllocator allocator(context);
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId backbuffer = AddPresentationTexture(
        testArena,
        context,
        allocator,
        graph,
        Name("tests/task_graph/setup_upload_backbuffer"),
        "Setup Upload Back Buffer",
        Graphics::ResourceStates::Unknown
    );
    const Graphics::GpuGraphResourceId vertices = AddBufferMetadata(
        graph,
        Name("tests/task_graph/setup_upload_vertices"),
        "Setup Upload Vertices"
    );
    const Graphics::GpuGraphResourceId indices = AddBufferMetadata(
        graph,
        Name("tests/task_graph/setup_upload_indices"),
        "Setup Upload Indices"
    );
    const Graphics::GpuGraphResourceId fontTexture = AddTextureMetadata(
        graph,
        Name("tests/task_graph/setup_upload_font"),
        "Setup Upload Font",
        Graphics::ResourceStates::ShaderResource
    );
    ASSERT_TRUE(backbuffer.valid());
    ASSERT_TRUE(vertices.valid());
    ASSERT_TRUE(indices.valid());
    ASSERT_TRUE(fontTexture.valid());

    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    const Graphics::GpuQueueRequest uploadRequest{
        Graphics::GpuQueueCapability::Transfer,
        Graphics::GpuQueuePreference::Transfer,
        true,
        true,
    };
    Graphics::GpuTaskSchedulingHint graphicsScheduling;
    graphicsScheduling.cost = Graphics::GpuTaskCostHint::Small;
    graphicsScheduling.avoidQueueCrossing = true;
    graphicsScheduling.forceSubmissionBoundary = true;
    graphicsScheduling.allowPacketMerge = false;
    Graphics::GpuTaskSchedulingHint smallUploadScheduling;
    smallUploadScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    smallUploadScheduling.avoidQueueCrossing = true;
    smallUploadScheduling.forceSubmissionBoundary = true;
    smallUploadScheduling.allowPacketMerge = false;
    Graphics::GpuTaskSchedulingHint largeUploadScheduling;
    largeUploadScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    largeUploadScheduling.overlapPreferred = true;
    largeUploadScheduling.forceSubmissionBoundary = true;
    largeUploadScheduling.allowPacketMerge = false;

    const Graphics::GpuTaskResourceUse sceneUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = backbuffer,
            .range = {},
            .requiredState = Graphics::ResourceStates::RenderTarget,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc sceneDesc;
    sceneDesc
        .setIdentity(Name("tests/task_graph/setup_upload_scene"))
        .setMarkerLabel("Setup Upload Scene")
        .setQueue(graphicsRequest)
        .setScheduling(graphicsScheduling)
        .setResourceUses(sceneUses, LengthOf(sceneUses))
    ;
    const Graphics::GpuTaskId scene = graph.addTask(sceneDesc);
    ASSERT_TRUE(scene.valid());

    const Graphics::GpuTaskId sceneDependencies[] = { scene };
    const Graphics::GpuTaskResourceUse vertexUploadUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = vertices,
            .range = {},
            .requiredState = Graphics::ResourceStates::Common,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc vertexUploadDesc;
    vertexUploadDesc
        .setIdentity(Name("tests/task_graph/setup_upload_vertices_task"))
        .setMarkerLabel("Setup Upload Vertices")
        .setQueue(uploadRequest)
        .setScheduling(smallUploadScheduling)
        .setDependencies(sceneDependencies, LengthOf(sceneDependencies))
        .setResourceUses(vertexUploadUses, LengthOf(vertexUploadUses))
    ;
    const Graphics::GpuTaskId vertexUpload = graph.addTask(vertexUploadDesc);
    ASSERT_TRUE(vertexUpload.valid());

    const Graphics::GpuTaskResourceUse indexUploadUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = indices,
            .range = {},
            .requiredState = Graphics::ResourceStates::Common,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc indexUploadDesc;
    indexUploadDesc
        .setIdentity(Name("tests/task_graph/setup_upload_indices_task"))
        .setMarkerLabel("Setup Upload Indices")
        .setQueue(uploadRequest)
        .setScheduling(smallUploadScheduling)
        .setDependencies(sceneDependencies, LengthOf(sceneDependencies))
        .setResourceUses(indexUploadUses, LengthOf(indexUploadUses))
    ;
    const Graphics::GpuTaskId indexUpload = graph.addTask(indexUploadDesc);
    ASSERT_TRUE(indexUpload.valid());

    const Graphics::GpuTaskResourceUse fontUploadUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = fontTexture,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc fontUploadDesc;
    fontUploadDesc
        .setIdentity(Name("tests/task_graph/setup_upload_font_task"))
        .setMarkerLabel("Setup Upload Font")
        .setQueue(uploadRequest)
        .setScheduling(largeUploadScheduling)
        .setDependencies(sceneDependencies, LengthOf(sceneDependencies))
        .setResourceUses(fontUploadUses, LengthOf(fontUploadUses))
    ;
    const Graphics::GpuTaskId fontUpload = graph.addTask(fontUploadDesc);
    ASSERT_TRUE(fontUpload.valid());

    const Graphics::GpuTaskId overlayDependencies[] = {
        scene,
        vertexUpload,
        indexUpload,
        fontUpload,
    };
    const Graphics::GpuTaskResourceUse overlayUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = backbuffer,
            .range = {},
            .requiredState = Graphics::ResourceStates::RenderTarget,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = vertices,
            .range = {},
            .requiredState = Graphics::ResourceStates::VertexBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = indices,
            .range = {},
            .requiredState = Graphics::ResourceStates::IndexBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = fontTexture,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    Graphics::GpuTaskDesc overlayDesc;
    overlayDesc
        .setIdentity(Name("tests/task_graph/setup_upload_overlay"))
        .setMarkerLabel("Setup Upload Overlay")
        .setQueue(graphicsRequest)
        .setScheduling(graphicsScheduling)
        .setDependencies(overlayDependencies, LengthOf(overlayDependencies))
        .setResourceUses(overlayUses, LengthOf(overlayUses))
    ;
    const Graphics::GpuTaskId overlay = graph.addTask(overlayDesc);
    ASSERT_TRUE(overlay.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedTransferQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphCompileOptions options;
    options.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, options));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);


    const Graphics::GpuSubmissionPacketId scenePacket = compiledPlan.packetForTask(scene);
    const Graphics::GpuSubmissionPacketId vertexPacket = compiledPlan.packetForTask(vertexUpload);
    const Graphics::GpuSubmissionPacketId indexPacket = compiledPlan.packetForTask(indexUpload);
    const Graphics::GpuSubmissionPacketId fontPacket = compiledPlan.packetForTask(fontUpload);
    const Graphics::GpuSubmissionPacketId overlayPacket = compiledPlan.packetForTask(overlay);
    ASSERT_TRUE(scenePacket.valid());
    ASSERT_TRUE(vertexPacket.valid());
    ASSERT_TRUE(indexPacket.valid());
    ASSERT_TRUE(fontPacket.valid());
    ASSERT_TRUE(overlayPacket.valid());
    EXPECT_EQ(compiledPlan.packet(scenePacket).plan->queue, queues[0].id);
    // Tiny vertex/index deltas avoid a queue crossing, but an amortizable texture upload follows Transfer first.
    EXPECT_EQ(compiledPlan.packet(vertexPacket).plan->queue, queues[0].id);
    EXPECT_EQ(compiledPlan.packet(indexPacket).plan->queue, queues[0].id);
    EXPECT_EQ(compiledPlan.packet(fontPacket).plan->queue, queues[1].id);
    EXPECT_EQ(compiledPlan.packet(overlayPacket).plan->queue, queues[0].id);
    EXPECT_GT(vertexPacket.index, scenePacket.index);
    EXPECT_GT(indexPacket.index, scenePacket.index);
    EXPECT_GT(fontPacket.index, scenePacket.index);
    EXPECT_GT(overlayPacket.index, vertexPacket.index);
    EXPECT_GT(overlayPacket.index, indexPacket.index);
    EXPECT_GT(overlayPacket.index, fontPacket.index);
    const Graphics::GpuSubmissionPacketRange presentationRange = compiledPlan.packetRange(scenePacket, overlayPacket);
    ASSERT_TRUE(presentationRange.valid());
    EXPECT_EQ(presentationRange.packetCount, 5u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

