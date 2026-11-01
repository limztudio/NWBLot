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
    Core::GpuTaskGraph graph(testArena.arena);
    const auto textureCopyDestResult = AddTextureMetadata(graph, Name("tests/task_graph/same_state_texture_copy_dest"), "Same-state Texture Copy Destination");
    ASSERT_TRUE(textureCopyDestResult);
    const Core::GpuGraphResourceId textureCopyDest = *textureCopyDestResult;
    const auto bufferCopyDestResult = AddBufferMetadata(graph, Name("tests/task_graph/same_state_buffer_copy_dest"), "Same-state Buffer Copy Destination");
    ASSERT_TRUE(bufferCopyDestResult);
    const Core::GpuGraphResourceId bufferCopyDest = *bufferCopyDestResult;
    const auto renderTargetResult = AddTextureMetadata(graph, Name("tests/task_graph/same_state_render_target"), "Same-state Render Target");
    ASSERT_TRUE(renderTargetResult);
    const Core::GpuGraphResourceId renderTarget = *renderTargetResult;
    const auto depthWriteResult = AddTextureMetadata(graph, Name("tests/task_graph/same_state_depth_write"), "Same-state Depth Write");
    ASSERT_TRUE(depthWriteResult);
    const Core::GpuGraphResourceId depthWrite = *depthWriteResult;
    const auto accelStructWriteResult = AddAccelStructMetadata(graph, Name("tests/task_graph/same_state_accel_struct_write"), "Same-state Acceleration Structure Write");
    ASSERT_TRUE(accelStructWriteResult);
    const Core::GpuGraphResourceId accelStructWrite = *accelStructWriteResult;
    const auto readOnlyResult = AddTextureMetadata(graph, Name("tests/task_graph/same_state_read_only"), "Same-state Read Only");
    ASSERT_TRUE(readOnlyResult);
    const Core::GpuGraphResourceId readOnly = *readOnlyResult;
    ASSERT_TRUE(textureCopyDest.valid());
    ASSERT_TRUE(bufferCopyDest.valid());
    ASSERT_TRUE(renderTarget.valid());
    ASSERT_TRUE(depthWrite.valid());
    ASSERT_TRUE(accelStructWrite.valid());
    ASSERT_TRUE(readOnly.valid());

    const Array<Core::GpuTaskResourceUse, 6> uses = {
        Core::GpuTaskResourceUse{ .resource = textureCopyDest, .range = {}, .requiredState = Core::ResourceStates::CopyDest, .access = Core::GpuTaskResourceAccess::Write, },
        Core::GpuTaskResourceUse{ .resource = bufferCopyDest, .range = {}, .requiredState = Core::ResourceStates::CopyDest, .access = Core::GpuTaskResourceAccess::Write, },
        Core::GpuTaskResourceUse{ .resource = renderTarget, .range = {}, .requiredState = Core::ResourceStates::RenderTarget, .access = Core::GpuTaskResourceAccess::Write, },
        Core::GpuTaskResourceUse{ .resource = depthWrite, .range = {}, .requiredState = Core::ResourceStates::DepthWrite, .access = Core::GpuTaskResourceAccess::Write, },
        Core::GpuTaskResourceUse{ .resource = accelStructWrite, .range = {}, .requiredState = Core::ResourceStates::AccelStructWrite, .access = Core::GpuTaskResourceAccess::Write, },
        Core::GpuTaskResourceUse{ .resource = readOnly, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, },
    };
    Core::GpuTaskDesc producerDesc;
    producerDesc.setIdentity(Name("tests/task_graph/same_state_dependency_producer")).setMarkerLabel("Same-state Dependency Producer").setResourceUses(uses);
    const auto producerResult = graph.addTask(producerDesc);
    ASSERT_TRUE(producerResult);
    const Core::GpuTaskId producer = *producerResult;
    ASSERT_TRUE(producer.valid());

    Core::GpuTaskSchedulingHint consumerScheduling;
    consumerScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc consumerDesc;
    consumerDesc.setIdentity(Name("tests/task_graph/same_state_dependency_consumer")).setMarkerLabel("Same-state Dependency Consumer").setScheduling(consumerScheduling).setDependencies(Span(&producer, 1u)).setResourceUses(uses);
    const auto consumerResult = graph.addTask(consumerDesc);
    ASSERT_TRUE(consumerResult);
    const Core::GpuTaskId consumer = *consumerResult;
    ASSERT_TRUE(consumer.valid());

    SingleQueueCompile singleQueueCompile(testArena);
    ASSERT_TRUE(singleQueueCompile.compile(graph));
    const Core::GpuCompiledGraph::ReadView compiledPlan(singleQueueCompile.compiledGraph);


    const Core::GpuCompiledTask* const compiledProducer = compiledPlan.findTask(producer).plan;
    const Core::GpuCompiledTask* const compiledConsumer = compiledPlan.findTask(consumer).plan;
    ASSERT_NE(compiledProducer, nullptr);
    ASSERT_NE(compiledConsumer, nullptr);
    EXPECT_EQ(compiledProducer->packet, compiledConsumer->packet);
    ASSERT_EQ(compiledProducer->prologueBarrierCount, LengthOf(uses));
    ASSERT_EQ(compiledConsumer->prologueStateSeedCount, 0u);
    ASSERT_EQ(compiledConsumer->prologueBarrierCount, 5u);

    const Core::GpuCompiledBarrier* const producerBarriers = compiledPlan.findTask(producer).prologueBarriers;
    const Core::GpuCompiledBarrier* const consumerBarriers = compiledPlan.findTask(consumer).prologueBarriers;
    ASSERT_NE(producerBarriers, nullptr);
    ASSERT_NE(consumerBarriers, nullptr);
    for(u32 barrierIndex = 0u; barrierIndex < compiledProducer->prologueBarrierCount; ++barrierIndex)
        EXPECT_FALSE(producerBarriers[barrierIndex].forceMemoryDependency);

    const auto findConsumerBarrier = [&](const Core::GpuGraphResourceId resource)noexcept{
        for(u32 barrierIndex = 0u; barrierIndex < compiledConsumer->prologueBarrierCount; ++barrierIndex){
            if(consumerBarriers[barrierIndex].resource == resource)
                return consumerBarriers + barrierIndex;
        }
        return static_cast<const Core::GpuCompiledBarrier*>(nullptr);
    };
    const Array<Core::GpuGraphResourceId, 5> forcedResources = { textureCopyDest, bufferCopyDest, renderTarget, depthWrite, accelStructWrite, };
    const Array<Core::ResourceStates::Mask, 5> forcedStates = { Core::ResourceStates::CopyDest, Core::ResourceStates::CopyDest, Core::ResourceStates::RenderTarget, Core::ResourceStates::DepthWrite, Core::ResourceStates::AccelStructWrite, };
    const Array<Core::GpuCompiledBarrierType::Enum, 5> forcedTypes = {
        Core::GpuCompiledBarrierType::TextureTransition,
        Core::GpuCompiledBarrierType::BufferTransition,
        Core::GpuCompiledBarrierType::TextureTransition,
        Core::GpuCompiledBarrierType::TextureTransition,
        Core::GpuCompiledBarrierType::AccelStructTransition,
    };
    for(usize forcedIndex = 0u; forcedIndex < LengthOf(forcedResources); ++forcedIndex){
        const Core::GpuCompiledBarrier* const barrier = findConsumerBarrier(forcedResources[forcedIndex]);
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
    Core::GpuTaskGraph graph(testArena.arena);
    const auto textureResult = AddTextureMetadata(graph, Name("tests/task_graph/planned_transitions"), "Planned Transitions");
    ASSERT_TRUE(textureResult);
    const Core::GpuGraphResourceId texture = *textureResult;
    ASSERT_TRUE(texture.valid());

    const Array<Core::GpuTaskResourceUse, 1> writeUse = { Core::GpuTaskResourceUse{ .resource = texture, .range = {}, .requiredState = Core::ResourceStates::UnorderedAccess, .access = Core::GpuTaskResourceAccess::Write, }, };
    const Array<Core::GpuTaskResourceUse, 1> uavReadUse = { Core::GpuTaskResourceUse{ .resource = texture, .range = {}, .requiredState = Core::ResourceStates::UnorderedAccess, .access = Core::GpuTaskResourceAccess::Read, }, };
    const Array<Core::GpuTaskResourceUse, 1> shaderReadUse = { Core::GpuTaskResourceUse{ .resource = texture, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, }, };
    const auto writerResult = AddTask(Name("tests/task_graph/planned_writer"), "Writer", {}, Span(writeUse), {}, {}, {}, graph);
    ASSERT_TRUE(writerResult);
    const Core::GpuTaskId writer = *writerResult;
    const auto uavReaderResult = AddTask(Name("tests/task_graph/planned_uav_reader"), "UAV Reader", {}, Span(uavReadUse), {}, {}, {}, graph);
    ASSERT_TRUE(uavReaderResult);
    const Core::GpuTaskId uavReader = *uavReaderResult;
    const auto shaderReaderResult = AddTask(Name("tests/task_graph/planned_shader_reader"), "Shader Reader", {}, Span(shaderReadUse), {}, {}, {}, graph);
    ASSERT_TRUE(shaderReaderResult);
    const Core::GpuTaskId shaderReader = *shaderReaderResult;
    ASSERT_TRUE(writer.valid());
    ASSERT_TRUE(uavReader.valid());
    ASSERT_TRUE(shaderReader.valid());

    SingleQueueCompile singleQueueCompile(testArena);
    ASSERT_TRUE(singleQueueCompile.compile(graph));
    const Core::GpuCompiledGraph::ReadView compiledPlan(singleQueueCompile.compiledGraph);


    const Core::GpuCompiledTask* const compiledWriter = compiledPlan.findTask(writer).plan;
    const Core::GpuCompiledTask* const compiledUavReader = compiledPlan.findTask(uavReader).plan;
    const Core::GpuCompiledTask* const compiledShaderReader = compiledPlan.findTask(shaderReader).plan;
    ASSERT_NE(compiledWriter, nullptr);
    ASSERT_NE(compiledUavReader, nullptr);
    ASSERT_NE(compiledShaderReader, nullptr);
    ASSERT_EQ(compiledWriter->prologueStateSeedCount, 0u);
    ASSERT_EQ(compiledUavReader->prologueStateSeedCount, 1u);
    ASSERT_EQ(compiledShaderReader->prologueStateSeedCount, 1u);
    ASSERT_EQ(compiledWriter->prologueBarrierCount, 1u);
    ASSERT_EQ(compiledUavReader->prologueBarrierCount, 1u);
    ASSERT_EQ(compiledShaderReader->prologueBarrierCount, 1u);

    const Core::GpuCompiledBarrier* const writerBarrier = compiledPlan.findTask(writer).prologueBarriers;
    const Core::GpuCompiledBarrier* const uavBarrier = compiledPlan.findTask(uavReader).prologueBarriers;
    const Core::GpuCompiledBarrier* const shaderBarrier = compiledPlan.findTask(shaderReader).prologueBarriers;
    const Core::GpuPacketStateSeed* const uavSeed = compiledPlan.findTask(uavReader).prologueStateSeeds;
    const Core::GpuPacketStateSeed* const shaderSeed = compiledPlan.findTask(shaderReader).prologueStateSeeds;
    ASSERT_NE(writerBarrier, nullptr);
    ASSERT_NE(uavBarrier, nullptr);
    ASSERT_NE(shaderBarrier, nullptr);
    ASSERT_NE(uavSeed, nullptr);
    ASSERT_NE(shaderSeed, nullptr);
    EXPECT_EQ(uavSeed[0].resource, texture);
    EXPECT_EQ(uavSeed[0].sourcePacket, compiledWriter->packet);
    EXPECT_EQ(shaderSeed[0].resource, texture);
    EXPECT_EQ(shaderSeed[0].sourcePacket, compiledUavReader->packet);
    EXPECT_EQ(writerBarrier[0].type, Core::GpuCompiledBarrierType::TextureTransition);
    EXPECT_EQ(writerBarrier[0].before, Core::ResourceStates::Common);
    EXPECT_EQ(writerBarrier[0].after, Core::ResourceStates::UnorderedAccess);
    EXPECT_FALSE(writerBarrier[0].forceMemoryDependency);
    EXPECT_EQ(uavBarrier[0].type, Core::GpuCompiledBarrierType::TextureUav);
    EXPECT_EQ(uavBarrier[0].before, Core::ResourceStates::UnorderedAccess);
    EXPECT_EQ(uavBarrier[0].after, Core::ResourceStates::UnorderedAccess);
    EXPECT_TRUE(uavBarrier[0].forceMemoryDependency);
    EXPECT_EQ(shaderBarrier[0].type, Core::GpuCompiledBarrierType::TextureTransition);
    EXPECT_EQ(shaderBarrier[0].before, Core::ResourceStates::UnorderedAccess);
    EXPECT_EQ(shaderBarrier[0].after, Core::ResourceStates::ShaderResource);
    EXPECT_FALSE(shaderBarrier[0].forceMemoryDependency);
}

