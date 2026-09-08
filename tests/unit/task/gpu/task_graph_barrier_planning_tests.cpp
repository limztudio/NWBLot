// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_barrier_planning_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, ForcesMergedSameStateWriteDependenciesAcrossResourceKinds){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId textureCopyDest = AddTextureMetadata(
        graph,
        Name("tests/task_graph/same_state_texture_copy_dest"),
        "Same-state Texture Copy Destination"
    );
    const Graphics::GpuGraphResourceId bufferCopyDest = AddBufferMetadata(
        graph,
        Name("tests/task_graph/same_state_buffer_copy_dest"),
        "Same-state Buffer Copy Destination"
    );
    const Graphics::GpuGraphResourceId renderTarget = AddTextureMetadata(
        graph,
        Name("tests/task_graph/same_state_render_target"),
        "Same-state Render Target"
    );
    const Graphics::GpuGraphResourceId depthWrite = AddTextureMetadata(
        graph,
        Name("tests/task_graph/same_state_depth_write"),
        "Same-state Depth Write"
    );
    const Graphics::GpuGraphResourceId accelStructWrite = AddAccelStructMetadata(
        graph,
        Name("tests/task_graph/same_state_accel_struct_write"),
        "Same-state Acceleration Structure Write"
    );
    const Graphics::GpuGraphResourceId readOnly = AddTextureMetadata(
        graph,
        Name("tests/task_graph/same_state_read_only"),
        "Same-state Read Only"
    );
    ASSERT_TRUE(textureCopyDest.valid());
    ASSERT_TRUE(bufferCopyDest.valid());
    ASSERT_TRUE(renderTarget.valid());
    ASSERT_TRUE(depthWrite.valid());
    ASSERT_TRUE(accelStructWrite.valid());
    ASSERT_TRUE(readOnly.valid());

    const Graphics::GpuTaskResourceUse uses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = textureCopyDest,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = bufferCopyDest,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = renderTarget,
            .range = {},
            .requiredState = Graphics::ResourceStates::RenderTarget,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = depthWrite,
            .range = {},
            .requiredState = Graphics::ResourceStates::DepthWrite,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = accelStructWrite,
            .range = {},
            .requiredState = Graphics::ResourceStates::AccelStructWrite,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = readOnly,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    Graphics::GpuTaskDesc producerDesc;
    producerDesc
        .setIdentity(Name("tests/task_graph/same_state_dependency_producer"))
        .setMarkerLabel("Same-state Dependency Producer")
        .setResourceUses(uses, LengthOf(uses))
    ;
    const Graphics::GpuTaskId producer = graph.addTask(producerDesc);
    ASSERT_TRUE(producer.valid());

    Graphics::GpuTaskSchedulingHint consumerScheduling;
    consumerScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc consumerDesc;
    consumerDesc
        .setIdentity(Name("tests/task_graph/same_state_dependency_consumer"))
        .setMarkerLabel("Same-state Dependency Consumer")
        .setScheduling(consumerScheduling)
        .setDependencies(&producer, 1u)
        .setResourceUses(uses, LengthOf(uses))
    ;
    const Graphics::GpuTaskId consumer = graph.addTask(consumerDesc);
    ASSERT_TRUE(consumer.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue() };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);


    const Graphics::GpuCompiledTask* const compiledProducer = compiledPlan.findTask(producer).plan;
    const Graphics::GpuCompiledTask* const compiledConsumer = compiledPlan.findTask(consumer).plan;
    ASSERT_NE(compiledProducer, nullptr);
    ASSERT_NE(compiledConsumer, nullptr);
    EXPECT_EQ(compiledProducer->packet, compiledConsumer->packet);
    ASSERT_EQ(compiledProducer->prologueBarrierCount, LengthOf(uses));
    ASSERT_EQ(compiledConsumer->prologueStateSeedCount, 0u);
    ASSERT_EQ(compiledConsumer->prologueBarrierCount, 5u);

    const Graphics::GpuCompiledBarrier* const producerBarriers = compiledPlan.findTask(producer).prologueBarriers;
    const Graphics::GpuCompiledBarrier* const consumerBarriers = compiledPlan.findTask(consumer).prologueBarriers;
    ASSERT_NE(producerBarriers, nullptr);
    ASSERT_NE(consumerBarriers, nullptr);
    for(u32 barrierIndex = 0u; barrierIndex < compiledProducer->prologueBarrierCount; ++barrierIndex)
        EXPECT_FALSE(producerBarriers[barrierIndex].forceMemoryDependency);

    const auto findConsumerBarrier = [&](const Graphics::GpuGraphResourceId resource){
        for(u32 barrierIndex = 0u; barrierIndex < compiledConsumer->prologueBarrierCount; ++barrierIndex){
            if(consumerBarriers[barrierIndex].resource == resource)
                return consumerBarriers + barrierIndex;
        }
        return static_cast<const Graphics::GpuCompiledBarrier*>(nullptr);
    };
    const Graphics::GpuGraphResourceId forcedResources[] = {
        textureCopyDest,
        bufferCopyDest,
        renderTarget,
        depthWrite,
        accelStructWrite,
    };
    const Graphics::ResourceStates::Mask forcedStates[] = {
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::RenderTarget,
        Graphics::ResourceStates::DepthWrite,
        Graphics::ResourceStates::AccelStructWrite,
    };
    const Graphics::GpuCompiledBarrierType::Enum forcedTypes[] = {
        Graphics::GpuCompiledBarrierType::TextureTransition,
        Graphics::GpuCompiledBarrierType::BufferTransition,
        Graphics::GpuCompiledBarrierType::TextureTransition,
        Graphics::GpuCompiledBarrierType::TextureTransition,
        Graphics::GpuCompiledBarrierType::AccelStructTransition,
    };
    for(usize forcedIndex = 0u; forcedIndex < LengthOf(forcedResources); ++forcedIndex){
        const Graphics::GpuCompiledBarrier* const barrier = findConsumerBarrier(forcedResources[forcedIndex]);
        ASSERT_NE(barrier, nullptr);
        EXPECT_EQ(barrier->type, forcedTypes[forcedIndex]);
        EXPECT_EQ(barrier->before, forcedStates[forcedIndex]);
        EXPECT_EQ(barrier->after, forcedStates[forcedIndex]);
        EXPECT_FALSE(barrier->isGraphInitialState);
        EXPECT_FALSE(barrier->isInitialOwnerHandoff);
        EXPECT_TRUE(barrier->forceMemoryDependency);
    }
    EXPECT_EQ(findConsumerBarrier(readOnly), nullptr);
}

