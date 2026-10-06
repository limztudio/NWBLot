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

    const Graphics::GpuTaskCommandRequirements graphicsCommands{ Graphics::GpuQueueCapability::Graphics };
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
        .setScheduling(scheduling)
        .setResourceUses(backbufferWrite, LengthOf(backbufferWrite))
    ;
    const Graphics::GpuTaskId sceneOutput = graph.addTask(sceneOutputDesc, graphicsCommands);
    ASSERT_TRUE(sceneOutput.valid());

    Graphics::GpuTaskDesc overlayDesc;
    overlayDesc
        .setIdentity(Name("tests/task_graph/presentation_overlay"))
        .setMarkerLabel("Presentation Overlay")
        .setScheduling(scheduling)
        .setDependencies(&sceneOutput, 1u)
        .setResourceUses(backbufferWrite, LengthOf(backbufferWrite))
    ;
    const Graphics::GpuTaskId overlay = graph.addTask(overlayDesc, graphicsCommands);
    ASSERT_TRUE(overlay.valid());

    // The published producer only confirms that the final presentation contributor recorded. It deliberately has
    // no direct backbuffer use, so endpoint validation must follow the graph dependency closure instead.
    Graphics::GpuTaskDesc terminalDesc;
    terminalDesc
        .setIdentity(Name("tests/task_graph/presentation_terminal"))
        .setMarkerLabel("Presentation Terminal")
        .setScheduling(scheduling)
        .setDependencies(&overlay, 1u)
    ;
    const Graphics::GpuTaskId terminal = graph.addTask(terminalDesc, graphicsCommands);
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
        .setScheduling(scheduling)
    ;
    const Graphics::GpuTaskId lateTail = graph.addTask(lateTailDesc, graphicsCommands);
    ASSERT_TRUE(lateTail.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue() };
    const Graphics::GpuPhysicalQueueTopology topology{
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

TEST(GpuTaskGraph, IndependentOutputLayerJoinsProducedVersionAtSceneAndStandalonePresentation){
    for(const bool hasScene : { false, true }){
        for(const bool hasDedicatedCompute : { false, true }){
            TestArena testArena;
            Graphics::GraphicsAllocator graphicsAllocator(testArena.arena);
            Core::CpuTaskScheduler cpuScheduler(0u);
            Graphics::GraphicsBackend::VulkanContext context(graphicsAllocator, cpuScheduler, 1u);
            Graphics::GraphicsBackend::VulkanAllocator allocator(context);
            Graphics::GpuTaskGraph graph(testArena.arena);
            const Graphics::GpuGraphResourceId backbuffer = AddPresentationTexture(
                testArena, context, allocator, graph,
                Name("tests/task_graph/layer_backbuffer"), "Output Layer Back Buffer", Graphics::ResourceStates::Unknown
            );
            const Graphics::GpuGraphResourceId layerColor = AddTextureMetadata(
                graph, Name("tests/task_graph/layer_color"), "Output Layer Color", Graphics::ResourceStates::Unknown
            );
            const Graphics::GpuGraphResourceId sceneColor = AddTextureMetadata(
                graph, Name("tests/task_graph/layer_scene"), "Output Layer Scene", Graphics::ResourceStates::Unknown
            );
            ASSERT_TRUE(backbuffer.valid());
            ASSERT_TRUE(layerColor.valid());
            ASSERT_TRUE(sceneColor.valid());
            const Graphics::GpuGraphResourceVersionId layerVersion = graph.declareResourceVersion(
                Graphics::GpuGraphResourceVersionDesc{}
                    .setResource(layerColor)
                    .setOrigin(Graphics::GpuGraphResourceVersionOrigin::TaskProduced)
            );
            ASSERT_TRUE(layerVersion.valid());
            Graphics::GpuTaskSchedulingHint scheduling;
            scheduling.forceSubmissionBoundary = true;
            scheduling.allowPacketMerge = false;
            const Graphics::GpuTaskResourceUse layerWrite{
                .resource = layerColor,
                .range = {},
                .requiredState = Graphics::ResourceStates::RenderTarget,
                .access = Graphics::GpuTaskResourceAccess::Write,
            };
            const Graphics::GpuTaskResourceVersionUse layerProduce{
                .version = layerVersion,
                .role = Graphics::GpuTaskResourceVersionRole::Produce,
            };
            Graphics::GpuTaskDesc layerDesc;
            layerDesc
                .setIdentity(Name("tests/task_graph/independent_layer"))
                .setMarkerLabel("Independent Output Layer")
                .setScheduling(scheduling)
                .setResourceUses(&layerWrite, 1u)
                .setResourceVersionUses(&layerProduce, 1u)
            ;
            const Graphics::GpuTaskId layer = graph.addTask(layerDesc, GraphicsCommands());
            ASSERT_TRUE(layer.valid());
            Graphics::GpuTaskId scene;
            if(hasScene){
                const Graphics::GpuTaskResourceUse sceneWrite{
                    .resource = sceneColor,
                    .range = {},
                    .requiredState = Graphics::ResourceStates::UnorderedAccess,
                    .access = Graphics::GpuTaskResourceAccess::Write,
                };
                Graphics::GpuTaskDesc sceneDesc;
                sceneDesc
                    .setIdentity(Name("tests/task_graph/independent_scene"))
                    .setMarkerLabel("Independent Scene")
                    .setScheduling(scheduling)
                    .setResourceUses(&sceneWrite, 1u)
                ;
                scene = graph.addTask(sceneDesc, ComputeCommands());
                ASSERT_TRUE(scene.valid());
            }
            const Graphics::GpuTaskResourceUse presentUses[] = {
                {
                    .resource = backbuffer,
                    .range = {},
                    .requiredState = Graphics::ResourceStates::RenderTarget,
                    .access = Graphics::GpuTaskResourceAccess::Write,
                },
                {
                    .resource = layerColor,
                    .range = {},
                    .requiredState = Graphics::ResourceStates::ShaderResource,
                    .access = Graphics::GpuTaskResourceAccess::Read,
                },
                {
                    .resource = sceneColor,
                    .range = {},
                    .requiredState = Graphics::ResourceStates::ShaderResource,
                    .access = Graphics::GpuTaskResourceAccess::Read,
                },
            };
            const Graphics::GpuTaskResourceVersionUse layerConsume{
                .version = layerVersion,
                .role = Graphics::GpuTaskResourceVersionRole::Consume,
            };
            // The layer dependency comes from its explicit produced version, even without an explicit task edge.
            Graphics::GpuTaskDesc presentDesc;
            presentDesc
                .setIdentity(Name("tests/task_graph/layer_final_join"))
                .setMarkerLabel("Output Layer Final Join")
                .setScheduling(scheduling)
                .setDependencies(&scene, hasScene ? 1u : 0u)
                .setResourceUses(presentUses, hasScene ? 3u : 2u)
                .setResourceVersionUses(&layerConsume, 1u)
            ;
            const Graphics::GpuTaskId present = graph.addTask(presentDesc, GraphicsCommands());
            ASSERT_TRUE(present.valid());
            ASSERT_TRUE(graph.declarePresentEndpoint({ .producer = present, .backBuffer = backbuffer }));
            const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), DedicatedComputeQueue() };
            const Graphics::GpuPhysicalQueueTopology topology{
                .queues = queues,
                .queueCount = hasDedicatedCompute ? 2u : 1u,
            };
            Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
            Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
            Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
            ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
            EXPECT_TRUE(analysis.hasInferredEdge(layer, present));
            EXPECT_FALSE(analysis.hasExplicitEdge(layer, present));
            if(hasScene){
                EXPECT_TRUE(analysis.hasExplicitEdge(scene, present));
                EXPECT_FALSE(analysis.hasExplicitEdge(scene, layer));
                EXPECT_FALSE(analysis.hasInferredEdge(scene, layer));
                EXPECT_FALSE(analysis.hasExplicitEdge(layer, scene));
                EXPECT_FALSE(analysis.hasInferredEdge(layer, scene));
            }
            const Tests::GpuTaskGraphReadViews reads(graph, compiledGraph);
            ASSERT_TRUE(reads.valid());
            EXPECT_EQ(reads.declarations.taskAt(layer.index).dependencyCount, 0u);
            EXPECT_EQ(reads.declarations.taskAt(layer.index).externalDependencyCount, 0u);
            const Graphics::GpuCompiledPresentEndpoint* const endpoint = reads.compiled.presentEndpoint();
            ASSERT_NE(endpoint, nullptr);
            EXPECT_EQ(endpoint->producer, present);
            EXPECT_EQ(endpoint->queue, queues[0u].id);
            const Graphics::GpuCompiledTaskView finalTask = reads.compiled.findTask(present);
            ASSERT_NE(finalTask.plan, nullptr);
            bool layerTransitionFound = false;
            for(u32 index = 0u; index < finalTask.plan->prologueBarrierCount; ++index){
                const Graphics::GpuCompiledBarrier& barrier = finalTask.prologueBarriers[index];
                layerTransitionFound = layerTransitionFound || (
                    barrier.resource == layerColor
                    && barrier.before == Graphics::ResourceStates::RenderTarget
                    && barrier.after == Graphics::ResourceStates::ShaderResource
                );
            }
            EXPECT_TRUE(layerTransitionFound);
        }
    }
}