TEST(GpuTaskGraph, PlansCompositeUavDependencies){
    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto textureResult = AddTextureMetadata(graph, Name("tests/task_graph/composite_uav_texture"), "Composite UAV Texture");
    ASSERT_TRUE(textureResult);
    const Core::GpuGraphResourceId texture = *textureResult;
    const auto bufferResult = AddBufferMetadata(graph, Name("tests/task_graph/composite_uav_buffer"), "Composite UAV Buffer");
    ASSERT_TRUE(bufferResult);
    const Core::GpuGraphResourceId buffer = *bufferResult;
    ASSERT_TRUE(texture.valid());
    ASSERT_TRUE(buffer.valid());

    const Core::ResourceStates::Mask compositeState = Core::ResourceStates::UnorderedAccess | Core::ResourceStates::ShaderResource;
    const Array<Core::GpuTaskResourceUse, 2> producerUses = {
        Core::GpuTaskResourceUse{ .resource = texture, .range = {}, .requiredState = compositeState, .access = Core::GpuTaskResourceAccess::Write, },
        Core::GpuTaskResourceUse{ .resource = buffer, .range = {}, .requiredState = compositeState, .access = Core::GpuTaskResourceAccess::Write, },
    };
    const Array<Core::GpuTaskResourceUse, 2> consumerUses = {
        Core::GpuTaskResourceUse{ .resource = texture, .range = {}, .requiredState = compositeState, .access = Core::GpuTaskResourceAccess::Read, },
        Core::GpuTaskResourceUse{ .resource = buffer, .range = {}, .requiredState = compositeState, .access = Core::GpuTaskResourceAccess::Read, },
    };
    const auto producerResult = AddTask(Name("tests/task_graph/composite_uav_producer"), "Composite UAV Producer", {}, Span(producerUses), {}, {}, {}, graph);
    ASSERT_TRUE(producerResult);
    const Core::GpuTaskId producer = *producerResult;
    const auto consumerResult = AddTask(Name("tests/task_graph/composite_uav_consumer"), "Composite UAV Consumer", {}, Span(consumerUses), {}, {}, {}, graph);
    ASSERT_TRUE(consumerResult);
    const Core::GpuTaskId consumer = *consumerResult;
    ASSERT_TRUE(producer.valid());
    ASSERT_TRUE(consumer.valid());

    SingleQueueCompile singleQueueCompile(testArena);
    ASSERT_TRUE(singleQueueCompile.compile(graph));
    const Core::GpuCompiledGraph::ReadView compiledPlan(singleQueueCompile.compiledGraph);


    const Core::GpuCompiledTask* const compiledProducer = compiledPlan.findTask(producer).plan;
    const Core::GpuCompiledTask* const compiledConsumer = compiledPlan.findTask(consumer).plan;
    ASSERT_NE(compiledProducer, nullptr);
    ASSERT_NE(compiledConsumer, nullptr);
    ASSERT_EQ(compiledConsumer->prologueStateSeedCount, 2u);
    ASSERT_EQ(compiledConsumer->prologueBarrierCount, 2u);

    const Core::GpuCompiledBarrier* const consumerBarriers = compiledPlan.findTask(consumer).prologueBarriers;
    ASSERT_NE(consumerBarriers, nullptr);
    b8 hasTextureUavBarrier = false;
    b8 hasBufferUavBarrier = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledConsumer->prologueBarrierCount; ++barrierIndex){
        const Core::GpuCompiledBarrier& barrier = consumerBarriers[barrierIndex];
        hasTextureUavBarrier = hasTextureUavBarrier || (barrier.resource == texture && barrier.type == Core::GpuCompiledBarrierType::TextureUav && barrier.before == compositeState && barrier.after == compositeState);
        hasBufferUavBarrier = hasBufferUavBarrier || (barrier.resource == buffer && barrier.type == Core::GpuCompiledBarrierType::BufferUav && barrier.before == compositeState && barrier.after == compositeState);
    }
    EXPECT_TRUE(hasTextureUavBarrier);
    EXPECT_TRUE(hasBufferUavBarrier);
}

