// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler_internal.h"

#include <global/simplemath.h>
#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuTaskGraphCompiler::compile(
    const GpuTaskGraph& graph,
    GpuTaskGraphAnalysis& outAnalysis,
    const GpuTaskGraphQueueTopology& topology,
    GpuTaskGraphQueueAssignments& outAssignments,
    GpuCompiledGraph& outCompiledGraph,
    Alloc::ScratchArena& scratchArena,
    const GpuTaskGraphCompileOptions& options
)const{
    using namespace GpuTaskGraphCompilerDetail;

    outCompiledGraph.reset();
    const Timer compileBegin = TimerNow();
    const Timer analysisBegin = compileBegin;
    if(!analyze(graph, outAnalysis, scratchArena))
        return false;
    const f64 analysisSeconds = DurationInSeconds<f64>(TimerNow(), analysisBegin);

    if(!options.allowMetadataOnlyTasks){
        for(usize taskIndex = 0u; taskIndex < graph.taskCount(); ++taskIndex){
            const GpuTaskGraphTaskView task = graph.taskAt(taskIndex);
            if(!task.hasPayload || !task.hasRecordPayload){
                outAssignments.reset();
                outAnalysis.m_diagnostic.status = GpuTaskGraphAnalysisStatus::MissingTaskRecordPayload;
                outAnalysis.m_diagnostic.task = task.id;
                outAnalysis.m_diagnostic.relatedTask = {};
                outAnalysis.m_diagnostic.resource = {};
                outAnalysis.m_valid = false;
                return false;
            }

            for(usize useIndex = 0u; useIndex < task.resourceUseCount; ++useIndex){
                const GpuTaskResourceUse& use = task.resourceUses[useIndex];
                const GpuTaskGraphResourceView resource = graph.resourceAt(use.resource.index);
                switch(resource.type){
                case GpuGraphResourceType::Texture:
                    if(graph.textureForResource(use.resource))
                        continue;
                    break;
                case GpuGraphResourceType::Buffer:
                    if(graph.bufferForResource(use.resource))
                        continue;
                    break;
                case GpuGraphResourceType::AccelStruct:
                    if(graph.accelStructForResource(use.resource))
                        continue;
                    break;
                case GpuGraphResourceType::HazardDomain:
                    continue;
                default:
                    break;
                }

                outAssignments.reset();
                outAnalysis.m_diagnostic.status = GpuTaskGraphAnalysisStatus::InvalidResourceUse;
                outAnalysis.m_diagnostic.task = task.id;
                outAnalysis.m_diagnostic.relatedTask = {};
                outAnalysis.m_diagnostic.resource = use.resource;
                outAnalysis.m_valid = false;
                return false;
            }
        }
    }

    const Timer queueAssignmentBegin = TimerNow();
    if(!assignQueues(graph, outAnalysis, topology, outAssignments, options.queueAssignmentOptions))
        return false;
    const f64 queueAssignmentSeconds = DurationInSeconds<f64>(TimerNow(), queueAssignmentBegin);
    const Timer planningBegin = TimerNow();

    if(
        topology.queueCount == 0u
        || !topology.queues
        || options.packetizationPolicy >= GpuTaskGraphPacketizationPolicy::kCount
        || !graph.validForDeviceGeneration(topology.queues[0].id.deviceGeneration)
    )
        return false;

    for(usize completionIndex = 0u; completionIndex < graph.externalCompletionCount(); ++completionIndex){
        const GpuTaskGraphExternalCompletionView completion = graph.externalCompletionAt(completionIndex);
        if(!completion.hasToken)
            continue;

        const GpuPhysicalQueueId producerQueue{
            completion.token.physicalQueueIndex,
            completion.token.deviceGeneration,
        };
        bool validProducerQueue = false;
        for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
            const GpuPhysicalQueueInfo& queue = topology.queues[queueIndex];
            if(queue.id != producerQueue || queue.queueClass != completion.token.queue)
                continue;
            validProducerQueue = true;
            break;
        }
        if(!validProducerQueue)
            return false;
    }

    outCompiledGraph.m_generation = graph.generation();
    outCompiledGraph.m_declarationRevision = graph.declarationRevision();
    outCompiledGraph.m_planGeneration = AllocateCompiledPlanGeneration();
    outCompiledGraph.m_deviceGeneration = topology.queues[0].id.deviceGeneration;
    outCompiledGraph.m_graphTaskCount = graph.taskCount();
    outCompiledGraph.m_tasks.reserve(graph.taskCount());
    outCompiledGraph.m_packets.reserve(graph.taskCount());
    outCompiledGraph.m_packetTasks.reserve(graph.taskCount());
    outCompiledGraph.m_prologueStateSeeds.reserve(graph.taskCount());
    outCompiledGraph.m_prologueBarriers.reserve(graph.taskCount());
    outCompiledGraph.m_epilogueBarriers.reserve(graph.taskCount());
    outCompiledGraph.m_externalResourceExports.reserve(graph.resourceCount());
    outCompiledGraph.m_externalResourceExportSources.reserve(graph.resourceCount());
    outCompiledGraph.m_queueTopology.reserve(topology.queueCount);
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex)
        outCompiledGraph.m_queueTopology.push_back(topology.queues[queueIndex]);

    GpuTaskGraphCompiledPlanStorage compiledPlan{
        .tasks = outCompiledGraph.m_tasks,
        .packets = outCompiledGraph.m_packets,
        .packetTasks = outCompiledGraph.m_packetTasks,
        .packetDependencies = outCompiledGraph.m_packetDependencies,
        .packetExternalDependencies = outCompiledGraph.m_packetExternalDependencies,
        .prologueStateSeeds = outCompiledGraph.m_prologueStateSeeds,
        .prologueBarriers = outCompiledGraph.m_prologueBarriers,
        .epilogueBarriers = outCompiledGraph.m_epilogueBarriers,
        .externalResourceExports = outCompiledGraph.m_externalResourceExports,
        .externalResourceExportSources = outCompiledGraph.m_externalResourceExportSources,
        .queueTopology = outCompiledGraph.m_queueTopology,
        .presentEndpoint = outCompiledGraph.m_presentEndpoint,
        .hasPresentEndpoint = outCompiledGraph.m_hasPresentEndpoint,
        .graphGeneration = outCompiledGraph.m_generation,
        .deviceGeneration = outCompiledGraph.m_deviceGeneration,
        .planGeneration = outCompiledGraph.m_planGeneration,
    };

    // An import may name the exact physical queue that owned an exclusive texture/buffer/acceleration structure
    // before graph work began.
    // A different first packet must also name a fixed release destination, an imported completion, and a native
    // state handoff source. That keeps the compiler from manufacturing an acquire or cross-queue wait on its own.
    for(usize resourceIndex = 0u; resourceIndex < graph.resourceCount(); ++resourceIndex){
        const GpuTaskGraphResourceView resource = graph.resourceAt(resourceIndex);
        const bool hasExternalFinalRelease = resource.externalFinalReleaseDestinationQueue.valid();
        if(
            hasExternalFinalRelease
            && (
                resource.externalFinalState == ResourceStates::Unknown
                || ResourceUsesConcurrentQueueSharing(resource.queueSharing, topology)
                || !FindCompiledQueueInfo(compiledPlan, resource.externalFinalReleaseDestinationQueue)
            )
        ){
            outCompiledGraph.reset();
            return false;
        }
        if(resource.initialOwnerHandoffSourceCount != 0u){
            const Texture* const typedTexture = graph.textureForResource(resource.id);
            if(
                resource.type != GpuGraphResourceType::Texture
                || !resource.initialOwnerHandoffSources
                || ResourceUsesConcurrentQueueSharing(resource.queueSharing, topology)
            ){
                outCompiledGraph.reset();
                return false;
            }
            for(usize sourceIndex = 0u;
                sourceIndex < resource.initialOwnerHandoffSourceCount;
                ++sourceIndex
            ){
                const GpuTaskGraphInitialOwnerHandoffSourceView& source =
                    resource.initialOwnerHandoffSources[sourceIndex]
                ;
                GpuTaskResourceRange plannedSourceRange;
                const GpuPhysicalQueueInfo* const sourceQueueInfo = FindCompiledQueueInfo(
                    compiledPlan,
                    source.sourceQueue
                );
                if(
                    !ResolveTextureRangeForPlanning(typedTexture, source.range, plannedSourceRange)
                    || !source.sourceQueue.valid()
                    || !source.destinationQueue.valid()
                    || !sourceQueueInfo
                    || !FindCompiledQueueInfo(compiledPlan, source.destinationQueue)
                    || !graph.validExternalCompletion(source.completion)
                    || !source.minimumCompletionToken.valid()
                    || source.minimumCompletionToken.queue != sourceQueueInfo->queueClass
                    || !source.minimumCompletionToken.matchesPhysicalQueue(
                        source.sourceQueue.index,
                        source.sourceQueue.deviceGeneration
                    )
                    || !source.stateSource
                ){
                    outCompiledGraph.reset();
                    return false;
                }
            }
            continue;
        }
        if(!resource.initialOwnerQueue.valid())
            continue;
        const bool hasInitialOwnerHandoff = resource.initialOwnerReleaseDestinationQueue.valid();
        const GpuPhysicalQueueInfo* const initialOwnerQueueInfo = FindCompiledQueueInfo(
            compiledPlan,
            resource.initialOwnerQueue
        );
        if(
            ResourceUsesConcurrentQueueSharing(resource.queueSharing, topology)
            || !initialOwnerQueueInfo
            || (
                hasInitialOwnerHandoff
                && (
                    !FindCompiledQueueInfo(compiledPlan, resource.initialOwnerReleaseDestinationQueue)
                    || !graph.validExternalCompletion(resource.initialOwnerCompletion)
                    || !resource.initialOwnerMinimumCompletionToken.valid()
                    || resource.initialOwnerMinimumCompletionToken.queue != initialOwnerQueueInfo->queueClass
                    || !resource.initialOwnerMinimumCompletionToken.matchesPhysicalQueue(
                        resource.initialOwnerQueue.index,
                        resource.initialOwnerQueue.deviceGeneration
                    )
                    || !resource.initialOwnerStateSource
                )
            )
        ){
            outCompiledGraph.reset();
            return false;
        }
    }

    const Timer packetizationBegin = TimerNow();
    if(!BuildSubmissionPackets(
        graph,
        outAnalysis,
        outAssignments,
        options.packetizationPolicy,
        compiledPlan
    )){
        outCompiledGraph.reset();
        return false;
    }
    const f64 packetizationSeconds = DurationInSeconds<f64>(TimerNow(), packetizationBegin);

    const Timer resourceStatePlanningBegin = TimerNow();
    // Start with graph-planned packet state seeds, transitions, UAV dependencies, and exclusive-family ownership
    // releases. A state seed selects the actual final-state snapshot of a graph-internal producer, so it also carries
    // the release destination into CommandList::open where the paired Vulkan acquire is emitted before the consumer.
    Vector<TrackedCompiledResourceState, Alloc::ScratchArena> trackedResourceStates(scratchArena);
    Vector<PendingCompiledEpilogueBarrier, Alloc::ScratchArena> pendingEpilogueBarriers(scratchArena);
    Vector<GpuTaskExternalDependencyEdge, Alloc::ScratchArena> initialOwnershipDependencies(scratchArena);
    Vector<TrackedTextureStateFragment, Alloc::ScratchArena> stateFragments(scratchArena);
    Vector<GpuTaskResourceRange, Alloc::ScratchArena> taskFirstUseRanges(scratchArena);
    trackedResourceStates.reserve(graph.taskCount());
    pendingEpilogueBarriers.reserve(graph.taskCount());
    initialOwnershipDependencies.reserve(graph.taskCount());
    stateFragments.reserve(graph.taskCount());
    taskFirstUseRanges.reserve(graph.taskCount());
    GpuTaskGraphResourceStatePlan resourceStatePlan{
        .graph = graph,
        .topology = topology,
        .topologicalOrder = outAnalysis.topologicalOrder(),
        .compiledPlan = compiledPlan,
        .scratchArena = scratchArena,
        .trackedResourceStates = trackedResourceStates,
        .pendingEpilogueBarriers = pendingEpilogueBarriers,
        .initialOwnershipDependencies = initialOwnershipDependencies,
        .stateFragments = stateFragments,
        .taskFirstUseRanges = taskFirstUseRanges,
    };
    if(
        !PlanTaskResourceStates(resourceStatePlan)
        || !PlanExternalResourceExports(resourceStatePlan)
    ){
        outCompiledGraph.reset();
        return false;
    }
    AppendPendingEpilogueBarriers(resourceStatePlan);
    const f64 resourceStatePlanningSeconds = DurationInSeconds<f64>(TimerNow(), resourceStatePlanningBegin);

    const Timer packetDependencyPlanningBegin = TimerNow();
    if(!PlanPacketDependencies(
        graph,
        outAnalysis,
        initialOwnershipDependencies,
        compiledPlan
    )){
        outCompiledGraph.reset();
        return false;
    }
    const f64 packetDependencyPlanningSeconds = DurationInSeconds<f64>(TimerNow(), packetDependencyPlanningBegin);

    GpuTaskGraphCompileStatistics& statistics = outCompiledGraph.m_compileStatistics;
    statistics.graphGeneration = outCompiledGraph.m_generation;
    statistics.planGeneration = outCompiledGraph.m_planGeneration;
    statistics.deviceGeneration = outCompiledGraph.m_deviceGeneration;
    statistics.taskCount = graph.taskCount();
    statistics.resourceCount = graph.resourceCount();
    statistics.resourceSetCount = graph.m_resourceSets.size();
    statistics.resourceSetMemberCount = graph.m_resourceSetMembers.size();
    statistics.uploadBlobCount = graph.m_uploadBlobs.size();
    for(const auto& blob : graph.m_uploadBlobs)
        statistics.uploadBlobBytes += blob.bytes.size();
    statistics.explicitDependencyCount = outAnalysis.explicitEdgeCount();
    statistics.inferredDependencyCount = outAnalysis.inferredEdgeCount();
    statistics.declaredExternalDependencyCount = outAnalysis.externalDependencies().size();
    statistics.packetCount = outCompiledGraph.m_packets.size();
    statistics.packetDependencyCount = outCompiledGraph.m_packetDependencies.size();
    statistics.packetExternalDependencyCount = outCompiledGraph.m_packetExternalDependencies.size();
    statistics.externalDependencyCount = statistics.packetExternalDependencyCount;
    statistics.initialOwnershipExternalDependencyCount = initialOwnershipDependencies.size();
    statistics.prologueStateSeedCount = outCompiledGraph.m_prologueStateSeeds.size();
    statistics.prologueBarrierCount = outCompiledGraph.m_prologueBarriers.size();
    statistics.epilogueBarrierCount = outCompiledGraph.m_epilogueBarriers.size();
    for(const GpuCompiledTask& compiledTask : outCompiledGraph.m_tasks){
        const GpuTaskGraphTaskView task = graph.taskAt(compiledTask.task.index);
        const auto& declaredTask = graph.m_tasks[compiledTask.task.index];
        statistics.resourceUseCount += task.resourceUseCount;
        statistics.directResourceUseCount += declaredTask.directResourceUseCount;
        statistics.declaredResourceSetUseCount += declaredTask.declaredResourceSetUseCount;
        statistics.expandedResourceSetMemberUseCount += declaredTask.expandedResourceSetMemberUseCount;
        if(declaredTask.payload){
            ++statistics.payloadObjectCount;
            statistics.payloadObjectBytes += declaredTask.payloadObjectSize;
        }
        if(compiledTask.packetizationDecision < GpuTaskPacketizationDecision::kCount)
            ++statistics.packetizationDecisionCounts[compiledTask.packetizationDecision];

        const GpuPhysicalQueueInfo* const queue = outCompiledGraph.queueInfo(compiledTask.queue);
        if(queue && queue->queueClass < CommandQueue::kCount)
            ++statistics.taskCountByQueueClass[queue->queueClass];
    }
    for(usize packetIndex = 0u; packetIndex < outCompiledGraph.m_packets.size(); ++packetIndex){
        const GpuSubmissionPacket& packet = outCompiledGraph.m_packets[packetIndex];
        if(packet.taskCount > 1u)
            statistics.mergedTaskCount += packet.taskCount - 1u;
        if(packet.recordingFrontier != Limit<u32>::s_Max)
            statistics.recordingFrontierCount = Max(
                statistics.recordingFrontierCount,
                static_cast<usize>(packet.recordingFrontier) + 1u
            );

        const GpuPhysicalQueueInfo* const queue = outCompiledGraph.queueInfo(packet.queue);
        if(queue && queue->queueClass < CommandQueue::kCount)
            ++statistics.packetCountByQueueClass[queue->queueClass];

        const GpuPacketDependency* const dependencies = packet.dependencyCount > 0u
            ? outCompiledGraph.m_packetDependencies.data() + packet.dependencyOffset
            : nullptr
        ;
        for(u32 dependencyIndex = 0u; dependencies && dependencyIndex < packet.dependencyCount; ++dependencyIndex){
            const GpuPacketDependency& dependency = dependencies[dependencyIndex];
            if(
                !dependency.producer.valid()
                || dependency.producer.generation != outCompiledGraph.m_planGeneration
                || dependency.producer.index >= outCompiledGraph.m_packets.size()
            )
                continue;
            const GpuSubmissionPacket& producer = outCompiledGraph.m_packets[dependency.producer.index];
            if(producer.queue == packet.queue)
                continue;

            ++statistics.crossQueuePacketDependencyCount;
            const GpuPhysicalQueueInfo* const producerQueue = outCompiledGraph.queueInfo(producer.queue);
            if(producerQueue && queue && producerQueue->familyIndex != queue->familyIndex)
                ++statistics.crossFamilyPacketDependencyCount;
        }
    }
    const auto countBarriers = [&](const GraphicsVector<GpuCompiledBarrier>& barriers){
        for(const GpuCompiledBarrier& barrier : barriers){
            switch(barrier.type){
            case GpuCompiledBarrierType::TextureTransition:
            case GpuCompiledBarrierType::BufferTransition:
            case GpuCompiledBarrierType::AccelStructTransition:
                ++statistics.transitionBarrierCount;
                break;
            case GpuCompiledBarrierType::TextureUav:
            case GpuCompiledBarrierType::BufferUav:
            case GpuCompiledBarrierType::AccelStructUav:
                ++statistics.uavBarrierCount;
                break;
            case GpuCompiledBarrierType::TextureOwnershipRelease:
            case GpuCompiledBarrierType::BufferOwnershipRelease:
            case GpuCompiledBarrierType::AccelStructOwnershipRelease:
                ++statistics.ownershipReleaseBarrierCount;
                break;
            case GpuCompiledBarrierType::TextureOwnershipAcquire:
            case GpuCompiledBarrierType::BufferOwnershipAcquire:
            case GpuCompiledBarrierType::AccelStructOwnershipAcquire:
                ++statistics.ownershipAcquireBarrierCount;
                break;
            case GpuCompiledBarrierType::TextureStateExport:
            case GpuCompiledBarrierType::BufferStateExport:
            case GpuCompiledBarrierType::AccelStructStateExport:
                ++statistics.stateExportBarrierCount;
                break;
            default:
                break;
            }
        }
    };
    countBarriers(outCompiledGraph.m_prologueBarriers);
    countBarriers(outCompiledGraph.m_epilogueBarriers);
    statistics.declarationSeconds = IsFinite(options.declarationSeconds) && options.declarationSeconds >= 0.0
        ? options.declarationSeconds
        : 0.0
    ;
    statistics.analysisSeconds = analysisSeconds;
    statistics.queueAssignmentSeconds = queueAssignmentSeconds;
    statistics.planningSeconds = DurationInSeconds<f64>(TimerNow(), planningBegin);
    statistics.packetizationSeconds = packetizationSeconds;
    statistics.resourceStatePlanningSeconds = resourceStatePlanningSeconds;
    statistics.packetDependencyPlanningSeconds = packetDependencyPlanningSeconds;
    statistics.totalSeconds = DurationInSeconds<f64>(TimerNow(), compileBegin);

    outCompiledGraph.m_valid = true;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