TEST(GpuTaskGraph, RejectsInvalidPresentationEndpointContracts){
    TestArena testArena;
    Graphics::GraphicsAllocator graphicsAllocator(testArena.arena);
    Core::CpuTaskScheduler cpuScheduler(0u);
    Graphics::GraphicsBackend::VulkanContext context(graphicsAllocator, cpuScheduler, 1u);
    Graphics::GraphicsBackend::VulkanAllocator allocator(context);
    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuPhysicalQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    const Graphics::GpuTaskCommandRequirements graphicsCommands{ Graphics::GpuQueueCapability::Graphics };
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
        const Graphics::GpuTaskId producer = AddTaskWithCommands(
            graph,
            Name("tests/task_graph/presentation_declaration_terminal"),
            "Presentation Declaration Terminal",
            graphicsCommands
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
        const Graphics::GpuTaskId foreignProducer = AddTaskWithCommands(
            foreignGraph,
            Name("tests/task_graph/presentation_declaration_foreign_terminal"),
            "Presentation Declaration Foreign Terminal",
            graphicsCommands
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
            .setResourceUses(&writerUse, 1u)
        ;
        const Graphics::GpuTaskId writer = graph.addTask(writerDesc, graphicsCommands);
        ASSERT_TRUE(writer.valid());
        const Graphics::GpuTaskId producer = AddTaskWithCommands(
            graph,
            Name("tests/task_graph/presentation_hazard_domain_terminal"),
            "Presentation Hazard Domain Terminal",
            graphicsCommands,
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
            .setResourceUses(&writerUse, 1u)
        ;
        const Graphics::GpuTaskId writer = graph.addTask(writerDesc, graphicsCommands);
        ASSERT_TRUE(writer.valid());
        const Graphics::GpuTaskId producer = AddTaskWithCommands(
            graph,
            Name("tests/task_graph/presentation_invalid_buffer_terminal"),
            "Presentation Invalid Buffer Terminal",
            graphicsCommands
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
            .setResourceUses(&writerUse, 1u)
        ;
        const Graphics::GpuTaskId writer = graph.addTask(writerDesc, graphicsCommands);
        ASSERT_TRUE(writer.valid());
        const Graphics::GpuTaskCommandRequirements transferCommands{ Graphics::GpuQueueCapability::Transfer };
        const Graphics::GpuTaskId producer = AddTaskWithCommands(
            graph,
            Name("tests/task_graph/presentation_non_graphics_terminal"),
            "Presentation Non-Graphics Terminal",
            transferCommands
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
            Name("tests/task_graph/presentation_unwritten_backbuffer"),
            "Presentation Unwritten Back Buffer",
            Graphics::ResourceStates::Present
        );
        ASSERT_TRUE(backbuffer.valid());
        const Graphics::GpuTaskId producer = AddTaskWithCommands(
            graph,
            Name("tests/task_graph/presentation_unwritten_terminal"),
            "Presentation Unwritten Terminal",
            graphicsCommands
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
            .setResourceUses(&writerUse, 1u)
        ;
        const Graphics::GpuTaskId writer = graph.addTask(writerDesc, graphicsCommands);
        ASSERT_TRUE(writer.valid());
        const Graphics::GpuTaskId producer = AddTaskWithCommands(
            graph,
            Name("tests/task_graph/presentation_disconnected_terminal"),
            "Presentation Disconnected Terminal",
            graphicsCommands
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
            .setResourceUses(&writerUse, 1u)
        ;
        const Graphics::GpuTaskId reachableWriter = graph.addTask(reachableWriterDesc, graphicsCommands);
        ASSERT_TRUE(reachableWriter.valid());
        Graphics::GpuTaskDesc producerDesc;
        producerDesc
            .setIdentity(Name("tests/task_graph/presentation_mixed_writer_terminal"))
            .setMarkerLabel("Presentation Mixed Writer Terminal")
            .setDependencies(&reachableWriter, 1u)
        ;
        const Graphics::GpuTaskId producer = graph.addTask(producerDesc, graphicsCommands);
        ASSERT_TRUE(producer.valid());
        Graphics::GpuTaskDesc unrelatedWriterDesc;
        unrelatedWriterDesc
            .setIdentity(Name("tests/task_graph/presentation_mixed_writer_unrelated"))
            .setMarkerLabel("Presentation Mixed Writer Unrelated")
            .setResourceUses(&writerUse, 1u)
        ;
        const Graphics::GpuTaskId unrelatedWriter = graph.addTask(unrelatedWriterDesc, graphicsCommands);
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
    const Graphics::GpuPhysicalQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    const Graphics::GpuTaskCommandRequirements graphicsCommands{ Graphics::GpuQueueCapability::Graphics };
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
        return AddTaskWithCommands(
            graph,
            Name("tests/task_graph/presentation_texture_contract_terminal"),
            "Presentation Texture Contract Terminal",
            graphicsCommands,
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
            .setResourceUses(&writerUse, 1u)
        ;
        const Graphics::GpuTaskId writer = graph.addTask(writerDesc, graphicsCommands);
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
            .setResourceUses(&writerUse, 1u)
        ;
        const Graphics::GpuTaskId writer = graph.addTask(writerDesc, graphicsCommands);
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
            .setResourceUses(&writerUse, 1u)
        ;
        const Graphics::GpuTaskId writer = graph.addTask(writerDesc, graphicsCommands);
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
            .setResourceUses(&readerUse, 1u)
        ;
        const Graphics::GpuTaskId reader = graph.addTask(readerDesc, graphicsCommands);
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
            .setResourceUses(&writerUse, 1u)
        ;
        const Graphics::GpuTaskId writer = graph.addTask(writerDesc, graphicsCommands);
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
            .setResourceUses(&laterReaderUse, 1u)
        ;
        const Graphics::GpuTaskId laterReader = graph.addTask(laterReaderDesc, graphicsCommands);
        ASSERT_TRUE(laterReader.valid());
        ASSERT_TRUE(graph.declarePresentEndpoint(Graphics::GpuPresentEndpoint{
            .producer = producer,
            .backBuffer = backbuffer,
        }));
        expectInvalidEndpoint(graph, producer, laterReader, backbuffer);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

