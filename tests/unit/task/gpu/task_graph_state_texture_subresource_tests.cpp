// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_state_texture_subresource_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, PlansTextureStatesPerDeclaredSubresourceRange){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId texture = AddTextureMetadata(
        graph,
        Name("tests/task_graph/subresource_states"),
        "Subresource States"
    );
    ASSERT_TRUE(texture.valid());

    const Graphics::GpuTaskResourceUse firstUse[] = {
        Graphics::GpuTaskResourceUse{
            .resource = texture,
            .range = Graphics::GpuTaskResourceRange{
                .textureSubresources = Graphics::TextureSubresourceSet{ 0u, 1u, 0u, 1u },
            },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse secondUse[] = {
        Graphics::GpuTaskResourceUse{
            .resource = texture,
            .range = Graphics::GpuTaskResourceRange{
                .textureSubresources = Graphics::TextureSubresourceSet{ 1u, 1u, 0u, 1u },
            },
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    const Graphics::GpuTaskId first = AddTask(
        graph,
        Name("tests/task_graph/subresource_first"),
        "First Subresource",
        nullptr,
        0u,
        firstUse,
        LengthOf(firstUse)
    );
    const Graphics::GpuTaskId second = AddTask(
        graph,
        Name("tests/task_graph/subresource_second"),
        "Second Subresource",
        nullptr,
        0u,
        secondUse,
        LengthOf(secondUse)
    );
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());

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


    const Graphics::GpuCompiledBarrier* const secondBarrier = compiledPlan.findTask(second).prologueBarriers;
    ASSERT_NE(secondBarrier, nullptr);
    EXPECT_EQ(secondBarrier[0].type, Graphics::GpuCompiledBarrierType::TextureTransition);
    EXPECT_EQ(secondBarrier[0].before, Graphics::ResourceStates::Common);
    EXPECT_EQ(secondBarrier[0].after, Graphics::ResourceStates::ShaderResource);
}

TEST(GpuTaskGraph, FansInTerminalTextureStateFragmentsForBroadCrossQueueConsumer){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId texture = AddTextureMetadata(
        graph,
        Name("tests/task_graph/fragment_fan_in_texture"),
        "Fragment Fan-In Texture"
    );
    ASSERT_TRUE(texture.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
        DedicatedTransferQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
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
    const Graphics::GpuQueueRequest transferRequest{
        Graphics::GpuQueueCapability::Transfer,
        Graphics::GpuQueuePreference::Transfer,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;

    const Graphics::GpuTaskResourceRange transferRange{
        .textureSubresources = Graphics::TextureSubresourceSet(
            0u,
            1u,
            0u,
            Graphics::TextureSubresourceSet::AllArraySlices
        ),
    };
    const Graphics::GpuTaskResourceRange computeRange{
        .textureSubresources = Graphics::TextureSubresourceSet(
            1u,
            1u,
            0u,
            Graphics::TextureSubresourceSet::AllArraySlices
        ),
    };
    const Graphics::GpuTaskResourceRange broadRange{
        .textureSubresources = Graphics::s_AllSubresources,
    };
    const Graphics::GpuTaskResourceRange initialTailRange{
        .textureSubresources = Graphics::TextureSubresourceSet(
            2u,
            Graphics::TextureSubresourceSet::AllMipLevels,
            0u,
            Graphics::TextureSubresourceSet::AllArraySlices
        ),
    };
    const Graphics::GpuTaskResourceUse transferUse{
        .resource = texture,
        .range = transferRange,
        .requiredState = Graphics::ResourceStates::CopyDest,
        .access = Graphics::GpuTaskResourceAccess::Write,
    };
    const Graphics::GpuTaskResourceUse computeUse{
        .resource = texture,
        .range = computeRange,
        .requiredState = Graphics::ResourceStates::UnorderedAccess,
        .access = Graphics::GpuTaskResourceAccess::Write,
    };
    const Graphics::GpuTaskResourceUse consumerUse{
        .resource = texture,
        .range = broadRange,
        .requiredState = Graphics::ResourceStates::ShaderResource,
        .access = Graphics::GpuTaskResourceAccess::Read,
    };

    Graphics::GpuTaskDesc transferDesc;
    transferDesc
        .setIdentity(Name("tests/task_graph/fragment_fan_in_transfer"))
        .setMarkerLabel("Fragment Fan-In Transfer")
        .setQueue(transferRequest)
        .setScheduling(scheduling)
        .setResourceUses(&transferUse, 1u)
    ;
    Graphics::GpuTaskDesc computeDesc;
    computeDesc
        .setIdentity(Name("tests/task_graph/fragment_fan_in_compute"))
        .setMarkerLabel("Fragment Fan-In Compute")
        .setQueue(computeRequest)
        .setScheduling(scheduling)
        .setResourceUses(&computeUse, 1u)
    ;
    Graphics::GpuTaskDesc consumerDesc;
    consumerDesc
        .setIdentity(Name("tests/task_graph/fragment_fan_in_consumer"))
        .setMarkerLabel("Fragment Fan-In Consumer")
        .setQueue(graphicsRequest)
        .setScheduling(scheduling)
        .setResourceUses(&consumerUse, 1u)
    ;
    const Graphics::GpuTaskId transferTask = graph.addTask(transferDesc);
    const Graphics::GpuTaskId computeTask = graph.addTask(computeDesc);
    const Graphics::GpuTaskId consumerTask = graph.addTask(consumerDesc);
    ASSERT_TRUE(transferTask.valid());
    ASSERT_TRUE(computeTask.valid());
    ASSERT_TRUE(consumerTask.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);


    const Graphics::GpuCompiledTask* const compiledTransfer = compiledPlan.findTask(transferTask).plan;
    const Graphics::GpuCompiledTask* const compiledCompute = compiledPlan.findTask(computeTask).plan;
    const Graphics::GpuCompiledTask* const compiledConsumer = compiledPlan.findTask(consumerTask).plan;
    ASSERT_NE(compiledTransfer, nullptr);
    ASSERT_NE(compiledCompute, nullptr);
    ASSERT_NE(compiledConsumer, nullptr);
    EXPECT_EQ(compiledTransfer->queue, queues[2u].id);
    EXPECT_EQ(compiledCompute->queue, queues[1u].id);
    EXPECT_EQ(compiledConsumer->queue, queues[0u].id);

    const Graphics::GpuSubmissionPacketId transferPacket = compiledTransfer->packet;
    const Graphics::GpuSubmissionPacketId computePacket = compiledCompute->packet;
    const Graphics::GpuSubmissionPacketId consumerPacket = compiledConsumer->packet;
    ASSERT_TRUE(transferPacket.valid());
    ASSERT_TRUE(computePacket.valid());
    ASSERT_TRUE(consumerPacket.valid());
    ASSERT_EQ(compiledConsumer->prologueStateSeedCount, 2u);
    const Graphics::GpuPacketStateSeed* const seeds = compiledPlan.findTask(consumerTask).prologueStateSeeds;
    ASSERT_NE(seeds, nullptr);
    bool hasTransferSeed = false;
    bool hasComputeSeed = false;
    for(u32 seedIndex = 0u; seedIndex < compiledConsumer->prologueStateSeedCount; ++seedIndex){
        const Graphics::GpuPacketStateSeed& seed = seeds[seedIndex];
        hasTransferSeed = hasTransferSeed || (
            seed.resource == texture
            && seed.range.textureSubresources == transferRange.textureSubresources
            && seed.sourcePacket == transferPacket
        );
        hasComputeSeed = hasComputeSeed || (
            seed.resource == texture
            && seed.range.textureSubresources == computeRange.textureSubresources
            && seed.sourcePacket == computePacket
        );
        EXPECT_NE(seed.range.textureSubresources, broadRange.textureSubresources);
    }
    EXPECT_TRUE(hasTransferSeed);
    EXPECT_TRUE(hasComputeSeed);

    ASSERT_EQ(compiledPlan.packet(consumerPacket).plan->dependencyCount, 2u);
    const Graphics::GpuPacketDependency* const dependencies = compiledPlan.packet(consumerPacket).dependencies;
    ASSERT_NE(dependencies, nullptr);
    bool waitsForTransfer = false;
    bool waitsForCompute = false;
    for(u32 dependencyIndex = 0u; dependencyIndex < compiledPlan.packet(consumerPacket).plan->dependencyCount; ++dependencyIndex){
        waitsForTransfer = waitsForTransfer || dependencies[dependencyIndex].producer == transferPacket;
        waitsForCompute = waitsForCompute || dependencies[dependencyIndex].producer == computePacket;
    }
    EXPECT_TRUE(waitsForTransfer);
    EXPECT_TRUE(waitsForCompute);

    ASSERT_EQ(compiledTransfer->epilogueBarrierCount, 1u);
    ASSERT_EQ(compiledCompute->epilogueBarrierCount, 1u);
    const Graphics::GpuCompiledBarrier* const transferRelease = compiledPlan.findTask(transferTask).epilogueBarriers;
    const Graphics::GpuCompiledBarrier* const computeRelease = compiledPlan.findTask(computeTask).epilogueBarriers;
    ASSERT_NE(transferRelease, nullptr);
    ASSERT_NE(computeRelease, nullptr);
    EXPECT_EQ(transferRelease[0u].type, Graphics::GpuCompiledBarrierType::TextureOwnershipRelease);
    EXPECT_EQ(transferRelease[0u].range.textureSubresources, transferRange.textureSubresources);
    EXPECT_EQ(transferRelease[0u].sourceQueue, queues[2u].id);
    EXPECT_EQ(transferRelease[0u].destinationQueue, queues[0u].id);
    EXPECT_EQ(computeRelease[0u].type, Graphics::GpuCompiledBarrierType::TextureOwnershipRelease);
    EXPECT_EQ(computeRelease[0u].range.textureSubresources, computeRange.textureSubresources);
    EXPECT_EQ(computeRelease[0u].sourceQueue, queues[1u].id);
    EXPECT_EQ(computeRelease[0u].destinationQueue, queues[0u].id);

    ASSERT_EQ(compiledConsumer->prologueBarrierCount, 5u);
    const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(consumerTask).prologueBarriers;
    ASSERT_NE(barriers, nullptr);
    bool hasTransferAcquire = false;
    bool hasComputeAcquire = false;
    bool hasTransferTransition = false;
    bool hasComputeTransition = false;
    bool hasInitialTailTransition = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledConsumer->prologueBarrierCount; ++barrierIndex){
        const Graphics::GpuCompiledBarrier& barrier = barriers[barrierIndex];
        hasTransferAcquire = hasTransferAcquire || (
            barrier.type == Graphics::GpuCompiledBarrierType::TextureOwnershipAcquire
            && barrier.range.textureSubresources == transferRange.textureSubresources
            && barrier.before == Graphics::ResourceStates::CopyDest
            && barrier.sourceQueue == queues[2u].id
            && barrier.destinationQueue == queues[0u].id
        );
        hasComputeAcquire = hasComputeAcquire || (
            barrier.type == Graphics::GpuCompiledBarrierType::TextureOwnershipAcquire
            && barrier.range.textureSubresources == computeRange.textureSubresources
            && barrier.before == Graphics::ResourceStates::UnorderedAccess
            && barrier.sourceQueue == queues[1u].id
            && barrier.destinationQueue == queues[0u].id
        );
        hasTransferTransition = hasTransferTransition || (
            barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
            && barrier.range.textureSubresources == transferRange.textureSubresources
            && barrier.before == Graphics::ResourceStates::CopyDest
            && barrier.after == Graphics::ResourceStates::ShaderResource
            && !barrier.isGraphInitialState
        );
        hasComputeTransition = hasComputeTransition || (
            barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
            && barrier.range.textureSubresources == computeRange.textureSubresources
            && barrier.before == Graphics::ResourceStates::UnorderedAccess
            && barrier.after == Graphics::ResourceStates::ShaderResource
            && !barrier.isGraphInitialState
        );
        hasInitialTailTransition = hasInitialTailTransition || (
            barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
            && barrier.range.textureSubresources == initialTailRange.textureSubresources
            && barrier.before == Graphics::ResourceStates::Common
            && barrier.after == Graphics::ResourceStates::ShaderResource
            && barrier.isGraphInitialState
        );
    }
    EXPECT_TRUE(hasTransferAcquire);
    EXPECT_TRUE(hasComputeAcquire);
    EXPECT_TRUE(hasTransferTransition);
    EXPECT_TRUE(hasComputeTransition);
    EXPECT_TRUE(hasInitialTailTransition);
}

TEST(GpuTaskGraph, ClampsTypedTextureFragmentsToPhysicalSubresources){
    TestArena testArena;
    Graphics::GraphicsAllocator graphicsAllocator(testArena.arena);
    Core::CpuTaskScheduler cpuScheduler(0u);
    Graphics::GraphicsBackend::VulkanContext context(graphicsAllocator, cpuScheduler, 1u);
    Graphics::GraphicsBackend::VulkanAllocator allocator(context);
    Graphics::Texture* const textureObject = NewMetadataOnlyTexture(
        testArena.arena,
        context,
        allocator,
        Graphics::TextureDesc{}
    );
    ASSERT_NE(textureObject, nullptr);
    Graphics::TextureHandle typedTexture(
        textureObject,
        Graphics::TextureHandle::deleter_type(&testArena.arena),
        AdoptRef
    );

    // The safe CPU-only Texture fixture retains its default one-mip descriptor. A broad consumer after this
    // complete mip-0 producer must not invent a symbolic [1, All) tail with no physical subresources.
    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId texture = graph.importTexture(
        typedTexture,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/typed_fragment_texture"))
            .setMarkerLabel("Typed Fragment Texture")
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(Graphics::ResourceStates::Common)
    );
    ASSERT_TRUE(texture.valid());

    const Graphics::GpuTaskResourceUse producerUse{
        .resource = texture,
        .range = Graphics::GpuTaskResourceRange{
            .textureSubresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u),
        },
        .requiredState = Graphics::ResourceStates::CopyDest,
        .access = Graphics::GpuTaskResourceAccess::Write,
    };
    const Graphics::GpuTaskResourceUse consumerUse{
        .resource = texture,
        .range = Graphics::GpuTaskResourceRange{
            .textureSubresources = Graphics::s_AllSubresources,
        },
        .requiredState = Graphics::ResourceStates::ShaderResource,
        .access = Graphics::GpuTaskResourceAccess::Read,
    };
    const Graphics::GpuTaskId producer = AddTask(
        graph,
        Name("tests/task_graph/typed_fragment_producer"),
        "Typed Fragment Producer",
        nullptr,
        0u,
        &producerUse,
        1u
    );
    const Graphics::GpuTaskId consumer = AddTask(
        graph,
        Name("tests/task_graph/typed_fragment_consumer"),
        "Typed Fragment Consumer",
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


    const Graphics::GpuCompiledTask* const compiledConsumer = compiledPlan.findTask(consumer).plan;
    ASSERT_NE(compiledConsumer, nullptr);
    ASSERT_EQ(compiledConsumer->prologueStateSeedCount, 1u);
    const Graphics::GpuPacketStateSeed* const seeds = compiledPlan.findTask(consumer).prologueStateSeeds;
    ASSERT_NE(seeds, nullptr);
    EXPECT_EQ(
        seeds[0u].range.textureSubresources,
        Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u)
    );
    ASSERT_EQ(compiledConsumer->prologueBarrierCount, 1u);
    const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(consumer).prologueBarriers;
    ASSERT_NE(barriers, nullptr);
    EXPECT_EQ(barriers[0u].type, Graphics::GpuCompiledBarrierType::TextureTransition);
    EXPECT_EQ(barriers[0u].range.textureSubresources, Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u));
    EXPECT_EQ(barriers[0u].before, Graphics::ResourceStates::CopyDest);
    EXPECT_EQ(barriers[0u].after, Graphics::ResourceStates::ShaderResource);
    EXPECT_FALSE(barriers[0u].isInitialOwnerHandoff);

    const Graphics::GpuSubmissionPacketId consumerPacket = compiledConsumer->packet;
    ASSERT_TRUE(consumerPacket.valid());
    EXPECT_EQ(compiledPlan.packet(consumerPacket).plan->externalDependencyCount, 0u);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

