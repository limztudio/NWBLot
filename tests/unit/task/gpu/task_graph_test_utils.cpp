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


static_assert(requires(const Graphics::GpuCompiledGraph::ReadView& compiledPlan){
    { compiledPlan.logicalOwnershipTransferCount() }->SameAs<usize>;
    { compiledPlan.logicalOwnershipTransfers() }->SameAs<const Graphics::GpuCompiledOwnershipTransfer*>;
    { compiledPlan.logicalOwnershipTransferAt(0u) }->SameAs<const Graphics::GpuCompiledOwnershipTransfer*>;
});

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
        AdoptRef
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
        AdoptRef
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
        AdoptRef
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

[[nodiscard]] Graphics::GpuTaskId AddTaskWithQueue(
    Graphics::GpuTaskGraph& graph,
    const Name& identity,
    const AStringView label,
    const Graphics::GpuQueueRequest& queue,
    const Graphics::GpuTaskSchedulingHint& scheduling,
    const Graphics::GpuTaskTimingMetadata& timing,
    const Graphics::GpuTaskId* const dependencies,
    const usize dependencyCount){
    Graphics::GpuTaskDesc desc;
    desc
        .setIdentity(identity)
        .setMarkerLabel(label)
        .setQueue(queue)
        .setScheduling(scheduling)
        .setTimingMetadata(timing)
        .setDependencies(dependencies, dependencyCount)
    ;
    return graph.addTask(desc);
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
    const Graphics::GpuTaskGraphQueueTopology& topology,
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
    const Graphics::GpuTaskGraphQueueTopology& topology,
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

[[nodiscard]] Graphics::GpuPhysicalQueueInfo GraphicsQueue(
    const u16 index,
    const Graphics::GpuQueueCapability::Mask capabilities){
    return Graphics::GpuPhysicalQueueInfo{
        .id = Graphics::GpuPhysicalQueueId{ index, 1u },
        .queueClass = Graphics::CommandQueue::Graphics,
        .capabilities = capabilities,
        .familyIndex = 0u,
        .queueIndex = 0u,
        .dedicated = false,
    };
}

[[nodiscard]] Graphics::GpuPhysicalQueueInfo DedicatedComputeQueue(const u16 index){
    return Graphics::GpuPhysicalQueueInfo{
        .id = Graphics::GpuPhysicalQueueId{ index, 1u },
        .queueClass = Graphics::CommandQueue::Compute,
        .capabilities = QueueCapabilities(
            Graphics::GpuQueueCapability::Compute,
            Graphics::GpuQueueCapability::Transfer
        ),
        .familyIndex = 1u,
        .queueIndex = 0u,
        .dedicated = true,
    };
}

[[nodiscard]] Graphics::GpuPhysicalQueueInfo DedicatedTransferQueue(const u16 index){
    return Graphics::GpuPhysicalQueueInfo{
        .id = Graphics::GpuPhysicalQueueId{ index, 1u },
        .queueClass = Graphics::CommandQueue::Transfer,
        .capabilities = Graphics::GpuQueueCapability::Transfer,
        .familyIndex = 2u,
        .queueIndex = 0u,
        .dedicated = true,
    };
}

[[nodiscard]] Telemetry::FrameGraphQueueAssignmentModifier::Mask ExpectedTelemetryModifiers(
    const Graphics::GpuTaskQueueAssignmentModifier::Mask modifiers
){
    u8 expected = Telemetry::FrameGraphQueueAssignmentModifier::None;
    if(modifiers & Graphics::GpuTaskQueueAssignmentModifier::DirectDependencyAffinity)
        expected |= Telemetry::FrameGraphQueueAssignmentModifier::DirectDependencyAffinity;
    if(modifiers & Graphics::GpuTaskQueueAssignmentModifier::SameClassLoadBalance)
        expected |= Telemetry::FrameGraphQueueAssignmentModifier::SameClassLoadBalance;
    if(modifiers & Graphics::GpuTaskQueueAssignmentModifier::NonPrimaryPreference)
        expected |= Telemetry::FrameGraphQueueAssignmentModifier::NonPrimaryPreference;
    if(modifiers & Graphics::GpuTaskQueueAssignmentModifier::DebugTimingOverride)
        expected |= Telemetry::FrameGraphQueueAssignmentModifier::DebugTimingOverride;
    if(modifiers & Graphics::GpuTaskQueueAssignmentModifier::TimingCalibration)
        expected |= Telemetry::FrameGraphQueueAssignmentModifier::TimingCalibration;
    if(modifiers & Graphics::GpuTaskQueueAssignmentModifier::TimingFeedback)
        expected |= Telemetry::FrameGraphQueueAssignmentModifier::TimingFeedback;
    return static_cast<Telemetry::FrameGraphQueueAssignmentModifier::Mask>(expected);
}

void ExpectPlannedQueueAssignmentTelemetry(
    const Graphics::GpuTaskQueueAssignment& source,
    const Telemetry::FrameGraphQueueAssignment& telemetry,
    const Telemetry::FrameGraphQueueClass::Enum queueClass,
    const Telemetry::FrameGraphQueueAssignmentReason::Enum reason
){
    EXPECT_TRUE(telemetry.present);
    EXPECT_EQ(telemetry.initialQueue.index, source.initialQueue.index);
    EXPECT_EQ(telemetry.initialQueue.deviceGeneration, source.initialQueue.deviceGeneration);
    EXPECT_EQ(telemetry.plannedQueue.index, source.queue.index);
    EXPECT_EQ(telemetry.plannedQueue.deviceGeneration, source.queue.deviceGeneration);
    EXPECT_FALSE(telemetry.acceptedQueue.valid());
    EXPECT_FALSE(telemetry.previousAcceptedQueue.valid());
    EXPECT_EQ(telemetry.score.preference, source.score.preference);
    EXPECT_EQ(telemetry.score.overlap, source.score.overlap);
    EXPECT_EQ(telemetry.score.queueLoad, source.score.queueLoad);
    EXPECT_EQ(telemetry.score.incomingCrossings, source.score.incomingCrossings);
    EXPECT_EQ(telemetry.score.outgoingCrossings, source.score.outgoingCrossings);
    EXPECT_EQ(telemetry.score.ownershipTransfers, source.score.ownershipTransfers);
    EXPECT_EQ(telemetry.score.total, source.score.total());
    EXPECT_EQ(telemetry.queueClass, queueClass);
    EXPECT_EQ(telemetry.reason, reason);
    EXPECT_EQ(telemetry.modifiers, ExpectedTelemetryModifiers(source.modifiers));
    EXPECT_EQ(telemetry.acceptance, Telemetry::FrameGraphQueueAssignmentAcceptance::NotAccepted);
    EXPECT_EQ(telemetry.dedicated, source.dedicated);
}

[[nodiscard]] TransferOwnershipPair AddTransferOwnershipPair(
    Graphics::GpuTaskGraph& graph,
    const Graphics::ResourceQueueSharing::Mask queueSharing,
    const bool allowFallback){
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
        .setQueue(Graphics::GpuQueueRequest{
            Graphics::GpuQueueCapability::Graphics,
            Graphics::GpuQueuePreference::Graphics,
            false,
            false,
        })
        .setScheduling(scheduling)
        .setResourceUses(producerUses, LengthOf(producerUses))
    ;
    Graphics::GpuTaskDesc consumerDesc;
    consumerDesc
        .setIdentity(Name("tests/task_graph/transfer_ownership_consumer"))
        .setMarkerLabel("Transfer Ownership Consumer")
        .setQueue(Graphics::GpuQueueRequest{
            Graphics::GpuQueueCapability::Transfer,
            Graphics::GpuQueuePreference::Transfer,
            allowFallback,
            allowFallback,
        })
        .setScheduling(scheduling)
        .setResourceUses(consumerUses, LengthOf(consumerUses))
    ;

    return TransferOwnershipPair{
        .texture = texture,
        .producer = graph.addTask(producerDesc),
        .consumer = graph.addTask(consumerDesc),
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
        .setQueue(graphicsRequest)
        .setScheduling(earlierScheduling)
        .setResourceUses(&earlierUse, 1u)
    ;
    const Graphics::GpuTaskId earlierReader = graph.addTask(earlierDesc);
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
        .setQueue(samePacket ? graphicsRequest : computeRequest)
        .setScheduling(finalizingScheduling)
        .setDependencies(finalizerDependsOnEarlier ? &earlierReader : nullptr, finalizerDependsOnEarlier ? 1u : 0u)
        .setResourceUses(&finalizingUse, 1u)
    ;
    return ExternalFinalReadPair{
        .resource = resource,
        .earlierReader = earlierReader,
        .finalizingReader = graph.addTask(finalizingDesc),
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

