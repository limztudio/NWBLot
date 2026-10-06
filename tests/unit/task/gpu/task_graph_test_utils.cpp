// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TaskGraphTestUtils{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ExpectMemoryStatsEqual(const ArenaMemoryStats& expected, const ArenaMemoryStats& actual){
    EXPECT_EQ(actual.reservedBytes, expected.reservedBytes);
    EXPECT_EQ(actual.usedBytes, expected.usedBytes);
    EXPECT_EQ(actual.peakUsedBytes, expected.peakUsedBytes);
    EXPECT_EQ(actual.allocationCount, expected.allocationCount);
    EXPECT_EQ(actual.reallocationCount, expected.reallocationCount);
    EXPECT_EQ(actual.deallocationCount, expected.deallocationCount);
}

[[nodiscard]] ImportedTexturePair ImportTexturePair(
    TestArena& testArena,
    Graphics::GraphicsBackend::VulkanContext& context,
    Graphics::GraphicsBackend::VulkanAllocator& allocator,
    Graphics::GpuTaskGraph& graph,
    const Graphics::TextureDesc& sourceDescription,
    const Graphics::TextureDesc& destinationDescription
){
    Graphics::Texture* const sourceObject = NewMetadataOnlyTexture(
        testArena.arena,
        context,
        allocator,
        sourceDescription
    );
    if(!sourceObject)
        return {};
    Graphics::TextureHandle source(
        sourceObject,
        Graphics::TextureHandle::deleter_type(&testArena.arena),
        s_AdoptRef
    );

    Graphics::Texture* const destinationObject = NewMetadataOnlyTexture(
        testArena.arena,
        context,
        allocator,
        destinationDescription
    );
    if(!destinationObject)
        return {};
    Graphics::TextureHandle destination(
        destinationObject,
        Graphics::TextureHandle::deleter_type(&testArena.arena),
        s_AdoptRef
    );

    return {
        .source = graph.importTexture(
            source,
            Graphics::GpuGraphResourceDesc{}
                .setIdentity(Name("tests/task_graph/immutable_texture_pair_source"))
                .setMarkerLabel("Immutable Texture Pair Source")
                .setType(Graphics::GpuGraphResourceType::Texture)
                .setInitialState(Graphics::ResourceStates::Common)
        ),
        .destination = graph.importTexture(
            destination,
            Graphics::GpuGraphResourceDesc{}
                .setIdentity(Name("tests/task_graph/immutable_texture_pair_destination"))
                .setMarkerLabel("Immutable Texture Pair Destination")
                .setType(Graphics::GpuGraphResourceType::Texture)
                .setInitialState(Graphics::ResourceStates::Common)
        ),
    };
}

[[nodiscard]] Graphics::GpuGraphResourceId AddHazardDomain(
    Graphics::GpuTaskGraph& graph,
    const Name& identity,
    const AStringView label
){
    Graphics::GpuGraphResourceDesc desc;
    desc
        .setIdentity(identity)
        .setMarkerLabel(label)
        .setType(Graphics::GpuGraphResourceType::HazardDomain)
    ;
    return graph.importHazardDomain(desc);
}

[[nodiscard]] Graphics::GpuGraphResourceId AddTextureMetadata(
    Graphics::GpuTaskGraph& graph,
    const Name& identity,
    const AStringView label,
    const Graphics::ResourceStates::Mask initialState,
    const Graphics::ResourceQueueSharing::Mask queueSharing){
    Graphics::GpuGraphResourceDesc desc;
    desc
        .setIdentity(identity)
        .setMarkerLabel(label)
        .setType(Graphics::GpuGraphResourceType::Texture)
        .setInitialState(initialState)
        .setQueueSharing(queueSharing)
    ;
    return graph.importResource(desc);
}

[[nodiscard]] Graphics::GpuGraphResourceId AddPresentationTexture(
    TestArena& testArena,
    Graphics::GraphicsBackend::VulkanContext& context,
    Graphics::GraphicsBackend::VulkanAllocator& allocator,
    Graphics::GpuTaskGraph& graph,
    const Name& identity,
    const AStringView label,
    const Graphics::ResourceStates::Mask initialState,
    const Graphics::ResourceStates::Mask externalFinalState,
    const Graphics::GpuPhysicalQueueId externalFinalReleaseDestinationQueue){
    Graphics::Texture* const textureObject = NewMetadataOnlyTexture(
        testArena.arena,
        context,
        allocator,
        Graphics::TextureDesc()
            .setName(identity)
            .setInRenderTarget(true)
            .setInitialState(initialState)
    );
    if(!textureObject)
        return {};
    Graphics::TextureHandle texture(
        textureObject,
        Graphics::TextureHandle::deleter_type(&testArena.arena),
        s_AdoptRef
    );
    return graph.importTexture(
        texture,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(initialState)
            .setExternalFinalState(externalFinalState)
            .setExternalFinalReleaseDestinationQueue(externalFinalReleaseDestinationQueue)
    );
}

