// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

#include <tests/unit/task/gpu/case_types.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TaskGraphTestUtils{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<Name> IndexedName(const Name& prefix, const usize index){
    Array<char, 32> text{};
    return DeriveName(prefix, FormatDecimal(index, text));
}

[[nodiscard]] Expected<Core::GpuGraphResourceId> AddHazardDomain(Core::GpuTaskGraph& graph, const Name& identity, const AStringView label){
    Core::GpuGraphResourceDesc desc;
    desc.setIdentity(identity).setMarkerLabel(label).setType(Core::GpuGraphResourceType::HazardDomain);
    return graph.importHazardDomain(desc);
}

[[nodiscard]] Expected<Core::GpuGraphResourceId> AddTextureMetadata(Core::GpuTaskGraph& graph, const Name& identity, const AStringView label, const Core::ResourceStates::Mask initialState, const Core::ResourceQueueSharing::Mask queueSharing){
    Core::GpuGraphResourceDesc desc;
    desc.setIdentity(identity).setMarkerLabel(label).setType(Core::GpuGraphResourceType::Texture).setInitialState(initialState).setQueueSharing(queueSharing);
    return graph.importResource(desc);
}

[[nodiscard]] Expected<Core::GpuGraphResourceId> AddBufferMetadata(Core::GpuTaskGraph& graph, const Name& identity, const AStringView label, const Core::ResourceStates::Mask initialState, const Core::ResourceQueueSharing::Mask queueSharing){
    Core::GpuGraphResourceDesc desc;
    desc.setIdentity(identity).setMarkerLabel(label).setType(Core::GpuGraphResourceType::Buffer).setInitialState(initialState).setQueueSharing(queueSharing);
    return graph.importResource(desc);
}

[[nodiscard]] Expected<Core::GpuGraphResourceId> AddAccelStructMetadata(
    Core::GpuTaskGraph& graph,
    const Name& identity,
    const AStringView label,
    const Core::ResourceStates::Mask initialState,
    const Core::ResourceQueueSharing::Mask queueSharing
){
    Core::GpuGraphResourceDesc desc;
    desc.setIdentity(identity).setMarkerLabel(label).setType(Core::GpuGraphResourceType::AccelStruct).setInitialState(initialState).setQueueSharing(queueSharing);
    return graph.importResource(desc);
}

[[nodiscard]] Expected<Core::GpuGraphPipelineId> AddPipelineMetadata(Core::GpuTaskGraph& graph, const Name& identity, const AStringView label, const Core::GpuGraphPipelineType::Enum type){
    Core::GpuGraphPipelineDesc desc;
    desc.setIdentity(identity).setMarkerLabel(label).setType(type);
    return graph.importPipeline(desc);
}

[[nodiscard]] Expected<Core::GpuTaskId> AddTask(
    const Name& identity,
    const AStringView label,
    const Span<const Core::GpuTaskId> dependencies,
    const Span<const Core::GpuTaskResourceUse> resourceUses,
    const Span<const Core::GpuTaskResourceSetUse> resourceSetUses,
    const Span<const Core::GpuTaskResourceVersionUse> resourceVersionUses,
    const Span<const Core::GpuExternalCompletionId> externalDependencies,
    Core::GpuTaskGraph& graph
){
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(identity)
        .setMarkerLabel(label)
        .setDependencies(dependencies)
        .setExternalDependencies(externalDependencies)
        .setResourceUses(resourceUses)
        .setResourceSetUses(resourceSetUses)
        .setResourceVersionUses(resourceVersionUses)
    ;
    return graph.addTask(desc);
}

[[nodiscard]] Expected<Core::GpuTaskId> AddTaskWithCommands(
    const Name& identity,
    const AStringView label,
    const Core::GpuTaskCommandRequirements& commands,
    const Core::GpuTaskSchedulingHint& scheduling,
    const Core::GpuTaskTimingMetadata& timing,
    const Span<const Core::GpuTaskId> dependencies,
    Core::GpuTaskGraph& graph
){
    Core::GpuTaskDesc desc;
    desc.setIdentity(identity).setMarkerLabel(label).setScheduling(scheduling).setTiming(timing).setDependencies(dependencies);
    return graph.addTask(desc, commands);
}

[[nodiscard]] b8 Analyze(const Core::GpuTaskGraph& graph, Core::Alloc::ScratchArena& scratchArena, Core::GpuTaskGraphAnalysis& analysis){
    const Core::GpuTaskGraphCompiler compiler;
    const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
    return compiler.analyze(declarations, scratchArena, analysis);
}