TEST(GpuTaskGraph, PlansPacketBoundaryTransitionsAndUavDependencies){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId texture = AddTextureMetadata(
        graph,
        Name("tests/task_graph/planned_transitions"),
        "Planned Transitions"
    );
    ASSERT_TRUE(texture.valid());

    const Graphics::GpuTaskResourceUse writeUse[] = {
        Graphics::GpuTaskResourceUse{
            .resource = texture,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse uavReadUse[] = {
        Graphics::GpuTaskResourceUse{
            .resource = texture,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    const Graphics::GpuTaskResourceUse shaderReadUse[] = {
        Graphics::GpuTaskResourceUse{
            .resource = texture,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    const Graphics::GpuTaskId writer = AddTask(
        graph,
        Name("tests/task_graph/planned_writer"),
        "Writer",
        nullptr,
        0u,
        writeUse,
        LengthOf(writeUse)
    );
    const Graphics::GpuTaskId uavReader = AddTask(
        graph,
        Name("tests/task_graph/planned_uav_reader"),
        "UAV Reader",
        nullptr,
        0u,
        uavReadUse,
        LengthOf(uavReadUse)
    );
    const Graphics::GpuTaskId shaderReader = AddTask(
        graph,
        Name("tests/task_graph/planned_shader_reader"),
        "Shader Reader",
        nullptr,
        0u,
        shaderReadUse,
        LengthOf(shaderReadUse)
    );
    ASSERT_TRUE(writer.valid());
    ASSERT_TRUE(uavReader.valid());
    ASSERT_TRUE(shaderReader.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue() };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);


    const Graphics::GpuCompiledTask* const compiledWriter = compiledPlan.findTask(writer).plan;
    const Graphics::GpuCompiledTask* const compiledUavReader = compiledPlan.findTask(uavReader).plan;
    const Graphics::GpuCompiledTask* const compiledShaderReader = compiledPlan.findTask(shaderReader).plan;
    ASSERT_NE(compiledWriter, nullptr);
    ASSERT_NE(compiledUavReader, nullptr);
    ASSERT_NE(compiledShaderReader, nullptr);
    ASSERT_EQ(compiledWriter->prologueStateSeedCount, 0u);
    ASSERT_EQ(compiledUavReader->prologueStateSeedCount, 1u);
    ASSERT_EQ(compiledShaderReader->prologueStateSeedCount, 1u);
    ASSERT_EQ(compiledWriter->prologueBarrierCount, 1u);
    ASSERT_EQ(compiledUavReader->prologueBarrierCount, 1u);
    ASSERT_EQ(compiledShaderReader->prologueBarrierCount, 1u);

    const Graphics::GpuCompiledBarrier* const writerBarrier = compiledPlan.findTask(writer).prologueBarriers;
    const Graphics::GpuCompiledBarrier* const uavBarrier = compiledPlan.findTask(uavReader).prologueBarriers;
    const Graphics::GpuCompiledBarrier* const shaderBarrier = compiledPlan.findTask(shaderReader).prologueBarriers;
    const Graphics::GpuPacketStateSeed* const uavSeed = compiledPlan.findTask(uavReader).prologueStateSeeds;
    const Graphics::GpuPacketStateSeed* const shaderSeed = compiledPlan.findTask(shaderReader).prologueStateSeeds;
    ASSERT_NE(writerBarrier, nullptr);
    ASSERT_NE(uavBarrier, nullptr);
    ASSERT_NE(shaderBarrier, nullptr);
    ASSERT_NE(uavSeed, nullptr);
    ASSERT_NE(shaderSeed, nullptr);
    EXPECT_EQ(uavSeed[0].resource, texture);
    EXPECT_EQ(uavSeed[0].sourcePacket, compiledWriter->packet);
    EXPECT_EQ(shaderSeed[0].resource, texture);
    EXPECT_EQ(shaderSeed[0].sourcePacket, compiledUavReader->packet);
    EXPECT_EQ(writerBarrier[0].type, Graphics::GpuCompiledBarrierType::TextureTransition);
    EXPECT_EQ(writerBarrier[0].before, Graphics::ResourceStates::Common);
    EXPECT_EQ(writerBarrier[0].after, Graphics::ResourceStates::UnorderedAccess);
    EXPECT_FALSE(writerBarrier[0].forceMemoryDependency);
    EXPECT_EQ(uavBarrier[0].type, Graphics::GpuCompiledBarrierType::TextureUav);
    EXPECT_EQ(uavBarrier[0].before, Graphics::ResourceStates::UnorderedAccess);
    EXPECT_EQ(uavBarrier[0].after, Graphics::ResourceStates::UnorderedAccess);
    EXPECT_TRUE(uavBarrier[0].forceMemoryDependency);
    EXPECT_EQ(shaderBarrier[0].type, Graphics::GpuCompiledBarrierType::TextureTransition);
    EXPECT_EQ(shaderBarrier[0].before, Graphics::ResourceStates::UnorderedAccess);
    EXPECT_EQ(shaderBarrier[0].after, Graphics::ResourceStates::ShaderResource);
    EXPECT_FALSE(shaderBarrier[0].forceMemoryDependency);
}

TEST(GpuTaskGraph, PlansCompositeUavDependencies){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId texture = AddTextureMetadata(
        graph,
        Name("tests/task_graph/composite_uav_texture"),
        "Composite UAV Texture"
    );
    const Graphics::GpuGraphResourceId buffer = AddBufferMetadata(
        graph,
        Name("tests/task_graph/composite_uav_buffer"),
        "Composite UAV Buffer"
    );
    ASSERT_TRUE(texture.valid());
    ASSERT_TRUE(buffer.valid());

    const Graphics::ResourceStates::Mask compositeState =
        Graphics::ResourceStates::UnorderedAccess | Graphics::ResourceStates::ShaderResource
    ;
    const Graphics::GpuTaskResourceUse producerUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = texture,
            .range = {},
            .requiredState = compositeState,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = buffer,
            .range = {},
            .requiredState = compositeState,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse consumerUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = texture,
            .range = {},
            .requiredState = compositeState,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = buffer,
            .range = {},
            .requiredState = compositeState,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    const Graphics::GpuTaskId producer = AddTask(
        graph,
        Name("tests/task_graph/composite_uav_producer"),
        "Composite UAV Producer",
        nullptr,
        0u,
        producerUses,
        LengthOf(producerUses)
    );
    const Graphics::GpuTaskId consumer = AddTask(
        graph,
        Name("tests/task_graph/composite_uav_consumer"),
        "Composite UAV Consumer",
        nullptr,
        0u,
        consumerUses,
        LengthOf(consumerUses)
    );
    ASSERT_TRUE(producer.valid());
    ASSERT_TRUE(consumer.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue() };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);


    const Graphics::GpuCompiledTask* const compiledProducer = compiledPlan.findTask(producer).plan;
    const Graphics::GpuCompiledTask* const compiledConsumer = compiledPlan.findTask(consumer).plan;
    ASSERT_NE(compiledProducer, nullptr);
    ASSERT_NE(compiledConsumer, nullptr);
    ASSERT_EQ(compiledConsumer->prologueStateSeedCount, 2u);
    ASSERT_EQ(compiledConsumer->prologueBarrierCount, 2u);

    const Graphics::GpuCompiledBarrier* const consumerBarriers = compiledPlan.findTask(consumer).prologueBarriers;
    ASSERT_NE(consumerBarriers, nullptr);
    bool hasTextureUavBarrier = false;
    bool hasBufferUavBarrier = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledConsumer->prologueBarrierCount; ++barrierIndex){
        const Graphics::GpuCompiledBarrier& barrier = consumerBarriers[barrierIndex];
        hasTextureUavBarrier = hasTextureUavBarrier || (
            barrier.resource == texture
            && barrier.type == Graphics::GpuCompiledBarrierType::TextureUav
            && barrier.before == compositeState
            && barrier.after == compositeState
        );
        hasBufferUavBarrier = hasBufferUavBarrier || (
            barrier.resource == buffer
            && barrier.type == Graphics::GpuCompiledBarrierType::BufferUav
            && barrier.before == compositeState
            && barrier.after == compositeState
        );
    }
    EXPECT_TRUE(hasTextureUavBarrier);
    EXPECT_TRUE(hasBufferUavBarrier);
}

TEST(GpuTaskGraph, TracksFinalOverlappingIntraTaskTextureStateForConsumersAndExports){
    TestArena testArena;
    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue() };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };

    {
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId texture = AddTextureMetadata(
            graph,
            Name("tests/task_graph/intra_task_final_state_consumer"),
            "Intra-Task Final State Consumer"
        );
        ASSERT_TRUE(texture.valid());

        const Graphics::GpuTaskResourceUse producerUses[] = {
            Graphics::GpuTaskResourceUse{
                .resource = texture,
                .range = {},
                .requiredState = Graphics::ResourceStates::ShaderResource,
                .access = Graphics::GpuTaskResourceAccess::Read,
            },
            Graphics::GpuTaskResourceUse{
                .resource = texture,
                .range = {},
                .requiredState = Graphics::ResourceStates::RenderTarget,
                .access = Graphics::GpuTaskResourceAccess::Write,
            },
        };
        const Graphics::GpuTaskResourceUse consumerUse{
            .resource = texture,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        };
        const Graphics::GpuTaskId producer = AddTask(
            graph,
            Name("tests/task_graph/intra_task_final_state_producer"),
            "Intra-Task Final State Producer",
            nullptr,
            0u,
            producerUses,
            LengthOf(producerUses)
        );
        const Graphics::GpuTaskId consumer = AddTask(
            graph,
            Name("tests/task_graph/intra_task_final_state_consumer_task"),
            "Intra-Task Final State Consumer Task",
            nullptr,
            0u,
            &consumerUse,
            1u
        );
        ASSERT_TRUE(producer.valid());
        ASSERT_TRUE(consumer.valid());

        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
        ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
        const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

        const Graphics::GpuCompiledTaskView compiledProducerView = compiledPlan.findTask(producer);
        const Graphics::GpuCompiledTaskView compiledConsumerView = compiledPlan.findTask(consumer);
        const Graphics::GpuCompiledTask* const compiledProducer = compiledProducerView.plan;
        const Graphics::GpuCompiledTask* const compiledConsumer = compiledConsumerView.plan;
        ASSERT_NE(compiledProducer, nullptr);
        ASSERT_NE(compiledConsumer, nullptr);
        EXPECT_NE(compiledProducer->packet, compiledConsumer->packet);
        ASSERT_EQ(compiledProducer->prologueBarrierCount, 1u);
        ASSERT_EQ(compiledConsumer->prologueStateSeedCount, 1u);
        ASSERT_EQ(compiledConsumer->prologueBarrierCount, 1u);

        const Graphics::GpuCompiledBarrier* const producerBarrier = compiledProducerView.prologueBarriers;
        const Graphics::GpuPacketStateSeed* const consumerSeed = compiledConsumerView.prologueStateSeeds;
        const Graphics::GpuCompiledBarrier* const consumerBarrier = compiledConsumerView.prologueBarriers;
        ASSERT_NE(producerBarrier, nullptr);
        ASSERT_NE(consumerSeed, nullptr);
        ASSERT_NE(consumerBarrier, nullptr);
        EXPECT_EQ(producerBarrier[0u].after, Graphics::ResourceStates::ShaderResource);
        EXPECT_EQ(consumerSeed[0u].sourcePacket, compiledProducer->packet);
        EXPECT_EQ(consumerBarrier[0u].type, Graphics::GpuCompiledBarrierType::TextureTransition);
        EXPECT_EQ(consumerBarrier[0u].before, Graphics::ResourceStates::RenderTarget);
        EXPECT_EQ(consumerBarrier[0u].after, Graphics::ResourceStates::ShaderResource);
    }

    {
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId texture = graph.importResource(
            Graphics::GpuGraphResourceDesc{}
                .setIdentity(Name("tests/task_graph/intra_task_final_state_export"))
                .setMarkerLabel("Intra-Task Final State Export")
                .setType(Graphics::GpuGraphResourceType::Texture)
                .setInitialState(Graphics::ResourceStates::Common)
                .setExternalFinalState(Graphics::ResourceStates::ShaderResource)
        );
        ASSERT_TRUE(texture.valid());

        const Graphics::GpuTaskResourceUse uses[] = {
            Graphics::GpuTaskResourceUse{
                .resource = texture,
                .range = {},
                .requiredState = Graphics::ResourceStates::ShaderResource,
                .access = Graphics::GpuTaskResourceAccess::Read,
            },
            Graphics::GpuTaskResourceUse{
                .resource = texture,
                .range = {},
                .requiredState = Graphics::ResourceStates::RenderTarget,
                .access = Graphics::GpuTaskResourceAccess::Write,
            },
        };
        const Graphics::GpuTaskId producer = AddTask(
            graph,
            Name("tests/task_graph/intra_task_final_state_export_task"),
            "Intra-Task Final State Export Task",
            nullptr,
            0u,
            uses,
            LengthOf(uses)
        );
        ASSERT_TRUE(producer.valid());

        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
        ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
        const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

        const Graphics::GpuCompiledTaskView compiledProducerView = compiledPlan.findTask(producer);
        const Graphics::GpuCompiledTask* const compiledProducer = compiledProducerView.plan;
        ASSERT_NE(compiledProducer, nullptr);
        ASSERT_EQ(compiledProducer->epilogueBarrierCount, 1u);
        const Graphics::GpuCompiledBarrier* const exportBarrier = compiledProducerView.epilogueBarriers;
        ASSERT_NE(exportBarrier, nullptr);
        EXPECT_EQ(exportBarrier[0u].type, Graphics::GpuCompiledBarrierType::TextureStateExport);
        EXPECT_EQ(exportBarrier[0u].before, Graphics::ResourceStates::RenderTarget);
        EXPECT_EQ(exportBarrier[0u].after, Graphics::ResourceStates::ShaderResource);
    }
}

TEST(GpuTaskGraph, PlansGraphInitialStateForUncoveredLaterTextureSubresourcesWithinTask){
    TestArena testArena;
    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue() };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId texture = graph.importResource(
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/intra_task_first_use_texture_tail"))
            .setMarkerLabel("Intra-Task First-Use Texture Tail")
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(Graphics::ResourceStates::CopySource)
    );
    ASSERT_TRUE(texture.valid());

    const Graphics::GpuTaskResourceRange mipZeroRange{
        .textureSubresources = Graphics::TextureSubresourceSet(
            0u,
            1u,
            0u,
            Graphics::TextureSubresourceSet::AllArraySlices
        ),
    };
    const Graphics::GpuTaskResourceRange allMipsRange{
        .textureSubresources = Graphics::s_AllSubresources,
    };
    const Graphics::GpuTaskResourceRange unplannedTailRange{
        .textureSubresources = Graphics::TextureSubresourceSet(
            1u,
            Graphics::TextureSubresourceSet::AllMipLevels,
            0u,
            Graphics::TextureSubresourceSet::AllArraySlices
        ),
    };
    const Graphics::GpuTaskResourceUse uses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = texture,
            .range = mipZeroRange,
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = texture,
            .range = allMipsRange,
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    const Graphics::GpuTaskId task = AddTask(
        graph,
        Name("tests/task_graph/intra_task_first_use_texture_tail_task"),
        "Intra-Task First-Use Texture Tail Task",
        nullptr,
        0u,
        uses,
        LengthOf(uses)
    );
    ASSERT_TRUE(task.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);


    const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;
    ASSERT_NE(compiledTask, nullptr);
    ASSERT_EQ(compiledTask->prologueBarrierCount, 2u);
    const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(task).prologueBarriers;
    ASSERT_NE(barriers, nullptr);

    bool hasMipZeroInitialBarrier = false;
    bool hasTailInitialBarrier = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledTask->prologueBarrierCount; ++barrierIndex){
        const Graphics::GpuCompiledBarrier& barrier = barriers[barrierIndex];
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
            && barrier.resource == texture
            && barrier.before == Graphics::ResourceStates::CopySource
            && barrier.after == Graphics::ResourceStates::CopyDest
            && barrier.isGraphInitialState
            && barrier.range.textureSubresources == mipZeroRange.textureSubresources
        )
            hasMipZeroInitialBarrier = true;
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
            && barrier.resource == texture
            && barrier.before == Graphics::ResourceStates::CopySource
            && barrier.after == Graphics::ResourceStates::ShaderResource
            && barrier.isGraphInitialState
            && barrier.range.textureSubresources == unplannedTailRange.textureSubresources
        )
            hasTailInitialBarrier = true;
    }
    EXPECT_TRUE(hasMipZeroInitialBarrier);
    EXPECT_TRUE(hasTailInitialBarrier);
}