[[nodiscard]] Graphics::GpuGraphResourceId AddBufferMetadata(
    Graphics::GpuTaskGraph& graph,
    const Name& identity,
    const AStringView label,
    const Graphics::ResourceStates::Mask initialState,
    const Graphics::ResourceQueueSharing::Mask queueSharing){
    Graphics::GpuGraphResourceDesc desc;
    desc
        .setIdentity(identity)
        .setMarkerLabel(label)
        .setType(Graphics::GpuGraphResourceType::Buffer)
        .setInitialState(initialState)
        .setQueueSharing(queueSharing)
    ;
    return graph.importResource(desc);
}

[[nodiscard]] Graphics::GpuGraphResourceId AddAccelStructMetadata(
    Graphics::GpuTaskGraph& graph,
    const Name& identity,
    const AStringView label,
    const Graphics::ResourceStates::Mask initialState,
    const Graphics::ResourceQueueSharing::Mask queueSharing){
    Graphics::GpuGraphResourceDesc desc;
    desc
        .setIdentity(identity)
        .setMarkerLabel(label)
        .setType(Graphics::GpuGraphResourceType::AccelStruct)
        .setInitialState(initialState)
        .setQueueSharing(queueSharing)
    ;
    return graph.importResource(desc);
}

[[nodiscard]] Graphics::GpuGraphPipelineId AddPipelineMetadata(
    Graphics::GpuTaskGraph& graph,
    const Name& identity,
    const AStringView label,
    const Graphics::GpuGraphPipelineType::Enum type
){
    Graphics::GpuGraphPipelineDesc desc;
    desc
        .setIdentity(identity)
        .setMarkerLabel(label)
        .setType(type)
    ;
    return graph.importPipeline(desc);
}

[[nodiscard]] Graphics::GpuTaskId AddTask(
    Graphics::GpuTaskGraph& graph,
    const Name& identity,
    const AStringView label,
    const Graphics::GpuTaskId* const dependencies,
    const usize dependencyCount,
    const Graphics::GpuTaskResourceUse* const resourceUses,
    const usize resourceUseCount,
    const Graphics::GpuTaskResourceSetUse* const resourceSetUses,
    const usize resourceSetUseCount){
    Graphics::GpuTaskDesc desc;
    desc
        .setIdentity(identity)
        .setMarkerLabel(label)
        .setDependencies(dependencies, dependencyCount)
        .setResourceUses(resourceUses, resourceUseCount)
        .setResourceSetUses(resourceSetUses, resourceSetUseCount)
    ;
    return graph.addTask(desc);
}

[[nodiscard]] Graphics::GpuTaskId AddTaskWithCommands(
    Graphics::GpuTaskGraph& graph,
    const Name& identity,
    const AStringView label,
    const Graphics::GpuTaskCommandRequirements& commands,
    const Graphics::GpuTaskSchedulingHint& scheduling,
    const Graphics::GpuTaskTimingMetadata& timing,
    const Graphics::GpuTaskId* const dependencies,
    const usize dependencyCount){
    Graphics::GpuTaskDesc desc;
    desc
        .setIdentity(identity)
        .setMarkerLabel(label)
        .setScheduling(scheduling)
        .setTimingMetadata(timing)
        .setDependencies(dependencies, dependencyCount)
    ;
    return graph.addTask(desc, commands);
}

[[nodiscard]] bool Analyze(
    const Graphics::GpuTaskGraph& graph,
    Graphics::GpuTaskGraphAnalysis& analysis
){
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
    const Graphics::GpuTaskGraphCompiler compiler;
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
    return compiler.analyze(declarations, analysis, scratchArena);
}

[[nodiscard]] bool Assign(
    const Graphics::GpuTaskGraph& graph,
    const Graphics::GpuTaskGraphAnalysis& analysis,
    const Graphics::GpuPhysicalQueueTopology& topology,
    Graphics::GpuTaskGraphQueueAssignments& assignments,
    const Graphics::GpuTaskGraphQueueAssignmentOptions& options){
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
    const Graphics::GpuTaskGraphCompiler compiler;
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
    return compiler.assignQueues(declarations, analysis, topology, assignments, scratchArena, options);
}