[[nodiscard]] b8 Assign(
    Core::GpuTaskGraphQueueAssignments& assignments,
    Core::Alloc::ScratchArena& scratchArena,
    const Core::GpuTaskGraph& graph,
    const Core::GpuTaskGraphAnalysis& analysis,
    const Core::GpuPhysicalQueueTopology& topology,
    const Core::GpuTaskGraphQueueAssignmentOptions& options
){
    const Core::GpuTaskGraphCompiler compiler;
    const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
    return compiler.assignQueues(declarations, analysis, topology, options, scratchArena, assignments);
}

[[nodiscard]] b8 Compile(
    Core::GpuTaskGraphAnalysis& analysis,
    Core::GpuTaskGraphQueueAssignments& assignments,
    Core::GpuCompiledGraph& compiledGraph,
    Core::Alloc::ScratchArena& scratchArena,
    const Core::GpuTaskGraph& graph,
    const Core::GpuPhysicalQueueTopology& topology,
    const Core::GpuTaskGraphCompileOptions& options
){
    const Core::GpuTaskGraphCompiler compiler;
    Core::GpuTaskGraphCompileOptions metadataOptions = options;
    // Allow metadata-only tasks in unit packetization helpers.
    metadataOptions.allowMetadataOnlyTasks = true;
    const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
    return compiler.compile(declarations, topology, metadataOptions, scratchArena, analysis, assignments, compiledGraph);
}

[[nodiscard]] b8 CompileWithSeparatedCommandQueues(
    Core::GpuTaskGraphAnalysis& analysis,
    Core::GpuTaskGraphQueueAssignments& assignments,
    Core::GpuCompiledGraph& compiledGraph,
    Core::Alloc::ScratchArena& scratchArena,
    const Core::GpuTaskGraph& graph,
    const Core::GpuPhysicalQueueTopology& topology,
    const Core::GpuTaskGraphCompileOptions& options
){
    if(!topology.queues)
        return Compile(analysis, assignments, compiledGraph, scratchArena, graph, topology, options);

    b8 hasComputeQueue = false;
    b8 hasTransferQueue = false;
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        hasComputeQueue = hasComputeQueue || topology.queues[queueIndex].queueClass == Core::CommandQueue::Compute;
        hasTransferQueue = hasTransferQueue || topology.queues[queueIndex].queueClass == Core::CommandQueue::Transfer;
    }
    Vector<Core::GpuPhysicalQueueInfo, Core::Alloc::ScratchArena> queues(topology.queues, topology.queues + topology.queueCount, scratchArena);
    for(Core::GpuPhysicalQueueInfo& queue : queues){
        u8 capabilities = static_cast<u8>(queue.capabilities);
        if(hasComputeQueue && queue.queueClass == Core::CommandQueue::Graphics)
            capabilities &= static_cast<u8>(~static_cast<u8>(Core::GpuQueueCapability::Compute));
        if(hasTransferQueue && queue.queueClass != Core::CommandQueue::Transfer)
            capabilities &= static_cast<u8>(~static_cast<u8>(Core::GpuQueueCapability::Transfer));
        queue.capabilities = static_cast<Core::GpuQueueCapability::Mask>(capabilities);
    }
    const Core::GpuPhysicalQueueTopology separatedTopology{ .queues = queues.data(), .queueCount = queues.size(), };
    return Compile(analysis, assignments, compiledGraph, scratchArena, graph, separatedTopology, options);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SingleQueueCompile::SingleQueueCompile(TestArena& testArena)
    : scratchArena(s_TaskGraphScratchArena)
    , singleQueue(GraphicsQueue())
    , topology{ .queues = &singleQueue, .queueCount = 1u }
    , analysis(testArena.arena)
    , assignments(testArena.arena)
    , compiledGraph(testArena.arena)
{}