TEST(GpuTaskGraph, TracksFinalOverlappingIntraTaskTextureStateForConsumersAndExports){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    const Array<Core::GpuPhysicalQueueInfo, 1> queues = { GraphicsQueue() };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues), };

    {
        Core::GpuTaskGraph graph(testArena.arena);
        const auto textureResult = AddTextureMetadata(graph, Name("tests/task_graph/intra_task_final_state_consumer"), "Intra-Task Final State Consumer");
        ASSERT_TRUE(textureResult);
        const Core::GpuGraphResourceId texture = *textureResult;
        ASSERT_TRUE(texture.valid());

        const Array<Core::GpuTaskResourceUse, 2> producerUses = {
            Core::GpuTaskResourceUse{ .resource = texture, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, },
            Core::GpuTaskResourceUse{ .resource = texture, .range = {}, .requiredState = Core::ResourceStates::RenderTarget, .access = Core::GpuTaskResourceAccess::Write, },
        };
        const Core::GpuTaskResourceUse consumerUse{ .resource = texture, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, };
        const auto producerResult = AddTask(Name("tests/task_graph/intra_task_final_state_producer"), "Intra-Task Final State Producer", {}, Span(producerUses), {}, {}, {}, graph);
        ASSERT_TRUE(producerResult);
        const Core::GpuTaskId producer = *producerResult;
        const auto consumerResult = AddTask(Name("tests/task_graph/intra_task_final_state_consumer_task"), "Intra-Task Final State Consumer Task", {}, Span(&consumerUse, 1u), {}, {}, {}, graph);
        ASSERT_TRUE(consumerResult);
        const Core::GpuTaskId consumer = *consumerResult;
        ASSERT_TRUE(producer.valid());
        ASSERT_TRUE(consumer.valid());

        Core::GpuTaskGraphAnalysis analysis(testArena.arena);
        Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Core::GpuCompiledGraph compiledGraph(testArena.arena);
        ASSERT_TRUE(CompileWithSeparatedCommandQueues(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
        const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

        const Core::GpuCompiledTaskView compiledProducerView = compiledPlan.findTask(producer);
        const Core::GpuCompiledTaskView compiledConsumerView = compiledPlan.findTask(consumer);
        const Core::GpuCompiledTask* const compiledProducer = compiledProducerView.plan;
        const Core::GpuCompiledTask* const compiledConsumer = compiledConsumerView.plan;
        ASSERT_NE(compiledProducer, nullptr);
        ASSERT_NE(compiledConsumer, nullptr);
        EXPECT_NE(compiledProducer->packet, compiledConsumer->packet);
        ASSERT_EQ(compiledProducer->prologueBarrierCount, 1u);
        ASSERT_EQ(compiledConsumer->prologueStateSeedCount, 1u);
        ASSERT_EQ(compiledConsumer->prologueBarrierCount, 1u);

        const Core::GpuCompiledBarrier* const producerBarrier = compiledProducerView.prologueBarriers;
        const Core::GpuPacketStateSeed* const consumerSeed = compiledConsumerView.prologueStateSeeds;
        const Core::GpuCompiledBarrier* const consumerBarrier = compiledConsumerView.prologueBarriers;
        ASSERT_NE(producerBarrier, nullptr);
        ASSERT_NE(consumerSeed, nullptr);
        ASSERT_NE(consumerBarrier, nullptr);
        EXPECT_EQ(producerBarrier[0u].after, Core::ResourceStates::ShaderResource);
        EXPECT_EQ(consumerSeed[0u].sourcePacket, compiledProducer->packet);
        EXPECT_EQ(consumerBarrier[0u].type, Core::GpuCompiledBarrierType::TextureTransition);
        EXPECT_EQ(consumerBarrier[0u].before, Core::ResourceStates::RenderTarget);
        EXPECT_EQ(consumerBarrier[0u].after, Core::ResourceStates::ShaderResource);
    }

    {
        Core::GpuTaskGraph graph(testArena.arena);
        const auto textureResult = graph.importResource(
            Core::GpuGraphResourceDesc{}
                .setIdentity(Name("tests/task_graph/intra_task_final_state_export"))
                .setMarkerLabel("Intra-Task Final State Export")
                .setType(Core::GpuGraphResourceType::Texture)
                .setInitialState(Core::ResourceStates::Common)
                .setExternalFinalState(Core::ResourceStates::ShaderResource)
        );
        ASSERT_TRUE(textureResult);
        const Core::GpuGraphResourceId texture = *textureResult;
        ASSERT_TRUE(texture.valid());

        const Array<Core::GpuTaskResourceUse, 2> uses = {
            Core::GpuTaskResourceUse{ .resource = texture, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, },
            Core::GpuTaskResourceUse{ .resource = texture, .range = {}, .requiredState = Core::ResourceStates::RenderTarget, .access = Core::GpuTaskResourceAccess::Write, },
        };
        const auto producerResult = AddTask(Name("tests/task_graph/intra_task_final_state_export_task"), "Intra-Task Final State Export Task", {}, Span(uses), {}, {}, {}, graph);
        ASSERT_TRUE(producerResult);
        const Core::GpuTaskId producer = *producerResult;
        ASSERT_TRUE(producer.valid());

        Core::GpuTaskGraphAnalysis analysis(testArena.arena);
        Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Core::GpuCompiledGraph compiledGraph(testArena.arena);
        ASSERT_TRUE(CompileWithSeparatedCommandQueues(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
        const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

        const Core::GpuCompiledTaskView compiledProducerView = compiledPlan.findTask(producer);
        const Core::GpuCompiledTask* const compiledProducer = compiledProducerView.plan;
        ASSERT_NE(compiledProducer, nullptr);
        ASSERT_EQ(compiledProducer->epilogueBarrierCount, 1u);
        const Core::GpuCompiledBarrier* const exportBarrier = compiledProducerView.epilogueBarriers;
        ASSERT_NE(exportBarrier, nullptr);
        EXPECT_EQ(exportBarrier[0u].type, Core::GpuCompiledBarrierType::TextureStateExport);
        EXPECT_EQ(exportBarrier[0u].before, Core::ResourceStates::RenderTarget);
        EXPECT_EQ(exportBarrier[0u].after, Core::ResourceStates::ShaderResource);
    }
}

TEST(GpuTaskGraph, PlansGraphInitialStateForUncoveredLaterTextureSubresourcesWithinTask){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    const Array<Core::GpuPhysicalQueueInfo, 1> queues = { GraphicsQueue() };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues), };
    Core::GpuTaskGraph graph(testArena.arena);
    const auto textureResult = graph.importResource(
        Core::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/intra_task_first_use_texture_tail"))
            .setMarkerLabel("Intra-Task First-Use Texture Tail")
            .setType(Core::GpuGraphResourceType::Texture)
            .setInitialState(Core::ResourceStates::CopySource)
    );
    ASSERT_TRUE(textureResult);
    const Core::GpuGraphResourceId texture = *textureResult;
    ASSERT_TRUE(texture.valid());

    const Core::GpuTaskResourceRange mipZeroRange{ .textureSubresources = Core::TextureSubresourceSet(0u, 1u, 0u, Core::TextureSubresourceSet::s_AllArraySlices), };
    const Core::GpuTaskResourceRange allMipsRange{ .textureSubresources = Core::s_AllSubresources, };
    const Core::GpuTaskResourceRange unplannedTailRange{ .textureSubresources = Core::TextureSubresourceSet(1u, Core::TextureSubresourceSet::s_AllMipLevels, 0u, Core::TextureSubresourceSet::s_AllArraySlices), };
    const Array<Core::GpuTaskResourceUse, 2> uses = {
        Core::GpuTaskResourceUse{ .resource = texture, .range = mipZeroRange, .requiredState = Core::ResourceStates::CopyDest, .access = Core::GpuTaskResourceAccess::Write, },
        Core::GpuTaskResourceUse{ .resource = texture, .range = allMipsRange, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, },
    };
    const auto taskResult = AddTask(Name("tests/task_graph/intra_task_first_use_texture_tail_task"), "Intra-Task First-Use Texture Tail Task", {}, Span(uses), {}, {}, {}, graph);
    ASSERT_TRUE(taskResult);
    const Core::GpuTaskId task = *taskResult;
    ASSERT_TRUE(task.valid());

    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
    const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);


    const Core::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;
    ASSERT_NE(compiledTask, nullptr);
    ASSERT_EQ(compiledTask->prologueBarrierCount, 2u);
    const Core::GpuCompiledBarrier* const barriers = compiledPlan.findTask(task).prologueBarriers;
    ASSERT_NE(barriers, nullptr);

    b8 hasMipZeroInitialBarrier = false;
    b8 hasTailInitialBarrier = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledTask->prologueBarrierCount; ++barrierIndex){
        const Core::GpuCompiledBarrier& barrier = barriers[barrierIndex];
        if(
            barrier.type == Core::GpuCompiledBarrierType::TextureTransition
            && barrier.resource == texture
            && barrier.before == Core::ResourceStates::CopySource
            && barrier.after == Core::ResourceStates::CopyDest
            && barrier.isGraphInitialState
            && barrier.range.textureSubresources == mipZeroRange.textureSubresources
        )
            hasMipZeroInitialBarrier = true;
        if(
            barrier.type == Core::GpuCompiledBarrierType::TextureTransition
            && barrier.resource == texture
            && barrier.before == Core::ResourceStates::CopySource
            && barrier.after == Core::ResourceStates::ShaderResource
            && barrier.isGraphInitialState
            && barrier.range.textureSubresources == unplannedTailRange.textureSubresources
        )
            hasTailInitialBarrier = true;
    }
    EXPECT_TRUE(hasMipZeroInitialBarrier);
    EXPECT_TRUE(hasTailInitialBarrier);
}