[[nodiscard]] bool Compile(
    const Graphics::GpuTaskGraph& graph,
    Graphics::GpuTaskGraphAnalysis& analysis,
    const Graphics::GpuPhysicalQueueTopology& topology,
    Graphics::GpuTaskGraphQueueAssignments& assignments,
    Graphics::GpuCompiledGraph& compiledGraph,
    const Graphics::GpuTaskGraphCompileOptions& options){
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
    const Graphics::GpuTaskGraphCompiler compiler;
    Graphics::GpuTaskGraphCompileOptions metadataOptions = options;
    // Unit packetization fixtures intentionally use metadata-only task nodes to isolate compiler structure from
    // backend recording. Native compilation keeps the stricter default, covered by the payload lifecycle tests.
    metadataOptions.allowMetadataOnlyTasks = true;
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
    return compiler.compile(declarations, analysis, topology, assignments, compiledGraph, scratchArena, metadataOptions);
}

[[nodiscard]] bool CompileWithSeparatedCommandQueues(
    const Graphics::GpuTaskGraph& graph,
    Graphics::GpuTaskGraphAnalysis& analysis,
    const Graphics::GpuPhysicalQueueTopology& topology,
    Graphics::GpuTaskGraphQueueAssignments& assignments,
    Graphics::GpuCompiledGraph& compiledGraph,
    const Graphics::GpuTaskGraphCompileOptions& options){
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
    Vector<Graphics::GpuTaskDiagnosticQueueOverride, Core::Alloc::ScratchArena> overrides{ scratchArena };
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        overrides.reserve(declarations.taskCount());
        for(usize taskIndex = 0u; taskIndex < declarations.taskCount(); ++taskIndex){
            const Graphics::GpuTaskGraphTaskView task = declarations.taskAt(taskIndex);
            const Graphics::GpuQueueCapability::Mask capabilities = task.commands.requiredCapabilities;
            if(capabilities == Graphics::GpuQueueCapability::None || task.commands.requiresPrimaryGraphicsQueue || task.commands.externalQueue.valid())
                continue;
            const Graphics::CommandQueue::Enum queueClass = (capabilities & Graphics::GpuQueueCapability::Graphics)
                ? Graphics::CommandQueue::Graphics
                : ((capabilities & Graphics::GpuQueueCapability::Compute) ? Graphics::CommandQueue::Compute : Graphics::CommandQueue::Transfer)
            ;
            const Graphics::GpuPhysicalQueueInfo* selectedQueue = nullptr;
            for(usize queueIndex = 0u; topology.queues && queueIndex < topology.queueCount; ++queueIndex){
                const Graphics::GpuPhysicalQueueInfo& queue = topology.queues[queueIndex];
                if(queue.queueClass != queueClass || (queue.capabilities & capabilities) != capabilities)
                    continue;
                if(!selectedQueue || queue.id.index < selectedQueue->id.index)
                    selectedQueue = &queue;
            }
            if(selectedQueue)
                overrides.push_back({ .task = task.id, .queue = selectedQueue->id });
        }
    }
    Graphics::GpuTaskGraphCompileOptions routedOptions = options;
    routedOptions.queueAssignmentOptions.diagnosticQueueOverrides = overrides.data();
    routedOptions.queueAssignmentOptions.diagnosticQueueOverrideCount = overrides.size();
    return Compile(graph, analysis, topology, assignments, compiledGraph, routedOptions);
}

SingleQueueCompile::SingleQueueCompile(TestArena& testArena)
    : singleQueue(GraphicsQueue())
    , topology{.queues = &singleQueue, .queueCount = 1u}
    , analysis(testArena.arena)
    , assignments(testArena.arena)
    , compiledGraph(testArena.arena){
}

[[nodiscard]] bool SingleQueueCompile::compile(
    const Graphics::GpuTaskGraph& graph,
    const Graphics::GpuTaskGraphCompileOptions& options
){
    return Compile(graph, analysis, topology, assignments, compiledGraph, options);
}

TwoQueueCompile::TwoQueueCompile(TestArena& testArena)
    : graphicsQueue(GraphicsQueue())
    , computeQueue(DedicatedComputeQueue())
    , topology{.queues = queueStorage, .queueCount = 2u}
    , analysis(testArena.arena)
    , assignments(testArena.arena)
    , compiledGraph(testArena.arena){
    queueStorage[0u] = graphicsQueue;
    queueStorage[1u] = computeQueue;
}

