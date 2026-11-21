// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler.h"
#include "compiler_internal.h"
#include "compiler_ownership_validator.h"
#include "compiler_frontier_validator.h"

#include <core/graphics/backend_selection/backend.h>

#include <global/simd/vector_arithmetic.h>
#include <global/scope_exit.h>
#include <global/simplemath.h>
#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u32 GpuTaskGraphSchedulingTaskIndexView::operator[](const usize index)const noexcept{
    FOUNDATION_ASSERT(index < taskCount);
    return taskIndices[index];
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 GpuTaskGraphCompiler::compile(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuPhysicalQueueTopology& topology,
    const GpuTaskGraphCompileOptions& options,
    Alloc::ScratchArena& scratchArena,
    GpuTaskGraphAnalysis& outAnalysis,
    GpuTaskGraphQueueAssignments& outAssignments,
    GpuCompiledGraph& outCompiledGraph
)const{
    using namespace GpuTaskGraphCompilerDetail;

    if(!graph.valid()){
        outAnalysis.reset();
        outAnalysis.m_diagnostic.status = GpuTaskGraphAnalysisStatus::InputGraphInUse;
        return false;
    }

    outAssignments.reset();
    ScopeExit assignmentPublication([&outAssignments]()noexcept{
        const GpuTaskQueueAssignmentDiagnostic diagnostic = outAssignments.m_diagnostic;
        outAssignments.reset();
        outAssignments.m_diagnostic = diagnostic;
    });

    GpuCompiledGraph::CompilationScope compilation(outCompiledGraph);
    if(!compilation.valid()){
        outAnalysis.reset();
        outAnalysis.m_diagnostic.status = GpuTaskGraphAnalysisStatus::OutputPlanInUse;
        return false;
    }

    const usize taskCount = CompilerGraphAccess::TaskCount(graph);
    const usize resourceCount = CompilerGraphAccess::ResourceCount(graph);
    const u64 generation = CompilerGraphAccess::Generation(graph);
    b8 hasNonHazardDomainResource = false;
    for(usize resourceIndex = 0u; resourceIndex < resourceCount; ++resourceIndex){
        const GpuTaskGraphDetail::GpuGraphResourceNode& resource = CompilerGraphAccess::ResourceNode(graph, resourceIndex);
        if(resource.type != GpuGraphResourceType::HazardDomain){
            hasNonHazardDomainResource = true;
            break;
        }
        FOUNDATION_ASSERT(resource.externalFinalState == ResourceStates::Unknown);
    }

    const Timer compileBegin = TimerNow();
    const Timer analysisBegin = compileBegin;
    f64 analysisSeconds = 0.0;
    f64 queueAssignmentSeconds = 0.0;
    {
        Alloc::ScratchArena::Scope analysisAndQueueScratchScope(scratchArena);
        DenseDescendantBitMatrix denseDesc(scratchArena);
        if(taskCount != 0u && taskCount <= DenseDescendantBitMatrix::s_MaxTasks){
            const usize wordsPerRow = (taskCount - 1u) / s_BitsPerWord + 1u;
            denseDesc.strictDesc.resize(taskCount * wordsPerRow, 0u);
            denseDesc.taskCount = taskCount;
            denseDesc.wordsPerRow = wordsPerRow;
        }
        if(!analyzeInternal(graph, scratchArena, outAnalysis, &denseDesc))
            return false;
        const Timer postAnalysisTime = TimerNow();
        analysisSeconds = DurationInSeconds<f64>(postAnalysisTime, analysisBegin);

        if(!options.allowMetadataOnlyTasks){
            for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
                const GpuTaskGraphDetail::GpuTaskNode& task = CompilerGraphAccess::TaskNode(graph, taskIndex);
                const GpuTaskId taskId{ .generation = generation, .index = static_cast<u32>(taskIndex) };
                if(!task.payload || !task.recordPayload){
                    outAnalysis.m_diagnostic.status = GpuTaskGraphAnalysisStatus::MissingTaskRecordPayload;
                    outAnalysis.m_diagnostic.task = taskId;
                    outAnalysis.m_diagnostic.relatedTask = {};
                    outAnalysis.m_diagnostic.resource = {};
                    outAnalysis.m_valid = false;
                    return false;
                }
                if(!hasNonHazardDomainResource)
                    continue;

                const Span<const GpuTaskResourceUse> resourceUses = CompilerGraphAccess::TaskResourceUses(graph, task);
                for(const GpuTaskResourceUse& use : resourceUses){
                    const GpuTaskGraphDetail::GpuGraphResourceNode& resource = CompilerGraphAccess::ResourceNode(graph, use.resource.index);
                    switch(resource.type){
                    case GpuGraphResourceType::Texture:
                        if(CompilerGraphAccess::TextureForResourceIndex(graph, use.resource.index))
                            continue;
                        break;
                    case GpuGraphResourceType::Buffer:
                        if(CompilerGraphAccess::BufferForResourceIndex(graph, use.resource.index))
                            continue;
                        break;
                    case GpuGraphResourceType::AccelStruct:
                        if(CompilerGraphAccess::AccelStructForResourceIndex(graph, use.resource.index))
                            continue;
                        break;
                    case GpuGraphResourceType::HazardDomain:
                        continue;
                    default:
                        break;
                    }

                    outAnalysis.m_diagnostic.status = GpuTaskGraphAnalysisStatus::InvalidResourceUse;
                    outAnalysis.m_diagnostic.task = taskId;
                    outAnalysis.m_diagnostic.relatedTask = {};
                    outAnalysis.m_diagnostic.resource = use.resource;
                    outAnalysis.m_valid = false;
                    return false;
                }
            }
        }

        const Timer queueAssignmentBegin = options.allowMetadataOnlyTasks ? postAnalysisTime : TimerNow();
        if(!assignQueuesInternal(graph, outAnalysis, topology, options.queueAssignmentOptions, scratchArena, outAssignments, denseDesc.valid ? &denseDesc : nullptr, hasNonHazardDomainResource))
            return false;
        queueAssignmentSeconds = DurationInSeconds<f64>(TimerNow(), queueAssignmentBegin);
    }
    const Timer planningBegin = TimerNow();

    FOUNDATION_ASSERT(topology.queues && topology.queueCount != 0u);
    if(options.packetizationPolicy >= GpuTaskGraphPacketizationPolicy::Count || !graph.validForDeviceGeneration(topology.queues[0].id.deviceGeneration))
        return false;

    if(!GpuTaskGraphOwnershipValidator::ValidateCompletionQueues(graph, topology))
        return false;

    outCompiledGraph.m_generation = generation;
    outCompiledGraph.m_declarationRevision = CompilerGraphAccess::DeclarationRevision(graph);
    outCompiledGraph.m_planGeneration = AllocateCompiledPlanGeneration();
    outCompiledGraph.m_deviceGeneration = topology.queues[0].id.deviceGeneration;
    outCompiledGraph.m_graphTaskCount = taskCount;
    outCompiledGraph.m_compiledTaskIndexByTask.resize(taskCount, Limit<u32>::s_Max);
    outCompiledGraph.m_tasks.reserve(taskCount);
    outCompiledGraph.m_packets.reserve(taskCount);
    outCompiledGraph.m_packetTasks.reserve(taskCount);
    outCompiledGraph.m_packetDependencies.reserve(outAnalysis.schedulingEdges().size());
    if(hasNonHazardDomainResource){
        outCompiledGraph.m_prologueStateSeeds.reserve(taskCount);
        outCompiledGraph.m_prologueBarriers.reserve(taskCount);
        outCompiledGraph.m_epilogueBarriers.reserve(taskCount);
        outCompiledGraph.m_ownershipTransfers.reserve(taskCount);
        outCompiledGraph.m_externalResourceExports.reserve(resourceCount);
        outCompiledGraph.m_externalResourceExportSources.reserve(resourceCount);
    }
    outCompiledGraph.m_queueTopology.reserve(topology.queueCount);
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex)
        outCompiledGraph.m_queueTopology.push_back(topology.queues[queueIndex]);

    GpuTaskGraphCompiledPlanStorage compiledPlan{
        .tasks = outCompiledGraph.m_tasks,
        .compiledTaskIndexByTask = outCompiledGraph.m_compiledTaskIndexByTask,
        .packets = outCompiledGraph.m_packets,
        .packetTasks = outCompiledGraph.m_packetTasks,
        .packetDependencies = outCompiledGraph.m_packetDependencies,
        .packetExternalDependencies = outCompiledGraph.m_packetExternalDependencies,
        .prologueStateSeeds = outCompiledGraph.m_prologueStateSeeds,
        .prologueBarriers = outCompiledGraph.m_prologueBarriers,
        .epilogueBarriers = outCompiledGraph.m_epilogueBarriers,
        .ownershipTransfers = outCompiledGraph.m_ownershipTransfers,
        .externalResourceExports = outCompiledGraph.m_externalResourceExports,
        .externalResourceExportSources = outCompiledGraph.m_externalResourceExportSources,
        .queueTopology = outCompiledGraph.m_queueTopology,
        .presentEndpoint = outCompiledGraph.m_presentEndpoint,
        .hasPresentEndpoint = outCompiledGraph.m_hasPresentEndpoint,
        .graphGeneration = outCompiledGraph.m_generation,
        .planGeneration = outCompiledGraph.m_planGeneration,
        .deviceGeneration = outCompiledGraph.m_deviceGeneration,
    };

    f64 packetizationSeconds = 0.0;
    f64 resourceStatePlanningSeconds = 0.0;
    f64 packetDependencyPlanningSeconds = 0.0;
    usize initialOwnershipExternalDependencyCount = 0u;
    usize initialAvailabilityExternalDependencyCount = 0u;
    {
        Alloc::ScratchArena::Scope compileScratchScope(scratchArena);
        if(!GpuTaskGraphOwnershipValidator::ValidateInitialOwnership(graph, topology, compiledPlan, scratchArena))
            return false;

        const Timer packetizationBegin = TimerNow();
        const auto timingEnvelope = BuildSubmissionPackets(graph, outAnalysis, outAssignments, options.packetizationPolicy, options.packetTimingEnvelope, compiledPlan);
        if(!timingEnvelope)
            return false;
        outCompiledGraph.m_packetTimingEnvelopeRange = *timingEnvelope;
        const Timer resourceStatePlanningBegin = TimerNow();
        packetizationSeconds = DurationInSeconds<f64>(resourceStatePlanningBegin, packetizationBegin);

        Vector<GpuTaskExternalDependencyEdge, Alloc::ScratchArena> initialOwnershipDependencies(scratchArena);
        Vector<GpuTaskExternalDependencyEdge, Alloc::ScratchArena> initialAvailabilityDependencies(scratchArena);
        Vector<GpuPacketDependency, Alloc::ScratchArena> resourceStateDependencies(scratchArena);
        Timer packetDependencyPlanningBegin = resourceStatePlanningBegin;
        if(hasNonHazardDomainResource){
            const usize totalResourceUseCount = CompilerGraphAccess::TotalResourceUseCount(graph);
            Vector<TrackedCompiledResourceState, Alloc::ScratchArena> trackedResourceStates(scratchArena);
            Vector<PendingCompiledEpilogueBarrier, Alloc::ScratchArena> pendingEpilogueBarriers(scratchArena);
            Vector<TrackedResourceStateFragment, Alloc::ScratchArena> stateFragments(scratchArena);
            Vector<GpuTaskResourceRange, Alloc::ScratchArena> taskFirstUseRanges(scratchArena);
            ResourceRangeScratch rangeScratch(scratchArena);
            trackedResourceStates.reserve(totalResourceUseCount);
            stateFragments.reserve(taskCount);
            taskFirstUseRanges.reserve(taskCount);
            Vector<u8, Alloc::ScratchArena> resourceHasPendingEpilogueBarrier(resourceCount, 0u, scratchArena);
            TrackedResourceStateHistory resourceHistory(resourceCount, generation, trackedResourceStates, scratchArena);
            GpuTaskGraphResourceStatePlan resourceStatePlan{
                .graph = graph,
                .topology = topology,
                .topologicalOrder = outAnalysis.topologicalOrder(),
                .compiledPlan = compiledPlan,
                .scratchArena = scratchArena,
                .trackedResourceStates = trackedResourceStates,
                .resourceHistory = resourceHistory,
                .pendingEpilogueBarriers = pendingEpilogueBarriers,
                .resourceHasPendingEpilogueBarrier = resourceHasPendingEpilogueBarrier,
                .initialOwnershipDependencies = initialOwnershipDependencies,
                .initialAvailabilityDependencies = initialAvailabilityDependencies,
                .resourceStateDependencies = resourceStateDependencies,
                .stateFragments = stateFragments,
                .taskFirstUseRanges = taskFirstUseRanges,
                .rangeScratch = rangeScratch,
            };
            if(!PlanTaskResourceStates(resourceStatePlan) || !PlanExternalResourceExports(resourceStatePlan) || !AppendPendingEpilogueBarriers(resourceStatePlan))
                return false;
            packetDependencyPlanningBegin = TimerNow();
            resourceStatePlanningSeconds = DurationInSeconds<f64>(packetDependencyPlanningBegin, resourceStatePlanningBegin);
        }

        if(!PlanPacketDependencies(graph, outAnalysis, initialOwnershipDependencies, initialAvailabilityDependencies, scratchArena, resourceStateDependencies, compiledPlan))
            return false;
        packetDependencyPlanningSeconds = DurationInSeconds<f64>(TimerNow(), packetDependencyPlanningBegin);
        initialOwnershipExternalDependencyCount = initialOwnershipDependencies.size();
        initialAvailabilityExternalDependencyCount = initialAvailabilityDependencies.size();
    }

    GpuTaskGraphAnalysisDiagnostic frontierDiagnostic;
    if(!GpuTaskGraphFrontierValidator::Validate(graph, compiledPlan, frontierDiagnostic)){
        outAnalysis.m_diagnostic = frontierDiagnostic;
        outAnalysis.m_valid = false;
        return false;
    }

    GpuTaskGraphCompileStatistics& statistics = outCompiledGraph.m_compileStatistics;
    statistics.graphGeneration = outCompiledGraph.m_generation;
    statistics.planGeneration = outCompiledGraph.m_planGeneration;
    statistics.deviceGeneration = outCompiledGraph.m_deviceGeneration;
    statistics.taskCount = taskCount;
    statistics.resourceCount = resourceCount;
    statistics.resourceVersionCount = CompilerGraphAccess::ResourceVersionCount(graph);
    statistics.resourceVersionEdgeCount = outAnalysis.resourceVersionEdgeCount();
    statistics.resourceSetCount = CompilerGraphAccess::ResourceSetCount(graph);
    for(usize resourceSetIndex = 0u; resourceSetIndex < statistics.resourceSetCount; ++resourceSetIndex)
        statistics.resourceSetMemberCount += graph.resourceSetAt(resourceSetIndex).memberCount;
    statistics.uploadBlobCount = CompilerGraphAccess::UploadBlobCount(graph);
    for(usize uploadBlobIndex = 0u; uploadBlobIndex < statistics.uploadBlobCount; ++uploadBlobIndex){
        const auto bytes = graph.uploadBlobData(GpuUploadBlobId{ .generation = generation, .index = static_cast<u32>(uploadBlobIndex) });
        FOUNDATION_ASSERT(bytes);
        statistics.uploadBlobBytes += bytes->size();
    }
    statistics.explicitDependencyCount = outAnalysis.explicitEdgeCount();
    statistics.inferredDependencyCount = outAnalysis.inferredEdgeCount();
    statistics.declaredExternalDependencyCount = outAnalysis.externalDependencies().size();
    statistics.packetCount = outCompiledGraph.m_packets.size();
    statistics.packetDependencyCount = outCompiledGraph.m_packetDependencies.size();
    statistics.packetExternalDependencyCount = outCompiledGraph.m_packetExternalDependencies.size();
    statistics.externalDependencyCount = statistics.packetExternalDependencyCount;
    statistics.initialOwnershipExternalDependencyCount = initialOwnershipExternalDependencyCount;
    statistics.initialAvailabilityExternalDependencyCount = initialAvailabilityExternalDependencyCount;
    statistics.prologueStateSeedCount = outCompiledGraph.m_prologueStateSeeds.size();
    statistics.prologueBarrierCount = outCompiledGraph.m_prologueBarriers.size();
    statistics.epilogueBarrierCount = outCompiledGraph.m_epilogueBarriers.size();
    outCompiledGraph.buildPlanStatistics(graph, scratchArena);
    if(IsFinite(options.declarationSeconds)){
        statistics.declarationSeconds = options.declarationSeconds >= 0.0 ? options.declarationSeconds : 0.0;
    }
    else
        statistics.declarationSeconds = 0.0;
    statistics.analysisSeconds = analysisSeconds;
    statistics.validationSeconds = outAnalysis.m_validationSeconds;
    statistics.dependencyAnalysisSeconds = outAnalysis.m_dependencyAnalysisSeconds;
    statistics.hazardAnalysisSeconds = outAnalysis.m_hazardAnalysisSeconds;
    statistics.topologicalOrderSeconds = outAnalysis.m_topologicalOrderSeconds;
    statistics.queueAssignmentSeconds = queueAssignmentSeconds;
    const Timer compileEnd = TimerNow();
    statistics.planningSeconds = DurationInSeconds<f64>(compileEnd, planningBegin);
    statistics.packetizationSeconds = packetizationSeconds;
    statistics.resourceStatePlanningSeconds = resourceStatePlanningSeconds;
    statistics.packetDependencyPlanningSeconds = packetDependencyPlanningSeconds;
    statistics.totalSeconds = DurationInSeconds<f64>(compileEnd, compileBegin);

    outCompiledGraph.m_packetRuntimeFlags.assign(outCompiledGraph.m_packets.size(), 0u);
    {
        Alloc::ScratchArena::Scope flagsScratchScope(scratchArena);
        Vector<u8, Alloc::ScratchArena> resourceKeepInitialState(hasNonHazardDomainResource ? resourceCount : 0u, static_cast<u8>(255u), scratchArena);
        for(usize packetIndex = 0u; packetIndex < outCompiledGraph.m_packets.size(); ++packetIndex){
            const GpuSubmissionPacket& packetPlan = outCompiledGraph.m_packets[packetIndex];
            u8 flags = GpuCompiledGraph::PacketRuntimeFlag::AllowsParallelRecording;
            if(packetPlan.joinsAcceptedQueueFrontier || (outCompiledGraph.m_hasPresentEndpoint && outCompiledGraph.m_presentEndpoint.packet.index == packetIndex))
                flags = static_cast<u8>(flags | GpuCompiledGraph::PacketRuntimeFlag::HasCrossQueueConsumer);
            for(u32 taskOffset = 0u; taskOffset < packetPlan.taskCount; ++taskOffset){
                const GpuTaskId taskID = outCompiledGraph.m_packetTasks[packetPlan.taskOffset + taskOffset];
                const GpuTaskGraphDetail::GpuTaskNode& task = CompilerGraphAccess::TaskNode(graph, taskID.index);
                const u32 compiledTaskIndex = outCompiledGraph.m_compiledTaskIndexByTask[taskID.index];
                const GpuCompiledTask& compiledTask = outCompiledGraph.m_tasks[compiledTaskIndex];
                if(!task.scheduling.allowParallelRecording)
                    flags = static_cast<u8>(flags & ~GpuCompiledGraph::PacketRuntimeFlag::AllowsParallelRecording);
                if(task.externalStateSourceCount != 0u)
                    flags = static_cast<u8>(flags | GpuCompiledGraph::PacketRuntimeFlag::HasExternalStateSource);
                if(compiledTask.prologueStateSeedCount != 0u)
                    flags = static_cast<u8>(flags | GpuCompiledGraph::PacketRuntimeFlag::HasPrologueStateSeeds);
                if((flags & GpuCompiledGraph::PacketRuntimeFlag::HasInitialOwnerHandoff) == 0u){
                    for(u32 barrierOffset = 0u; barrierOffset < compiledTask.prologueBarrierCount; ++barrierOffset){
                        const GpuCompiledBarrier& barrier = outCompiledGraph.m_prologueBarriers[compiledTask.prologueBarrierOffset + barrierOffset];
                        if(
                            barrier.isInitialOwnerHandoff
                            && (
                                barrier.type == GpuCompiledBarrierType::TextureOwnershipAcquire
                                || barrier.type == GpuCompiledBarrierType::BufferOwnershipAcquire
                                || barrier.type == GpuCompiledBarrierType::AccelStructOwnershipAcquire
                            )
                        ){
                            flags = static_cast<u8>(flags | GpuCompiledGraph::PacketRuntimeFlag::HasInitialOwnerHandoff);
                            break;
                        }
                    }
                }
                if(hasNonHazardDomainResource && (flags & GpuCompiledGraph::PacketRuntimeFlag::HasRetainedInitialStateResource) == 0u){
                    for(const GpuTaskResourceUse& use : CompilerGraphAccess::TaskResourceUses(graph, task)){
                        u8& cachedKeep = resourceKeepInitialState[use.resource.index];
                        if(cachedKeep == 255u){
                            const GpuTaskGraphDetail::GpuGraphResourceNode& resource = CompilerGraphAccess::ResourceNode(graph, use.resource.index);
                            if(resource.type == GpuGraphResourceType::HazardDomain)
                                cachedKeep = 0u;
                            else if(resource.type == GpuGraphResourceType::Texture){
                                Texture* const texture = CompilerGraphAccess::TextureForResourceIndex(graph, use.resource.index);
                                cachedKeep = (!texture || texture->getCreationDescription().keepInitialState) ? 1u : 0u;
                            }
                            else if(resource.type == GpuGraphResourceType::Buffer){
                                Buffer* const buffer = CompilerGraphAccess::BufferForResourceIndex(graph, use.resource.index);
                                cachedKeep = (!buffer || buffer->getCreationDescription().keepInitialState) ? 1u : 0u;
                            }
                            else if(resource.type == GpuGraphResourceType::AccelStruct)
                                cachedKeep = 1u;
                            else
                                cachedKeep = 0u;
                        }
                        if(cachedKeep != 0u){
                            flags = static_cast<u8>(flags | GpuCompiledGraph::PacketRuntimeFlag::HasRetainedInitialStateResource);
                            break;
                        }
                    }
                }
            }
            outCompiledGraph.m_packetRuntimeFlags[packetIndex] = flags;
        }
    }
    for(const GpuPacketDependency& dep : outCompiledGraph.m_packetDependencies){
        if(dep.producer.index < outCompiledGraph.m_packets.size() && dep.consumer.index < outCompiledGraph.m_packets.size()){
            if(outCompiledGraph.m_packets[dep.producer.index].queue != outCompiledGraph.m_packets[dep.consumer.index].queue){
                outCompiledGraph.m_packetRuntimeFlags[dep.producer.index] = static_cast<u8>(
                    outCompiledGraph.m_packetRuntimeFlags[dep.producer.index] | GpuCompiledGraph::PacketRuntimeFlag::HasCrossQueueConsumer
                );
            }
        }
    }
    for(const GpuPacketStateSeed& seed : outCompiledGraph.m_prologueStateSeeds){
        if(seed.sourcePacket.valid() && seed.sourcePacket.index < outCompiledGraph.m_packetRuntimeFlags.size()){
            outCompiledGraph.m_packetRuntimeFlags[seed.sourcePacket.index] = static_cast<u8>(
                outCompiledGraph.m_packetRuntimeFlags[seed.sourcePacket.index] | GpuCompiledGraph::PacketRuntimeFlag::HasExportedStateConsumer
            );
        }
    }
    for(const GpuCompiledExternalResourceExportSource& source : outCompiledGraph.m_externalResourceExportSources){
        if(source.producerTask.valid() && source.producerTask.index < outCompiledGraph.m_compiledTaskIndexByTask.size()){
            const u32 compiledTaskIndex = outCompiledGraph.m_compiledTaskIndexByTask[source.producerTask.index];
            if(compiledTaskIndex < outCompiledGraph.m_tasks.size()){
                const u32 packetIndex = outCompiledGraph.m_tasks[compiledTaskIndex].packet.index;
                if(packetIndex < outCompiledGraph.m_packetRuntimeFlags.size()){
                    outCompiledGraph.m_packetRuntimeFlags[packetIndex] = static_cast<u8>(
                        outCompiledGraph.m_packetRuntimeFlags[packetIndex] | GpuCompiledGraph::PacketRuntimeFlag::HasCrossQueueConsumer | GpuCompiledGraph::PacketRuntimeFlag::HasExportedStateConsumer
                    );
                }
            }
        }
    }
    for(const GpuCompiledExternalResourceExport& exportInfo : outCompiledGraph.m_externalResourceExports){
        if(exportInfo.producerTask.valid() && exportInfo.producerTask.index < outCompiledGraph.m_compiledTaskIndexByTask.size()){
            const u32 compiledTaskIndex = outCompiledGraph.m_compiledTaskIndexByTask[exportInfo.producerTask.index];
            if(compiledTaskIndex < outCompiledGraph.m_tasks.size()){
                const u32 packetIndex = outCompiledGraph.m_tasks[compiledTaskIndex].packet.index;
                if(packetIndex < outCompiledGraph.m_packetRuntimeFlags.size()){
                    outCompiledGraph.m_packetRuntimeFlags[packetIndex] = static_cast<u8>(
                        outCompiledGraph.m_packetRuntimeFlags[packetIndex] | GpuCompiledGraph::PacketRuntimeFlag::HasCrossQueueConsumer | GpuCompiledGraph::PacketRuntimeFlag::HasExportedStateConsumer
                    );
                }
            }
        }
    }

    outAssignments.m_compiledPlanGeneration = outCompiledGraph.m_planGeneration;
    compilation.publish();
    assignmentPublication.release();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