TEST(GpuTaskGraph, AllowsIndependentConcurrentReadStateSources){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);

    const auto importTexture = [&](const Name& identity, const AStringView label, const Core::ResourceQueueSharing::Mask queueSharing, const Core::ResourceStates::Mask initialState = Core::ResourceStates::Common){
        Core::GpuGraphResourceDesc desc;
        desc.setIdentity(identity).setMarkerLabel(label).setType(Core::GpuGraphResourceType::Texture).setInitialState(initialState).setQueueSharing(queueSharing);
        return graph.importResource(desc);
    };
    const auto concurrentTextureResult = importTexture(Name("tests/task_graph/concurrent_read_only"), "Concurrent Read Only", Core::ResourceQueueSharing::GraphicsAndAsyncCompute, Core::ResourceStates::ShaderResource);
    ASSERT_TRUE(concurrentTextureResult);
    const Core::GpuGraphResourceId concurrentTexture = *concurrentTextureResult;
    const auto defaultConcurrentTextureResult = importTexture(Name("tests/task_graph/default_concurrent_read_only"), "Default Concurrent Read Only", Core::ResourceQueueSharing::GraphicsAndAsyncCompute);
    ASSERT_TRUE(defaultConcurrentTextureResult);
    const Core::GpuGraphResourceId defaultConcurrentTexture = *defaultConcurrentTextureResult;
    const auto exclusiveTextureResult = importTexture(Name("tests/task_graph/exclusive_read_only"), "Exclusive Read Only", Core::ResourceQueueSharing::Exclusive);
    ASSERT_TRUE(exclusiveTextureResult);
    const Core::GpuGraphResourceId exclusiveTexture = *exclusiveTextureResult;
    ASSERT_TRUE(concurrentTexture.valid());
    ASSERT_TRUE(defaultConcurrentTexture.valid());
    ASSERT_TRUE(exclusiveTexture.valid());

    Core::GpuTaskSchedulingHint scheduling;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    const Core::GpuTaskCommandRequirements graphicsCommands = GraphicsCommands();
    const Core::GpuTaskCommandRequirements computeCommands = ComputeCommands();
    const auto addReadTask = [&](const Name& identity, const AStringView label, const Core::GpuGraphResourceId resource, const Core::GpuTaskCommandRequirements& commands, const b8 hasIndependentStateSource){
        const Array<Core::GpuTaskResourceUse, 1> uses = {
            Core::GpuTaskResourceUse{ .resource = resource, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, .hasIndependentStateSource = hasIndependentStateSource, },
        };
        Core::GpuTaskDesc desc;
        desc.setIdentity(identity).setMarkerLabel(label).setScheduling(scheduling).setResourceUses(uses);
        return graph.addTask(desc, commands);
    };
    const auto concurrentGraphicsResult = addReadTask(Name("tests/task_graph/concurrent_read_graphics"), "Concurrent Graphics Read", concurrentTexture, graphicsCommands, false);
    ASSERT_TRUE(concurrentGraphicsResult);
    const Core::GpuTaskId concurrentGraphics = *concurrentGraphicsResult;
    const auto concurrentComputeResult = addReadTask(Name("tests/task_graph/concurrent_read_compute"), "Concurrent Compute Read", concurrentTexture, computeCommands, true);
    ASSERT_TRUE(concurrentComputeResult);
    const Core::GpuTaskId concurrentCompute = *concurrentComputeResult;
    const auto defaultConcurrentGraphicsResult = addReadTask(Name("tests/task_graph/default_concurrent_read_graphics"), "Default Concurrent Graphics Read", defaultConcurrentTexture, graphicsCommands, false);
    ASSERT_TRUE(defaultConcurrentGraphicsResult);
    const Core::GpuTaskId defaultConcurrentGraphics = *defaultConcurrentGraphicsResult;
    const auto defaultConcurrentComputeResult = addReadTask(Name("tests/task_graph/default_concurrent_read_compute"), "Default Concurrent Compute Read", defaultConcurrentTexture, computeCommands, false);
    ASSERT_TRUE(defaultConcurrentComputeResult);
    const Core::GpuTaskId defaultConcurrentCompute = *defaultConcurrentComputeResult;
    const auto exclusiveGraphicsResult = addReadTask(Name("tests/task_graph/exclusive_read_graphics"), "Exclusive Graphics Read", exclusiveTexture, graphicsCommands, false);
    ASSERT_TRUE(exclusiveGraphicsResult);
    const Core::GpuTaskId exclusiveGraphics = *exclusiveGraphicsResult;
    const auto exclusiveComputeResult = addReadTask(Name("tests/task_graph/exclusive_read_compute"), "Exclusive Compute Read", exclusiveTexture, computeCommands, true);
    ASSERT_TRUE(exclusiveComputeResult);
    const Core::GpuTaskId exclusiveCompute = *exclusiveComputeResult;
    ASSERT_TRUE(concurrentGraphics.valid());
    ASSERT_TRUE(concurrentCompute.valid());
    ASSERT_TRUE(defaultConcurrentGraphics.valid());
    ASSERT_TRUE(defaultConcurrentCompute.valid());
    ASSERT_TRUE(exclusiveGraphics.valid());
    ASSERT_TRUE(exclusiveCompute.valid());

    const Array<Core::GpuPhysicalQueueInfo, 2> queues = { GraphicsQueue(), DedicatedComputeQueue(), };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues), };
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
    EXPECT_EQ(FindEdge(analysis, concurrentGraphics, concurrentCompute), nullptr);
    EXPECT_EQ(FindEdge(analysis, defaultConcurrentGraphics, defaultConcurrentCompute), nullptr);
    EXPECT_EQ(FindEdge(analysis, exclusiveGraphics, exclusiveCompute), nullptr);

    {
        const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

        ASSERT_EQ(compiledPlan.packetCount(), 6u);
        const Core::GpuSubmissionPacketId concurrentGraphicsPacket = compiledPlan.packetForTask(concurrentGraphics);
        const Core::GpuSubmissionPacketId concurrentComputePacket = compiledPlan.packetForTask(concurrentCompute);
        const Core::GpuSubmissionPacketId defaultConcurrentGraphicsPacket = compiledPlan.packetForTask(defaultConcurrentGraphics);
        const Core::GpuSubmissionPacketId defaultConcurrentComputePacket = compiledPlan.packetForTask(defaultConcurrentCompute);
        const Core::GpuSubmissionPacketId exclusiveGraphicsPacket = compiledPlan.packetForTask(exclusiveGraphics);
        const Core::GpuSubmissionPacketId exclusiveComputePacket = compiledPlan.packetForTask(exclusiveCompute);
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

        const Core::GpuCompiledTaskView concurrentComputeView = compiledPlan.findTask(concurrentCompute);
        const Core::GpuCompiledTaskView defaultConcurrentComputeView = compiledPlan.findTask(defaultConcurrentCompute);
        const Core::GpuCompiledTaskView exclusiveGraphicsView = compiledPlan.findTask(exclusiveGraphics);
        const Core::GpuCompiledTaskView exclusiveComputeView = compiledPlan.findTask(exclusiveCompute);
        const Core::GpuCompiledTask* const compiledConcurrentCompute = concurrentComputeView.plan;
        const Core::GpuCompiledTask* const compiledDefaultConcurrentCompute = defaultConcurrentComputeView.plan;
        const Core::GpuCompiledTask* const compiledExclusiveGraphics = exclusiveGraphicsView.plan;
        const Core::GpuCompiledTask* const compiledExclusiveCompute = exclusiveComputeView.plan;
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
        const Core::GpuPacketStateSeed* const defaultConcurrentSeed = defaultConcurrentComputeView.prologueStateSeeds;
        const Core::GpuPacketStateSeed* const exclusiveSeed = exclusiveComputeView.prologueStateSeeds;
        const Core::GpuCompiledBarrier* const exclusiveAcquire = exclusiveComputeView.prologueBarriers;
        const Core::GpuCompiledBarrier* const exclusiveRelease = exclusiveGraphicsView.epilogueBarriers;
        EXPECT_EQ(concurrentComputeView.prologueStateSeeds, nullptr);
        ASSERT_NE(defaultConcurrentSeed, nullptr);
        ASSERT_NE(exclusiveSeed, nullptr);
        ASSERT_NE(exclusiveAcquire, nullptr);
        ASSERT_NE(exclusiveRelease, nullptr);
        EXPECT_EQ(defaultConcurrentSeed[0u].sourcePacket, defaultConcurrentGraphicsPacket);
        EXPECT_EQ(exclusiveSeed[0u].sourcePacket, exclusiveGraphicsPacket);
        EXPECT_EQ(exclusiveAcquire[0u].type, Core::GpuCompiledBarrierType::TextureOwnershipAcquire);
        EXPECT_EQ(exclusiveAcquire[0u].sourceQueue, compiledExclusiveGraphics->queue);
        EXPECT_EQ(exclusiveAcquire[0u].destinationQueue, compiledExclusiveCompute->queue);
        EXPECT_FALSE(exclusiveAcquire[0u].isInitialOwnerHandoff);
        EXPECT_EQ(exclusiveRelease[0u].type, Core::GpuCompiledBarrierType::TextureOwnershipRelease);
        EXPECT_EQ(exclusiveRelease[0u].sourceQueue, compiledExclusiveGraphics->queue);
        EXPECT_EQ(exclusiveRelease[0u].destinationQueue, compiledExclusiveCompute->queue);
        EXPECT_EQ(compiledPlan.packet(concurrentComputePacket).plan->dependencyCount, 0u);
        const Core::GpuCompiledPacketView defaultConcurrentComputePacketView = compiledPlan.packet(defaultConcurrentComputePacket);
        ASSERT_TRUE(defaultConcurrentComputePacketView.valid());
        ASSERT_EQ(defaultConcurrentComputePacketView.plan->dependencyCount, 1u);
        EXPECT_EQ(defaultConcurrentComputePacketView.dependencies[0u].producer, defaultConcurrentGraphicsPacket);
        const Core::GpuCompiledPacketView exclusiveComputePacketView = compiledPlan.packet(exclusiveComputePacket);
        ASSERT_TRUE(exclusiveComputePacketView.valid());
        ASSERT_EQ(exclusiveComputePacketView.plan->dependencyCount, 1u);
        EXPECT_EQ(exclusiveComputePacketView.dependencies[0u].producer, exclusiveGraphicsPacket);
    }

    // Queues in one family retain exclusive ownership and require a handoff even with combined sharing flags.
    Core::GpuPhysicalQueueInfo sameFamilyComputeQueue = DedicatedComputeQueue();
    sameFamilyComputeQueue.familyIndex = GraphicsQueue().familyIndex;
    sameFamilyComputeQueue.queueIndex = 1u;
    const Array<Core::GpuPhysicalQueueInfo, 2> sameFamilyQueues = { GraphicsQueue(), sameFamilyComputeQueue, };
    const Core::GpuPhysicalQueueTopology sameFamilyTopology{ .queues = sameFamilyQueues.data(), .queueCount = LengthOf(sameFamilyQueues), };
    Core::GpuTaskGraphAnalysis sameFamilyAnalysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments sameFamilyAssignments(testArena.arena);
    Core::GpuCompiledGraph sameFamilyCompiledGraph(testArena.arena);
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(sameFamilyAnalysis, sameFamilyAssignments, sameFamilyCompiledGraph, operationScratchArena, graph, sameFamilyTopology));
    const Core::GpuCompiledGraph::ReadView sameFamilyPlan(sameFamilyCompiledGraph);
    const Core::GpuSubmissionPacketId sameFamilyGraphicsPacket = sameFamilyPlan.packetForTask(concurrentGraphics);
    const Core::GpuSubmissionPacketId sameFamilyComputePacket = sameFamilyPlan.packetForTask(concurrentCompute);
    const Core::GpuCompiledTaskView sameFamilyCompiledComputeView = sameFamilyPlan.findTask(concurrentCompute);
    const Core::GpuCompiledTask* const sameFamilyCompiledCompute = sameFamilyCompiledComputeView.plan;
    ASSERT_TRUE(sameFamilyGraphicsPacket.valid());
    ASSERT_TRUE(sameFamilyComputePacket.valid());
    ASSERT_NE(sameFamilyCompiledCompute, nullptr);
    ASSERT_EQ(sameFamilyCompiledCompute->prologueStateSeedCount, 1u);
    const Core::GpuPacketStateSeed* const sameFamilySeed = sameFamilyCompiledComputeView.prologueStateSeeds;
    ASSERT_NE(sameFamilySeed, nullptr);
    EXPECT_EQ(sameFamilySeed[0u].sourcePacket, sameFamilyGraphicsPacket);
    const Core::GpuCompiledPacketView sameFamilyComputePacketView = sameFamilyPlan.packet(sameFamilyComputePacket);
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