[[nodiscard]] bool TwoQueueCompile::compile(
    const Graphics::GpuTaskGraph& graph,
    const Graphics::GpuTaskGraphCompileOptions& options
){
    return CompileWithSeparatedCommandQueues(graph, analysis, topology, assignments, compiledGraph, options);
}

ThreeQueueCompile::ThreeQueueCompile(TestArena& testArena)
    : graphicsQueue(GraphicsQueue())
    , computeQueue(DedicatedComputeQueue())
    , transferQueue(DedicatedTransferQueue())
    , topology{.queues = queueStorage, .queueCount = 3u}
    , analysis(testArena.arena)
    , assignments(testArena.arena)
    , compiledGraph(testArena.arena){
    queueStorage[0u] = graphicsQueue;
    queueStorage[1u] = computeQueue;
    queueStorage[2u] = transferQueue;
}

[[nodiscard]] bool ThreeQueueCompile::compile(
    const Graphics::GpuTaskGraph& graph,
    const Graphics::GpuTaskGraphCompileOptions& options
){
    return CompileWithSeparatedCommandQueues(graph, analysis, topology, assignments, compiledGraph, options);
}

[[nodiscard]] Graphics::GpuPhysicalQueueInfo GraphicsQueue(
    const u16 index,
    const Graphics::GpuQueueCapability::Mask capabilities){
    return Graphics::GpuPhysicalQueueInfo{
        .familyIndex = 0u,
        .queueIndex = 0u,
        .id = Graphics::GpuPhysicalQueueId{ .index = index, .deviceGeneration = 1u },
        .queueClass = Graphics::CommandQueue::Graphics,
        .capabilities = capabilities,
        .dedicated = false,
    };
}

[[nodiscard]] Graphics::GpuPhysicalQueueInfo DedicatedComputeQueue(const u16 index){
    return Graphics::GpuPhysicalQueueInfo{
        .familyIndex = 1u,
        .queueIndex = 0u,
        .id = Graphics::GpuPhysicalQueueId{ .index = index, .deviceGeneration = 1u },
        .queueClass = Graphics::CommandQueue::Compute,
        .capabilities = QueueCapabilities(
            Graphics::GpuQueueCapability::Compute,
            Graphics::GpuQueueCapability::Transfer
        ),
        .dedicated = true,
    };
}

[[nodiscard]] Graphics::GpuPhysicalQueueInfo DedicatedTransferQueue(const u16 index){
    return Graphics::GpuPhysicalQueueInfo{
        .familyIndex = 2u,
        .queueIndex = 0u,
        .id = Graphics::GpuPhysicalQueueId{ .index = index, .deviceGeneration = 1u },
        .queueClass = Graphics::CommandQueue::Transfer,
        .capabilities = Graphics::GpuQueueCapability::Transfer,
        .dedicated = true,
    };
}

