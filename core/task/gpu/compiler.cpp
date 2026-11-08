// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler.h"
#include "compiler_internal.h"
#include "compiler_ownership_validator.h"
#include "compiler_frontier_validator.h"

#include <global/simd/vector_arithmetic.h>
#include <global/scope_exit.h>
#include <global/simplemath.h>
#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u32 GpuTaskGraphSchedulingTaskIndexView::operator[](const usize index)const noexcept{
    NWB_ASSERT(index < taskCount);
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

    const Timer compileBegin = TimerNow();
    const Timer analysisBegin = compileBegin;
    if(!analyze(graph, scratchArena, outAnalysis))
        return false;
    const f64 analysisSeconds = DurationInSeconds<f64>(TimerNow(), analysisBegin);

    if(!options.allowMetadataOnlyTasks){
        for(usize taskIndex = 0u; taskIndex < graph.taskCount(); ++taskIndex){
            const GpuTaskGraphTaskView task = graph.taskAt(taskIndex);
            if(!task.hasPayload || !task.hasRecordPayload){
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
    if(!assignQueues(graph, outAnalysis, topology, options.queueAssignmentOptions, scratchArena, outAssignments))
        return false;
    const f64 queueAssignmentSeconds = DurationInSeconds<f64>(TimerNow(), queueAssignmentBegin);
    const Timer planningBegin = TimerNow();

    NWB_ASSERT(topology.queues && topology.queueCount != 0u);
    if(options.packetizationPolicy >= GpuTaskGraphPacketizationPolicy::Count || !graph.validForDeviceGeneration(topology.queues[0].id.deviceGeneration))
        return false;

    if(!GpuTaskGraphOwnershipValidator::ValidateCompletionQueues(graph, topology))
        return false;

    outCompiledGraph.m_generation = graph.generation();
    outCompiledGraph.m_declarationRevision = graph.declarationRevision();
    outCompiledGraph.m_planGeneration = AllocateCompiledPlanGeneration();
    outCompiledGraph.m_deviceGeneration = topology.queues[0].id.deviceGeneration;
    outCompiledGraph.m_graphTaskCount = graph.taskCount();
    outCompiledGraph.m_compiledTaskIndexByTask.resize(graph.taskCount(), Limit<u32>::s_Max);
    outCompiledGraph.m_tasks.reserve(graph.taskCount());
    outCompiledGraph.m_packets.reserve(graph.taskCount());
    outCompiledGraph.m_packetTasks.reserve(graph.taskCount());
    outCompiledGraph.m_prologueStateSeeds.reserve(graph.taskCount());
    outCompiledGraph.m_prologueBarriers.reserve(graph.taskCount());
    outCompiledGraph.m_epilogueBarriers.reserve(graph.taskCount());
    outCompiledGraph.m_ownershipTransfers.reserve(graph.taskCount());
    outCompiledGraph.m_externalResourceExports.reserve(graph.resourceCount());
    outCompiledGraph.m_externalResourceExportSources.reserve(graph.resourceCount());
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

    if(!GpuTaskGraphOwnershipValidator::ValidateInitialOwnership(graph, topology, compiledPlan, scratchArena))
        return false;

    const Timer packetizationBegin = TimerNow();
    const auto timingEnvelope = BuildSubmissionPackets(graph, outAnalysis, outAssignments, options.packetizationPolicy, options.packetTimingEnvelope, compiledPlan);
    if(!timingEnvelope)
        return false;
    outCompiledGraph.m_packetTimingEnvelopeRange = *timingEnvelope;
    const f64 packetizationSeconds = DurationInSeconds<f64>(TimerNow(), packetizationBegin);

    const Timer resourceStatePlanningBegin = TimerNow();
    // Start with graph-planned packet state seeds, transitions, UAV dependencies, and exclusive-family ownership releases.
    // A state seed carries the producer's final snapshot plus its release target into open(), where the paired acquire emits before use.
    Vector<TrackedCompiledResourceState, Alloc::ScratchArena> trackedResourceStates(scratchArena);
    Vector<PendingCompiledEpilogueBarrier, Alloc::ScratchArena> pendingEpilogueBarriers(scratchArena);
    Vector<GpuTaskExternalDependencyEdge, Alloc::ScratchArena> initialOwnershipDependencies(scratchArena);
    Vector<GpuTaskExternalDependencyEdge, Alloc::ScratchArena> initialAvailabilityDependencies(scratchArena);
    Vector<GpuPacketDependency, Alloc::ScratchArena> resourceStateDependencies(scratchArena);
    Vector<TrackedResourceStateFragment, Alloc::ScratchArena> stateFragments(scratchArena);
    Vector<GpuTaskResourceRange, Alloc::ScratchArena> taskFirstUseRanges(scratchArena);
    // taskAt() exposes the expanded resource-use set. Each use appends at most one tracked state.
    usize trackedStateCapacity = 0u;
    for(const GpuCompiledTask& compiledTask : compiledPlan.tasks){
        const usize useCount = graph.taskAt(compiledTask.task.index).resourceUseCount;
        if(useCount > Limit<usize>::s_Max - trackedStateCapacity)
            return false;
        trackedStateCapacity += useCount;
    }
    trackedResourceStates.reserve(trackedStateCapacity);
    TrackedResourceStateHistory resourceHistory(graph.resourceCount(), graph.generation(), trackedResourceStates, scratchArena);
    pendingEpilogueBarriers.reserve(graph.taskCount());
    initialOwnershipDependencies.reserve(graph.taskCount());
    initialAvailabilityDependencies.reserve(graph.taskCount());
    resourceStateDependencies.reserve(graph.taskCount());
    stateFragments.reserve(graph.taskCount());
    taskFirstUseRanges.reserve(graph.taskCount());
    GpuTaskGraphResourceStatePlan resourceStatePlan{
        .graph = graph,
        .topology = topology,
        .topologicalOrder = outAnalysis.topologicalOrder(),
        .compiledPlan = compiledPlan,
        .scratchArena = scratchArena,
        .trackedResourceStates = trackedResourceStates,
        .resourceHistory = resourceHistory,
        .pendingEpilogueBarriers = pendingEpilogueBarriers,
        .initialOwnershipDependencies = initialOwnershipDependencies,
        .initialAvailabilityDependencies = initialAvailabilityDependencies,
        .resourceStateDependencies = resourceStateDependencies,
        .stateFragments = stateFragments,
        .taskFirstUseRanges = taskFirstUseRanges,
    };
    if(!PlanTaskResourceStates(resourceStatePlan) || !PlanExternalResourceExports(resourceStatePlan) || !AppendPendingEpilogueBarriers(resourceStatePlan)){
        return false;
    }
    const f64 resourceStatePlanningSeconds = DurationInSeconds<f64>(TimerNow(), resourceStatePlanningBegin);

    const Timer packetDependencyPlanningBegin = TimerNow();
    if(!PlanPacketDependencies(graph, outAnalysis, initialOwnershipDependencies, initialAvailabilityDependencies, scratchArena, resourceStateDependencies, compiledPlan)){
        return false;
    }
    const f64 packetDependencyPlanningSeconds = DurationInSeconds<f64>(TimerNow(), packetDependencyPlanningBegin);

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
    statistics.taskCount = graph.taskCount();
    statistics.resourceCount = graph.resourceCount();
    statistics.resourceVersionCount = graph.resourceVersionCount();
    statistics.resourceVersionEdgeCount = outAnalysis.resourceVersionEdgeCount();
    statistics.resourceSetCount = graph.resourceSetCount();
    for(usize resourceSetIndex = 0u; resourceSetIndex < graph.resourceSetCount(); ++resourceSetIndex)
        statistics.resourceSetMemberCount += graph.resourceSetAt(resourceSetIndex).memberCount;
    statistics.uploadBlobCount = graph.uploadBlobCount();
    for(usize uploadBlobIndex = 0u; uploadBlobIndex < graph.uploadBlobCount(); ++uploadBlobIndex){
        const auto bytes = graph.uploadBlobData(GpuUploadBlobId{ .generation = graph.generation(), .index = static_cast<u32>(uploadBlobIndex) });
        NWB_ASSERT(bytes);
        statistics.uploadBlobBytes += bytes->size();
    }
    statistics.explicitDependencyCount = outAnalysis.explicitEdgeCount();
    statistics.inferredDependencyCount = outAnalysis.inferredEdgeCount();
    statistics.declaredExternalDependencyCount = outAnalysis.externalDependencies().size();
    statistics.packetCount = outCompiledGraph.m_packets.size();
    statistics.packetDependencyCount = outCompiledGraph.m_packetDependencies.size();
    statistics.packetExternalDependencyCount = outCompiledGraph.m_packetExternalDependencies.size();
    statistics.externalDependencyCount = statistics.packetExternalDependencyCount;
    statistics.initialOwnershipExternalDependencyCount = initialOwnershipDependencies.size();
    statistics.initialAvailabilityExternalDependencyCount = initialAvailabilityDependencies.size();
    statistics.prologueStateSeedCount = outCompiledGraph.m_prologueStateSeeds.size();
    statistics.prologueBarrierCount = outCompiledGraph.m_prologueBarriers.size();
    statistics.epilogueBarrierCount = outCompiledGraph.m_epilogueBarriers.size();
    outCompiledGraph.buildPlanStatistics(graph, scratchArena);
    if(IsFinite(options.declarationSeconds)){
        // Keep values >= 0, including -0.0; negative values become +0.0.
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
    statistics.planningSeconds = DurationInSeconds<f64>(TimerNow(), planningBegin);
    statistics.packetizationSeconds = packetizationSeconds;
    statistics.resourceStatePlanningSeconds = resourceStatePlanningSeconds;
    statistics.packetDependencyPlanningSeconds = packetDependencyPlanningSeconds;
    statistics.totalSeconds = DurationInSeconds<f64>(TimerNow(), compileBegin);

    outAssignments.m_compiledPlanGeneration = outCompiledGraph.m_planGeneration;
    compilation.publish();
    assignmentPublication.release();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