TEST(GpuTaskGraph, AllowsIndependentConcurrentReadStateSources){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);

    const auto importTexture = [&](
        const Name& identity,
        const AStringView label,
        const Graphics::ResourceQueueSharing::Mask queueSharing,
        const Graphics::ResourceStates::Mask initialState = Graphics::ResourceStates::Common
    ){
        Graphics::GpuGraphResourceDesc desc;
        desc
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(initialState)
            .setQueueSharing(queueSharing)
        ;
        return graph.importResource(desc);
    };
    const Graphics::GpuGraphResourceId concurrentTexture = importTexture(
        Name("tests/task_graph/concurrent_read_only"),
        "Concurrent Read Only",
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute,
        Graphics::ResourceStates::ShaderResource
    );
    const Graphics::GpuGraphResourceId defaultConcurrentTexture = importTexture(
        Name("tests/task_graph/default_concurrent_read_only"),
        "Default Concurrent Read Only",
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId exclusiveTexture = importTexture(
        Name("tests/task_graph/exclusive_read_only"),
        "Exclusive Read Only",
        Graphics::ResourceQueueSharing::Exclusive
    );
    ASSERT_TRUE(concurrentTexture.valid());
    ASSERT_TRUE(defaultConcurrentTexture.valid());
    ASSERT_TRUE(exclusiveTexture.valid());

    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    const Graphics::GpuQueueRequest computeRequest{
        Graphics::GpuQueueCapability::Compute,
        Graphics::GpuQueuePreference::Compute,
        false,
        false,
    };
    const auto addReadTask = [&](
        const Name& identity,
        const AStringView label,
        const Graphics::GpuGraphResourceId resource,
        const Graphics::GpuQueueRequest& queue,
        const bool hasIndependentStateSource
    ){
        const Graphics::GpuTaskResourceUse uses[] = {
            Graphics::GpuTaskResourceUse{
                .resource = resource,
                .range = {},
                .requiredState = Graphics::ResourceStates::ShaderResource,
                .access = Graphics::GpuTaskResourceAccess::Read,
                .hasIndependentStateSource = hasIndependentStateSource,
            },
        };
        Graphics::GpuTaskDesc desc;
        desc
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setQueue(queue)
            .setScheduling(scheduling)
            .setResourceUses(uses, LengthOf(uses))
        ;
        return graph.addTask(desc);
    };
    const Graphics::GpuTaskId concurrentGraphics = addReadTask(
        Name("tests/task_graph/concurrent_read_graphics"),
        "Concurrent Graphics Read",
        concurrentTexture,
        graphicsRequest,
        false
    );
    const Graphics::GpuTaskId concurrentCompute = addReadTask(
        Name("tests/task_graph/concurrent_read_compute"),
        "Concurrent Compute Read",
        concurrentTexture,
        computeRequest,
        true
    );
    const Graphics::GpuTaskId defaultConcurrentGraphics = addReadTask(
        Name("tests/task_graph/default_concurrent_read_graphics"),
        "Default Concurrent Graphics Read",
        defaultConcurrentTexture,
        graphicsRequest,
        false
    );
    const Graphics::GpuTaskId defaultConcurrentCompute = addReadTask(
        Name("tests/task_graph/default_concurrent_read_compute"),
        "Default Concurrent Compute Read",
        defaultConcurrentTexture,
        computeRequest,
        false
    );
    const Graphics::GpuTaskId exclusiveGraphics = addReadTask(
        Name("tests/task_graph/exclusive_read_graphics"),
        "Exclusive Graphics Read",
        exclusiveTexture,
        graphicsRequest,
        false
    );
    const Graphics::GpuTaskId exclusiveCompute = addReadTask(
        Name("tests/task_graph/exclusive_read_compute"),
        "Exclusive Compute Read",
        exclusiveTexture,
        computeRequest,
        true
    );
    ASSERT_TRUE(concurrentGraphics.valid());
    ASSERT_TRUE(concurrentCompute.valid());
    ASSERT_TRUE(defaultConcurrentGraphics.valid());
    ASSERT_TRUE(defaultConcurrentCompute.valid());
    ASSERT_TRUE(exclusiveGraphics.valid());
    ASSERT_TRUE(exclusiveCompute.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    EXPECT_EQ(FindEdge(analysis, concurrentGraphics, concurrentCompute), nullptr);
    EXPECT_EQ(FindEdge(analysis, defaultConcurrentGraphics, defaultConcurrentCompute), nullptr);
    EXPECT_EQ(FindEdge(analysis, exclusiveGraphics, exclusiveCompute), nullptr);

    {
        const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

        ASSERT_EQ(compiledPlan.packetCount(), 6u);
        const Graphics::GpuSubmissionPacketId concurrentGraphicsPacket = compiledPlan.packetForTask(concurrentGraphics);
        const Graphics::GpuSubmissionPacketId concurrentComputePacket = compiledPlan.packetForTask(concurrentCompute);
        const Graphics::GpuSubmissionPacketId defaultConcurrentGraphicsPacket = compiledPlan.packetForTask(
            defaultConcurrentGraphics
        );
        const Graphics::GpuSubmissionPacketId defaultConcurrentComputePacket = compiledPlan.packetForTask(
            defaultConcurrentCompute
        );
        const Graphics::GpuSubmissionPacketId exclusiveGraphicsPacket = compiledPlan.packetForTask(exclusiveGraphics);
        const Graphics::GpuSubmissionPacketId exclusiveComputePacket = compiledPlan.packetForTask(exclusiveCompute);
        ASSERT_TRUE(concurrentGraphicsPacket.valid());
        ASSERT_TRUE(concurrentComputePacket.valid());
        ASSERT_TRUE(defaultConcurrentGraphicsPacket.valid());
        ASSERT_TRUE(defaultConcurrentComputePacket.valid());
        ASSERT_TRUE(exclusiveGraphicsPacket.valid());
        ASSERT_TRUE(exclusiveComputePacket.valid());
        EXPECT_EQ(compiledPlan.packetIdAt(0u), concurrentGraphicsPacket);
        EXPECT_EQ(compiledPlan.packetIdAt(1u), concurrentComputePacket);
        EXPECT_EQ(compiledPlan.packetIdAt(2u), defaultConcurrentGraphicsPacket);
        EXPECT_EQ(compiledPlan.packetIdAt(3u), defaultConcurrentComputePacket);
        EXPECT_EQ(compiledPlan.packetIdAt(4u), exclusiveGraphicsPacket);
        EXPECT_EQ(compiledPlan.packetIdAt(5u), exclusiveComputePacket);

        const Graphics::GpuCompiledTaskView concurrentComputeView = compiledPlan.findTask(concurrentCompute);
        const Graphics::GpuCompiledTaskView defaultConcurrentComputeView = compiledPlan.findTask(
            defaultConcurrentCompute
        );
        const Graphics::GpuCompiledTaskView exclusiveGraphicsView = compiledPlan.findTask(exclusiveGraphics);
        const Graphics::GpuCompiledTaskView exclusiveComputeView = compiledPlan.findTask(exclusiveCompute);
        const Graphics::GpuCompiledTask* const compiledConcurrentCompute = concurrentComputeView.plan;
        const Graphics::GpuCompiledTask* const compiledDefaultConcurrentCompute = defaultConcurrentComputeView.plan;
        const Graphics::GpuCompiledTask* const compiledExclusiveGraphics = exclusiveGraphicsView.plan;
        const Graphics::GpuCompiledTask* const compiledExclusiveCompute = exclusiveComputeView.plan;
        ASSERT_NE(compiledConcurrentCompute, nullptr);
        ASSERT_NE(compiledDefaultConcurrentCompute, nullptr);
        ASSERT_NE(compiledExclusiveGraphics, nullptr);
        ASSERT_NE(compiledExclusiveCompute, nullptr);
        EXPECT_EQ(compiledConcurrentCompute->prologueStateSeedCount, 0u);
        ASSERT_EQ(compiledDefaultConcurrentCompute->prologueStateSeedCount, 1u);
        ASSERT_EQ(compiledExclusiveCompute->prologueStateSeedCount, 1u);
        EXPECT_EQ(compiledConcurrentCompute->prologueBarrierCount, 0u);
        EXPECT_EQ(compiledDefaultConcurrentCompute->prologueBarrierCount, 0u);
        ASSERT_EQ(compiledExclusiveCompute->prologueBarrierCount, 1u);
        ASSERT_EQ(compiledExclusiveGraphics->epilogueBarrierCount, 1u);
        const Graphics::GpuPacketStateSeed* const defaultConcurrentSeed =
            defaultConcurrentComputeView.prologueStateSeeds
        ;
        const Graphics::GpuPacketStateSeed* const exclusiveSeed = exclusiveComputeView.prologueStateSeeds;
        const Graphics::GpuCompiledBarrier* const exclusiveAcquire = exclusiveComputeView.prologueBarriers;
        const Graphics::GpuCompiledBarrier* const exclusiveRelease = exclusiveGraphicsView.epilogueBarriers;
        EXPECT_EQ(concurrentComputeView.prologueStateSeeds, nullptr);
        ASSERT_NE(defaultConcurrentSeed, nullptr);
        ASSERT_NE(exclusiveSeed, nullptr);
        ASSERT_NE(exclusiveAcquire, nullptr);
        ASSERT_NE(exclusiveRelease, nullptr);
        EXPECT_EQ(defaultConcurrentSeed[0u].sourcePacket, defaultConcurrentGraphicsPacket);
        EXPECT_EQ(exclusiveSeed[0u].sourcePacket, exclusiveGraphicsPacket);
        EXPECT_EQ(exclusiveAcquire[0u].type, Graphics::GpuCompiledBarrierType::TextureOwnershipAcquire);
        EXPECT_EQ(exclusiveAcquire[0u].sourceQueue, compiledExclusiveGraphics->queue);
        EXPECT_EQ(exclusiveAcquire[0u].destinationQueue, compiledExclusiveCompute->queue);
        EXPECT_FALSE(exclusiveAcquire[0u].isInitialOwnerHandoff);
        EXPECT_EQ(exclusiveRelease[0u].type, Graphics::GpuCompiledBarrierType::TextureOwnershipRelease);
        EXPECT_EQ(exclusiveRelease[0u].sourceQueue, compiledExclusiveGraphics->queue);
        EXPECT_EQ(exclusiveRelease[0u].destinationQueue, compiledExclusiveCompute->queue);
        EXPECT_EQ(compiledPlan.packet(concurrentComputePacket).plan->dependencyCount, 0u);
        const Graphics::GpuCompiledPacketView defaultConcurrentComputePacketView = compiledPlan.packet(
            defaultConcurrentComputePacket
        );
        ASSERT_TRUE(defaultConcurrentComputePacketView.valid());
        ASSERT_EQ(defaultConcurrentComputePacketView.plan->dependencyCount, 1u);
        EXPECT_EQ(defaultConcurrentComputePacketView.dependencies[0u].producer, defaultConcurrentGraphicsPacket);
        const Graphics::GpuCompiledPacketView exclusiveComputePacketView = compiledPlan.packet(exclusiveComputePacket);
        ASSERT_TRUE(exclusiveComputePacketView.valid());
        ASSERT_EQ(exclusiveComputePacketView.plan->dependencyCount, 1u);
        EXPECT_EQ(exclusiveComputePacketView.dependencies[0u].producer, exclusiveGraphicsPacket);
    }

    // The combined sharing mask becomes Vulkan-concurrent only when the queues have distinct families. Two real
    // VkQueues in one family still use exclusive ownership in the backend, so they must retain the handoff.
    Graphics::GpuPhysicalQueueInfo sameFamilyComputeQueue = DedicatedComputeQueue();
    sameFamilyComputeQueue.familyIndex = GraphicsQueue().familyIndex;
    sameFamilyComputeQueue.queueIndex = 1u;
    const Graphics::GpuPhysicalQueueInfo sameFamilyQueues[] = {
        GraphicsQueue(),
        sameFamilyComputeQueue,
    };
    const Graphics::GpuTaskGraphQueueTopology sameFamilyTopology{
        .queues = sameFamilyQueues,
        .queueCount = LengthOf(sameFamilyQueues),
    };
    Graphics::GpuTaskGraphAnalysis sameFamilyAnalysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments sameFamilyAssignments(testArena.arena);
    Graphics::GpuCompiledGraph sameFamilyCompiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(
        graph,
        sameFamilyAnalysis,
        sameFamilyTopology,
        sameFamilyAssignments,
        sameFamilyCompiledGraph
    ));
    const Graphics::GpuCompiledGraph::ReadView sameFamilyPlan(sameFamilyCompiledGraph);
    const Graphics::GpuSubmissionPacketId sameFamilyGraphicsPacket = sameFamilyPlan.packetForTask(
        concurrentGraphics
    );
    const Graphics::GpuSubmissionPacketId sameFamilyComputePacket = sameFamilyPlan.packetForTask(
        concurrentCompute
    );
    const Graphics::GpuCompiledTaskView sameFamilyCompiledComputeView = sameFamilyPlan.findTask(concurrentCompute);
    const Graphics::GpuCompiledTask* const sameFamilyCompiledCompute = sameFamilyCompiledComputeView.plan;
    ASSERT_TRUE(sameFamilyGraphicsPacket.valid());
    ASSERT_TRUE(sameFamilyComputePacket.valid());
    ASSERT_NE(sameFamilyCompiledCompute, nullptr);
    ASSERT_EQ(sameFamilyCompiledCompute->prologueStateSeedCount, 1u);
    const Graphics::GpuPacketStateSeed* const sameFamilySeed = sameFamilyCompiledComputeView.prologueStateSeeds;
    ASSERT_NE(sameFamilySeed, nullptr);
    EXPECT_EQ(sameFamilySeed[0u].sourcePacket, sameFamilyGraphicsPacket);
    const Graphics::GpuCompiledPacketView sameFamilyComputePacketView = sameFamilyPlan.packet(sameFamilyComputePacket);
    ASSERT_TRUE(sameFamilyComputePacketView.valid());
    ASSERT_EQ(sameFamilyComputePacketView.plan->dependencyCount, 1u);
    EXPECT_EQ(sameFamilyComputePacketView.dependencies[0u].producer, sameFamilyGraphicsPacket);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