[[nodiscard]] TransferOwnershipPair AddTransferOwnershipPair(
    Graphics::GpuTaskGraph& graph,
    const Graphics::ResourceQueueSharing::Mask queueSharing){
    const Graphics::GpuGraphResourceId texture = AddTextureMetadata(
        graph,
        Name("tests/task_graph/transfer_ownership_texture"),
        "Transfer Ownership Texture",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    if(!texture.valid())
        return {};

    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Graphics::GpuTaskCostHint::Medium;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    const Graphics::GpuTaskResourceUse producerUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = texture,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopySource,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse consumerUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = texture,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopySource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    Graphics::GpuTaskDesc producerDesc;
    producerDesc
        .setIdentity(Name("tests/task_graph/transfer_ownership_producer"))
        .setMarkerLabel("Transfer Ownership Producer")
        .setScheduling(scheduling)
        .setResourceUses(producerUses, LengthOf(producerUses))
    ;
    Graphics::GpuTaskDesc consumerDesc;
    consumerDesc
        .setIdentity(Name("tests/task_graph/transfer_ownership_consumer"))
        .setMarkerLabel("Transfer Ownership Consumer")
        .setScheduling(scheduling)
        .setResourceUses(consumerUses, LengthOf(consumerUses))
    ;

    return TransferOwnershipPair{
        .texture = texture,
        .producer = graph.addTask(producerDesc, Graphics::GpuTaskCommandRequirements{ Graphics::GpuQueueCapability::Graphics }),
        .consumer = graph.addTask(consumerDesc, Graphics::GpuTaskCommandRequirements{ Graphics::GpuQueueCapability::Transfer }),
    };
}

[[nodiscard]] const Graphics::GpuTaskDependencyEdge* FindEdge(
    const Graphics::GpuTaskGraphAnalysis& analysis,
    const Graphics::GpuTaskId producer,
    const Graphics::GpuTaskId consumer
){
    for(const Graphics::GpuTaskDependencyEdge& edge : analysis.edges()){
        if(edge.producer == producer && edge.consumer == consumer)
            return &edge;
    }
    return nullptr;
}

[[nodiscard]] ExternalFinalReadPair AddExternalFinalReadPair(
    Graphics::GpuTaskGraph& graph,
    const Graphics::GpuGraphResourceType::Enum resourceType,
    const Graphics::GpuTaskResourceRange& earlierRange,
    const Graphics::GpuTaskResourceRange& finalizingRange,
    const Graphics::ResourceStates::Mask readState,
    const Graphics::ResourceStates::Mask externalFinalState,
    const Graphics::ResourceQueueSharing::Mask queueSharing,
    const bool samePacket,
    const bool finalizerDependsOnEarlier,
    const Graphics::GpuPhysicalQueueId externalFinalReleaseDestinationQueue
){
    Graphics::GpuGraphResourceDesc resourceDesc;
    resourceDesc
        .setIdentity(Name("tests/task_graph/external_final_read_pair_resource"))
        .setMarkerLabel("External Final Read Pair Resource")
        .setType(resourceType)
        .setInitialState(readState)
        .setExternalFinalState(externalFinalState)
        .setExternalFinalReleaseDestinationQueue(externalFinalReleaseDestinationQueue)
        .setQueueSharing(queueSharing)
    ;
    const Graphics::GpuGraphResourceId resource = graph.importResource(resourceDesc);
    if(!resource.valid())
        return {};

    const Graphics::GpuTaskCommandRequirements graphicsCommands{ Graphics::GpuQueueCapability::Graphics };
    const Graphics::GpuTaskCommandRequirements computeCommands{ Graphics::GpuQueueCapability::Compute };
    Graphics::GpuTaskSchedulingHint earlierScheduling;
    earlierScheduling.forceSubmissionBoundary = !samePacket;
    earlierScheduling.allowPacketMerge = samePacket;
    Graphics::GpuTaskSchedulingHint finalizingScheduling = earlierScheduling;
    finalizingScheduling.mergeWithPrevious = samePacket;

    const Graphics::GpuTaskResourceUse earlierUse{
        .resource = resource,
        .range = earlierRange,
        .requiredState = readState,
        .access = Graphics::GpuTaskResourceAccess::Read,
    };
    Graphics::GpuTaskDesc earlierDesc;
    earlierDesc
        .setIdentity(Name("tests/task_graph/external_final_earlier_reader"))
        .setMarkerLabel("External Final Earlier Reader")
        .setScheduling(earlierScheduling)
        .setResourceUses(&earlierUse, 1u)
    ;
    const Graphics::GpuTaskId earlierReader = graph.addTask(earlierDesc, graphicsCommands);
    if(!earlierReader.valid())
        return {};

    const Graphics::GpuTaskResourceUse finalizingUse{
        .resource = resource,
        .range = finalizingRange,
        .requiredState = readState,
        .access = Graphics::GpuTaskResourceAccess::Read,
        .hasIndependentStateSource = !samePacket,
    };
    Graphics::GpuTaskDesc finalizingDesc;
    finalizingDesc
        .setIdentity(Name("tests/task_graph/external_final_terminal_reader"))
        .setMarkerLabel("External Final Terminal Reader")
        .setScheduling(finalizingScheduling)
        .setDependencies(finalizerDependsOnEarlier ? &earlierReader : nullptr, finalizerDependsOnEarlier ? 1u : 0u)
        .setResourceUses(&finalizingUse, 1u)
    ;
    return ExternalFinalReadPair{
        .resource = resource,
        .earlierReader = earlierReader,
        .finalizingReader = graph.addTask(finalizingDesc, samePacket ? graphicsCommands : computeCommands),
    };
}

[[nodiscard]] bool HasInferredHazard(
    const Graphics::GpuTaskGraphAnalysis& analysis,
    const Graphics::GpuTaskId producer,
    const Graphics::GpuTaskId consumer,
    const Graphics::GpuGraphResourceId resource,
    const Graphics::GpuTaskHazardType::Enum hazard
){
    for(const Graphics::GpuTaskDependencyEdge& edge : analysis.inferredEdges()){
        if(
            edge.producer == producer
            && edge.consumer == consumer
            && edge.resource == resource
            && edge.hazard == hazard
        )
            return true;
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