[[nodiscard]] b8 SingleQueueCompile::compile(const Core::GpuTaskGraph& graph, const Core::GpuTaskGraphCompileOptions& options){
    return Compile(analysis, assignments, compiledGraph, scratchArena, graph, topology, options);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TwoQueueCompile::TwoQueueCompile(TestArena& testArena)
    : scratchArena(s_TaskGraphScratchArena)
    , graphicsQueue(GraphicsQueue())
    , computeQueue(DedicatedComputeQueue())
    , queueStorage{ graphicsQueue, computeQueue }
    , topology{ .queues = queueStorage.data(), .queueCount = queueStorage.size() }
    , analysis(testArena.arena)
    , assignments(testArena.arena)
    , compiledGraph(testArena.arena)
{}

[[nodiscard]] b8 TwoQueueCompile::compile(const Core::GpuTaskGraph& graph, const Core::GpuTaskGraphCompileOptions& options){
    return CompileWithSeparatedCommandQueues(analysis, assignments, compiledGraph, scratchArena, graph, topology, options);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ThreeQueueCompile::ThreeQueueCompile(TestArena& testArena)
    : scratchArena(s_TaskGraphScratchArena)
    , graphicsQueue(GraphicsQueue())
    , computeQueue(DedicatedComputeQueue())
    , transferQueue(DedicatedTransferQueue())
    , queueStorage{ graphicsQueue, computeQueue, transferQueue }
    , topology{ .queues = queueStorage.data(), .queueCount = queueStorage.size() }
    , analysis(testArena.arena)
    , assignments(testArena.arena)
    , compiledGraph(testArena.arena)
{}

[[nodiscard]] b8 ThreeQueueCompile::compile(const Core::GpuTaskGraph& graph, const Core::GpuTaskGraphCompileOptions& options){
    return CompileWithSeparatedCommandQueues(analysis, assignments, compiledGraph, scratchArena, graph, topology, options);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Core::GpuPhysicalQueueInfo GraphicsQueue(const u16 index, const Core::GpuQueueCapability::Mask capabilities)noexcept{
    return Core::GpuPhysicalQueueInfo{ .familyIndex = 0u, .queueIndex = 0u, .id = Core::GpuPhysicalQueueId{ .index = index, .deviceGeneration = 1u }, .queueClass = Core::CommandQueue::Graphics, .capabilities = capabilities, .dedicated = false, };
}

[[nodiscard]] Core::GpuPhysicalQueueInfo DedicatedComputeQueue(const u16 index)noexcept{
    return Core::GpuPhysicalQueueInfo{
        .familyIndex = 1u,
        .queueIndex = 0u,
        .id = Core::GpuPhysicalQueueId{ .index = index, .deviceGeneration = 1u },
        .queueClass = Core::CommandQueue::Compute,
        .capabilities = QueueCapabilities(Core::GpuQueueCapability::Compute, Core::GpuQueueCapability::Transfer),
        .dedicated = true,
    };
}

[[nodiscard]] Core::GpuPhysicalQueueInfo DedicatedTransferQueue(const u16 index)noexcept{
    return Core::GpuPhysicalQueueInfo{
        .familyIndex = 2u,
        .queueIndex = 0u,
        .id = Core::GpuPhysicalQueueId{ .index = index, .deviceGeneration = 1u },
        .queueClass = Core::CommandQueue::Transfer,
        .capabilities = Core::GpuQueueCapability::Transfer,
        .dedicated = true,
    };
}

[[nodiscard]] Expected<TransferOwnershipPair> AddTransferOwnershipPair(Core::GpuTaskGraph& graph, const Core::ResourceQueueSharing::Mask queueSharing){
    const auto textureResult = AddTextureMetadata(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphTransferOwnershipTexture), "Transfer Ownership Texture", Core::ResourceStates::Common, queueSharing);
    if(!textureResult)
        return MakeUnexpected(Failure{});

    const Core::GpuGraphResourceId texture = *textureResult;

    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Medium;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    const Array<Core::GpuTaskResourceUse, 1> producerUses{ Core::GpuTaskResourceUse{ .resource = texture, .range = {}, .requiredState = Core::ResourceStates::CopySource, .access = Core::GpuTaskResourceAccess::Write, } };
    const Array<Core::GpuTaskResourceUse, 1> consumerUses{ Core::GpuTaskResourceUse{ .resource = texture, .range = {}, .requiredState = Core::ResourceStates::CopySource, .access = Core::GpuTaskResourceAccess::Read, } };
    Core::GpuTaskDesc producerDesc;
    producerDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphTransferOwnershipProducer)).setMarkerLabel("Transfer Ownership Producer").setScheduling(scheduling).setResourceUses(producerUses);
    Core::GpuTaskDesc consumerDesc;
    consumerDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphTransferOwnershipConsumer)).setMarkerLabel("Transfer Ownership Consumer").setScheduling(scheduling).setResourceUses(consumerUses);

    const auto producer = graph.addTask(producerDesc, Core::GpuTaskCommandRequirements{ Core::GpuQueueCapability::Graphics });
    const auto consumer = graph.addTask(consumerDesc, Core::GpuTaskCommandRequirements{ Core::GpuQueueCapability::Transfer });
    if(!producer || !consumer)
        return MakeUnexpected(Failure{});
    return TransferOwnershipPair{ .texture = texture, .producer = *producer, .consumer = *consumer, };
}

[[nodiscard]] const Core::GpuTaskDependencyEdge* FindEdge(const Core::GpuTaskGraphAnalysis& analysis, const Core::GpuTaskId producer, const Core::GpuTaskId consumer)noexcept{
    for(const Core::GpuTaskDependencyEdge& edge : analysis.edges()){
        if(edge.producer == producer && edge.consumer == consumer)
            return &edge;
    }
    return nullptr;
}

[[nodiscard]] Expected<ExternalFinalReadPair> AddExternalFinalReadPair(
    Core::GpuTaskGraph& graph,
    const Core::GpuGraphResourceType::Enum resourceType,
    const Core::GpuTaskResourceRange& earlierRange,
    const Core::GpuTaskResourceRange& finalizingRange,
    const Core::ResourceStates::Mask readState,
    const Core::ResourceStates::Mask externalFinalState,
    const Core::ResourceQueueSharing::Mask queueSharing,
    const b8 samePacket,
    const b8 finalizerDependsOnEarlier,
    const Core::GpuPhysicalQueueId externalFinalReleaseDestinationQueue
){
    Core::GpuGraphResourceDesc resourceDesc;
    resourceDesc
        .setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphExternalFinalReadPairResource))
        .setMarkerLabel("External Final Read Pair Resource")
        .setType(resourceType)
        .setInitialState(readState)
        .setExternalFinalState(externalFinalState)
        .setExternalFinalReleaseDestinationQueue(externalFinalReleaseDestinationQueue)
        .setQueueSharing(queueSharing)
    ;
    const auto resourceResult = graph.importResource(resourceDesc);
    if(!resourceResult)
        return MakeUnexpected(Failure{});

    const Core::GpuGraphResourceId resource = *resourceResult;

    const Core::GpuTaskCommandRequirements graphicsCommands{ Core::GpuQueueCapability::Graphics };
    const Core::GpuTaskCommandRequirements computeCommands{ Core::GpuQueueCapability::Compute };
    Core::GpuTaskSchedulingHint earlierScheduling;
    earlierScheduling.forceSubmissionBoundary = !samePacket;
    earlierScheduling.allowPacketMerge = samePacket;
    Core::GpuTaskSchedulingHint finalizingScheduling = earlierScheduling;
    finalizingScheduling.mergeWithPrevious = samePacket;

    const Core::GpuTaskResourceUse earlierUse{ .resource = resource, .range = earlierRange, .requiredState = readState, .access = Core::GpuTaskResourceAccess::Read, };
    Core::GpuTaskDesc earlierDesc;
    earlierDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphExternalFinalEarlierReader)).setMarkerLabel("External Final Earlier Reader").setScheduling(earlierScheduling).setResourceUses(Span(&earlierUse, 1u));
    const auto earlierReaderResult = graph.addTask(earlierDesc, graphicsCommands);
    if(!earlierReaderResult)
        return MakeUnexpected(Failure{});

    const Core::GpuTaskId earlierReader = *earlierReaderResult;

    const Core::GpuTaskResourceUse finalizingUse{ .resource = resource, .range = finalizingRange, .requiredState = readState, .access = Core::GpuTaskResourceAccess::Read, .hasIndependentStateSource = !samePacket, };
    Core::GpuTaskDesc finalizingDesc;
    finalizingDesc
        .setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphExternalFinalTerminalReader))
        .setMarkerLabel("External Final Terminal Reader")
        .setScheduling(finalizingScheduling)
        .setDependencies(Span(finalizerDependsOnEarlier ? &earlierReader : nullptr, finalizerDependsOnEarlier ? 1u : 0u))
        .setResourceUses(Span(&finalizingUse, 1u))
    ;
    const auto finalizingReader = graph.addTask(finalizingDesc, samePacket ? graphicsCommands : computeCommands);
    if(!finalizingReader)
        return MakeUnexpected(Failure{});
    return ExternalFinalReadPair{ .resource = resource, .earlierReader = earlierReader, .finalizingReader = *finalizingReader, };
}

[[nodiscard]] b8 HasInferredHazard(const Core::GpuTaskGraphAnalysis& analysis, const Core::GpuTaskId producer, const Core::GpuTaskId consumer, const Core::GpuGraphResourceId resource, const Core::GpuTaskHazardType::Enum hazard)noexcept{
    for(const Core::GpuTaskDependencyEdge& edge : analysis.inferredEdges()){
        if(edge.producer == producer && edge.consumer == consumer && edge.resource == resource && edge.hazard == hazard)
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

