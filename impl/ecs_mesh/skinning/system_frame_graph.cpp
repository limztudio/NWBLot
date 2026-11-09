// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "system.h"
#include "frame_graph_tasks.h"
#include "frame_plan_builder.h"
#include "graph_resource_uses.h"
#include "local_bounds.h"
#include "skinning_state_handoff.h"
#include "timing_names.h"

#include <impl/assets/graphics/mesh/runtime_bounds_constants.h>

#include <core/common/log.h>
#include <core/ecs/world.h>
#include <core/graphics/backend_selection/backend.h>
#include <core/graphics/gpu_timing.h>
#include <core/graphics/rhi/gpu_descriptor_heap.h>
#include <core/graphics/runtime/runtime.h>
#include <core/task/gpu/compiled_graph.h>
#include <core/task/gpu/scheduler.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 CreateMeshSkinningLocalBoundsBuffers(Core::GraphicsRuntime& graphics, MeshSkinningRuntimeInstance& instance){
    instance.deformationState.invalidateCurrent();
    if(!ValidateMeshSkinningLocalBoundsMeshletCount(instance.meshlets.size(), NWB_RUNTIME_MESH_BOUNDS_BYTE_SIZE))
        return false;
    const auto partialName = DeriveRuntimeResourceName(instance.sourceName, instance.handle.value, instance.editRevision, s_MeshletLocalBoundsSuffix);
    const auto outputName = DeriveRuntimeResourceName(instance.sourceName, instance.handle.value, instance.editRevision, s_LocalBoundsSuffix);
    if(!partialName || !outputName){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to derive runtime local bounds buffer names"));
        return false;
    }
    Core::BufferDesc desc;
    desc.setByteSize(static_cast<u64>(instance.meshlets.size()) * NWB_RUNTIME_MESH_BOUNDS_BYTE_SIZE).setCanHaveUAVs(true).setCanHaveRawViews(true).setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute).setDebugName(partialName);
    auto partialsResult = graphics.createBuffer(desc);
    if(!partialsResult){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to create runtime local bounds partials"));
        return false;
    }
    Core::BufferHandle partials = Move(*partialsResult);
    desc.setByteSize(NWB_RUNTIME_MESH_BOUNDS_BYTE_SIZE).setDebugName(outputName);
    auto outputResult = graphics.createBuffer(desc);
    if(!outputResult){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to create runtime local bounds output"));
        return false;
    }
    Core::BufferHandle output = Move(*outputResult);
    instance.meshletLocalBoundsBuffer = Move(partials);
    instance.localBoundsBuffer = Move(output);
    return true;
}

b8 MeshSkinningLocalBoundsTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
    if(payload.dispatches.empty())
        return false;
    auto& device = payload.graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized())
        return false;
    Core::GpuTimingSubmissionTicket::RecordingScope recording(payload.timingTicket);
    for(const MeshSkinningLocalBoundsDispatch& plan : payload.dispatches){
        Core::ComputePipeline* const pipeline = context.declarations.computePipelineFor(plan.localBoundsPipeline);
        if(!pipeline || !context.declarations.bufferForResource(plan.bindlessResourceSlotsResource) || !context.declarations.bufferForResource(plan.meshletLocalBoundsResource) || !context.declarations.bufferForResource(plan.localBoundsResource))
            return false;
        Core::ComputeState state;
        state.setPipeline(pipeline);
        commandList.setComputeState(state);
        heap.bindCompute(commandList, *pipeline);
        const MeshletBoundsPushConstants push = MakeMeshletBoundsPushConstants(plan.meshletCount, plan.bindlessResourceSlots);
        commandList.setPushConstants(&push, sizeof(push));
        Core::GpuTimingMeasure timing(payload.graphics.gpuTiming(), MeshSkinningGpuTimingScope::s_LocalBounds, device, commandList);

        commandList.dispatch(1u, 1u, 1u);
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Core::GpuTaskSchedulingHint MeshSkinningGraphDetail::MakeSerialMergeScheduling(const Core::GpuTaskCostHint::Enum cost)noexcept{
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = cost;
    scheduling.overlapPreferred = false;
    scheduling.avoidQueueCrossing = true;
    scheduling.forceSubmissionBoundary = false;
    scheduling.allowPacketMerge = true;
    // The external renderer consumes one accepted skinning state on this serial timeline.
    scheduling.mergeWithPrevious = true;
    scheduling.frontierScoredMergeDomain = s_SerialMergeDomain;
    return scheduling;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 MeshSkinningSystem::TaskGraphSkinningFinalizerTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context)noexcept{
    if(payload.plans.empty())
        return false;

    for(const MeshSkinningGraphDispatchPlan& plan : payload.plans){
        if(plan.hasActiveSkin || plan.copiedRestStreams){
            Core::Buffer* const skinnedPosition = context.declarations.bufferForResource(plan.skinnedPositionResource);
            Core::Buffer* const skinnedNormal = context.declarations.bufferForResource(plan.skinnedNormalResource);
            Core::Buffer* const skinnedTangent = context.declarations.bufferForResource(plan.skinnedTangentResource);
            if(
                !skinnedPosition
                || !skinnedNormal
                || !skinnedTangent
                || commandList.getBufferState(skinnedPosition) != Core::ResourceStates::ShaderResource
                || commandList.getBufferState(skinnedNormal) != Core::ResourceStates::ShaderResource
                || commandList.getBufferState(skinnedTangent) != Core::ResourceStates::ShaderResource
            )
                return false;
        }
        if(plan.updatesMeshletBounds){
            Core::Buffer* const meshletBounds = context.declarations.bufferForResource(plan.meshletBoundsResource);
            Core::Buffer* const localBounds = context.declarations.bufferForResource(plan.localBoundsResource);
            if(!meshletBounds || !localBounds || commandList.getBufferState(meshletBounds) != Core::ResourceStates::ShaderResource || commandList.getBufferState(localBounds) != Core::ResourceStates::ShaderResource)
                return false;
        }
        if(plan.repacksNormals){
            Core::Buffer* const attributes = context.declarations.bufferForResource(plan.attributeResource);
            if(!attributes || commandList.getBufferState(attributes) != Core::ResourceStates::ShaderResource)
                return false;
        }
    }
    return true;
}

void MeshSkinningSystem::TaskGraphSkinningFinalizerTask::Accepted(Payload& payload, const Core::QueueSubmissionToken& token)noexcept{
    if(!token.valid())
        return;
    for(const MeshSkinningGraphDispatchPlan& plan : payload.plans)
        payload.system.confirmGraphOwnedSkinningDispatch(plan);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 MeshSkinningSystem::submitFrameSkinningGraph(){
    auto& device = m_graphics.getDevice();
    const Core::GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    if(!topology.queues || topology.queueCount == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: cannot declare graph-owned skinning without a physical queue topology"));
        return false;
    }

    // Compiler artifacts are reused member storage reset per submission; render() never constructs graph objects.
    resetFrameTaskGraph();
    Core::GpuTaskGraph& graph = m_frameTaskGraph;
    Core::Alloc::ScratchArena scratchArena(s_FrameUploadArena);
    // Dispatch plans are reused member storage; only capacity growth may allocate, never per-frame creation.
    Vector<MeshSkinningGraphDispatchPlan, Core::Alloc::GlobalArena>& dispatchPlans = m_frameDispatchPlans;
    dispatchPlans.clear();
    auto skinningBindings = m_world.view<SkinnedMeshBindingComponent>();
    dispatchPlans.reserve(skinningBindings.candidateCount());
    b8 declarationFailed = false;

    FramePlanBuilder planBuilder(*this, graph, scratchArena, dispatchPlans);
    skinningBindings.each([&](Core::ECS::EntityID entity, SkinnedMeshBindingComponent& binding){
        if(!declarationFailed && !planBuilder.declare(entity, binding))
            declarationFailed = true;
    });
    Core::GpuTaskId terminalTask = planBuilder.terminalTask();

    if(declarationFailed)
        return false;
    if(dispatchPlans.empty())
        return true;

    const auto resourceUses = BuildMeshSkinningGraphResourceUses(dispatchPlans, scratchArena);
    if(!resourceUses)
        return false;

    // Retain prior-frame state with its consuming task; source stays serial by contract.
    const Array<Core::GpuTaskExternalStateSource, 1> previousFrameStateSources = { Core::GpuTaskExternalStateSource{ .states = m_acceptedSkinningState.source(), }, };
    const usize previousFrameStateSourceCount = m_acceptedSkinningState.valid() ? previousFrameStateSources.size() : 0u;

    Core::GpuTimingSubmissionTicket timingTicket(m_graphics.gpuTiming());
    // Dispatch payload slices reference reused member storage that outlives m_frameTaskGraph.
    Vector<MeshSkinningLocalBoundsDispatch, Core::Alloc::GlobalArena>& localBoundsDispatches = m_frameLocalBoundsDispatches;
    localBoundsDispatches.clear();
    localBoundsDispatches.reserve(dispatchPlans.size());
    for(const MeshSkinningGraphDispatchPlan& plan : dispatchPlans){
        if(plan.updatesMeshletBounds){
            localBoundsDispatches.push_back({
                .bindlessResourceSlotsResource = plan.bindlessResourceSlotsResource,
                .meshletLocalBoundsResource = plan.meshletLocalBoundsResource,
                .localBoundsResource = plan.localBoundsResource,
                .localBoundsPipeline = plan.localBoundsPipeline,
                .meshletCount = plan.meshletCount,
                .bindlessResourceSlots = plan.bindlessResourceSlots,
            });
        }
    }
    const Span<const MeshSkinningGraphDispatchPlan> allDispatchPlans(dispatchPlans);
    TaskGraphSkinningFinalizerTask::Payload finalizerPayload(*this, allDispatchPlans);
    MeshSkinningLocalBoundsTask::Payload localBoundsPayload(m_graphics, timingTicket, localBoundsDispatches);
    Core::GpuTaskId postDispatchDependency = terminalTask;
    if(!resourceUses->deformation.empty()){
        Vector<MeshSkinningGraphDispatchPlan, Core::Alloc::GlobalArena>& deformationPlans = m_frameDeformationPlans;
        deformationPlans.clear();
        deformationPlans.reserve(dispatchPlans.size());
        for(const MeshSkinningGraphDispatchPlan& plan : dispatchPlans){
            if(plan.hasActiveSkin)
                deformationPlans.push_back(plan);
        }
        TaskGraphSkinningDeformationTask::Payload deformationPayload(*this, timingTicket, deformationPlans);

        Core::GpuTaskDesc deformationDesc;
        deformationDesc
            .setIdentity(s_FrameDeformationTaskName)
            .setMarkerLabel(s_FrameDeformationTaskLabel)
            .setScheduling(MeshSkinningGraphDetail::SkinningDispatchScheduling())
            .setResourceUses(resourceUses->deformation)
        ;
        if(previousFrameStateSourceCount != 0u)
            deformationDesc.setExternalStateSources({ previousFrameStateSources.data(), previousFrameStateSourceCount });
        if(terminalTask.valid())
            deformationDesc.setDependencies({ &terminalTask, 1u });
        const auto deformationTaskResult = graph.addTask<TaskGraphSkinningDeformationTask>(deformationDesc, Move(deformationPayload));
        if(!deformationTaskResult){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to declare graph-owned skinning deformation"));
            return false;
        }
        const Core::GpuTaskId deformationTask = *deformationTaskResult;
        postDispatchDependency = deformationTask;
    }

    TaskGraphSkinningPostDispatchTask::Payload postDispatchPayload(*this, timingTicket, allDispatchPlans);
    Core::GpuTaskDesc postDispatchDesc;
    postDispatchDesc
        .setIdentity(s_FrameBoundsRepackTaskName)
        .setMarkerLabel(s_FrameBoundsRepackTaskLabel)
        .setScheduling(MeshSkinningGraphDetail::SkinningDispatchScheduling())
        .setResourceUses(resourceUses->postDispatch)
    ;
    if(previousFrameStateSourceCount != 0u)
        postDispatchDesc.setExternalStateSources({ previousFrameStateSources.data(), previousFrameStateSourceCount });
    if(postDispatchDependency.valid())
        postDispatchDesc.setDependencies({ &postDispatchDependency, 1u });
    const auto postDispatchTaskResult = graph.addTask<TaskGraphSkinningPostDispatchTask>(postDispatchDesc, Move(postDispatchPayload));
    if(!postDispatchTaskResult){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to declare graph-owned skinning bounds/repack"));
        return false;
    }
    const Core::GpuTaskId postDispatchTask = *postDispatchTaskResult;

    Core::GpuTaskDesc localBoundsDesc;
    localBoundsDesc
        .setIdentity(s_FrameLocalBoundsTaskName)
        .setMarkerLabel(s_FrameLocalBoundsTaskLabel)
        .setScheduling(MeshSkinningGraphDetail::SkinningDispatchScheduling())
        .setDependencies({ &postDispatchTask, 1u })
        .setResourceUses(resourceUses->localBounds)
    ;
    if(previousFrameStateSourceCount != 0u)
        localBoundsDesc.setExternalStateSources({ previousFrameStateSources.data(), previousFrameStateSourceCount });
    const auto localBoundsTaskResult = graph.addTask<MeshSkinningLocalBoundsTask>(localBoundsDesc, Move(localBoundsPayload));
    if(!localBoundsTaskResult){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to declare graph-owned local bounds reduction"));
        return false;
    }
    const Core::GpuTaskId localBoundsTask = *localBoundsTaskResult;

    const Core::GpuTaskDesc finalizerDesc = Core::GpuTaskDesc{}
        .setIdentity(s_FrameFinalizeStatesTaskName)
        .setMarkerLabel(s_FrameFinalizeStatesTaskLabel)
        .setScheduling(MeshSkinningGraphDetail::SkinningDispatchScheduling())
        .setDependencies({ &localBoundsTask, 1u })
        .setResourceUses(resourceUses->finalizer)
    ;
    const auto finalizerTaskResult = graph.addTask<TaskGraphSkinningFinalizerTask>(finalizerDesc, Move(finalizerPayload));
    if(!finalizerTaskResult){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to declare graph-owned skinning final-state handoff"));
        return false;
    }
    const Core::GpuTaskId finalizerTask = *finalizerTaskResult;
    terminalTask = finalizerTask;

    Core::GpuTaskGraphAnalysis& analysis = m_frameTaskGraphAnalysis;
    Core::GpuTaskGraphQueueAssignments& assignments = m_frameTaskGraphQueueAssignments;
    Core::GpuCompiledGraph& compiledGraph = m_frameCompiledGraph;
    Core::GpuRecordedGraph& recordedGraph = m_frameRecordedGraph;
    Core::GpuGraphSubmissionTransaction& transaction = m_frameSubmissionTransaction;
    Core::GpuTaskGraphCompileOptions compileOptions;
    compileOptions.packetizationPolicy = Core::GpuTaskGraphPacketizationPolicy::FrontierScored;
    const Core::GpuTaskScheduler& scheduler = m_graphics.gpuTasks();
    if(!scheduler.scheduleGraph(graph, compileOptions, scratchArena, analysis, assignments, compiledGraph, recordedGraph, transaction)){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to prepare graph-owned skinning work"));
        return false;
    }

    const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
    const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);
    if(!compiledPlan.validFor(declarations)){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to retain graph-owned skinning compiler inputs"));
        return false;
    }

    if(compiledPlan.packetCount() != 1u){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: graph-owned skinning work did not merge into one primary Graphics packet"));
        return false;
    }

    const Core::GpuPhysicalQueueInfo* const terminalQueue = compiledPlan.queueInfoForTask(terminalTask);
    const Core::GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(Core::CommandQueue::Graphics);
    if(!terminalQueue || terminalQueue->id != graphicsQueue){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: graph-owned skinning work did not retain the primary Graphics queue"));
        return false;
    }

    // Live-buffer list is reused member storage; only capacity growth may allocate, never per-frame creation.
    Vector<Core::BufferHandle, Core::Alloc::GlobalArena>& liveBuffers = m_frameLiveBuffers;
    liveBuffers.clear();
    collectLiveSkinningStateBuffers(scratchArena, liveBuffers);
    SkinningStateHandoff skinningState(m_acceptedSkinningState, scratchArena, liveBuffers);
    const Core::GpuTaskGraphTaskRecordedCallback recordedCallback{ .task = terminalTask, .context = &skinningState, .invoke = &SkinningStateHandoff::Prepare, };
    const Core::GpuTaskGraphTaskAcceptedCallback acceptedCallback{ .task = terminalTask, .context = &skinningState, .invoke = &SkinningStateHandoff::Accept, };

    const Array<Core::GpuTaskGraphTaskTimingTicket, 1> timingTickets = { Core::GpuTaskGraphTaskTimingTicket{ .task = terminalTask, .timingTicket = &timingTicket, }, };
    Core::GpuTaskGraphNormalExecutionDesc normalExecution;
    normalExecution.taskRecordedCallbacks = &recordedCallback;
    normalExecution.taskRecordedCallbackCount = 1u;
    normalExecution.taskTimingTickets = timingTickets.data();
    normalExecution.taskTimingTicketCount = timingTickets.size();
    normalExecution.taskAcceptedCallbacks = &acceptedCallback;
    normalExecution.taskAcceptedCallbackCount = 1u;
    const b8 skinningGraphAccepted = scheduler.executeGraph(normalExecution, scratchArena, graph, compiledGraph, recordedGraph, transaction, nullptr);
    const Core::QueueSubmissionToken skinningToken = transaction.taskToken(compiledPlan, terminalTask);
    if(!skinningToken.valid()){
        if(!transaction.discardUnaccepted(graph, compiledGraph, recordedGraph.recordingAttemptGeneration()))
            m_graphics.requestDeviceRecreation();
        if(!skinningState.preparationInvoked())
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to record graph-owned skinning work"));
        else if(!skinningState.statePrepared())
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to prepare graph-owned skinning frame state"));
        else
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: graph-owned skinning submission was rejected"));
        return false;
    }
    if(!skinningGraphAccepted || !skinningState.stateAccepted()){
        m_graphics.requestDeviceRecreation();
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: accepted graph-owned skinning submission lost its retained state"));
        return false;
    }
    if(!skinningToken.matchesPhysicalQueue(graphicsQueue.index, graphicsQueue.deviceGeneration)){
        m_graphics.requestDeviceRecreation();
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: graph-owned skinning submission lost its Graphics queue identity"));
        return false;
    }
    return true;
}

void MeshSkinningSystem::resolveFrameTaskGraphAttempt(const Core::GpuCompiledGraph::ReadView& planAccess)noexcept{
    // This graph owns only skinning tasks and built-in copies/uploads; their discard hooks are absent or clear a token without callbacks or allocation.
    if(!planAccess.valid())
        return;
    const u64 recordingAttemptGeneration = m_frameRecordedGraph.recordingAttemptGeneration();
    if(recordingAttemptGeneration != 0u && !m_frameSubmissionTransaction.discardUnaccepted(m_frameTaskGraph, m_frameCompiledGraph, recordingAttemptGeneration))
        NWB_FATAL_ASSERT_MSG(false, "skinning task-graph reset must resolve every unaccepted packet");
}

void MeshSkinningSystem::resetFrameTaskGraph(){
    {
        const Core::GpuCompiledGraph::ReadView planAccess(m_frameCompiledGraph);
        resolveFrameTaskGraphAttempt(planAccess);
        if(planAccess.valid()){
            m_frameSubmissionTransaction.reset(m_frameCompiledGraph);
            m_frameRecordedGraph.reset(m_frameCompiledGraph);
        }
    }

    m_frameTaskGraph.reset();
    m_frameTaskGraphAnalysis.reset();
    m_frameTaskGraphQueueAssignments.reset();
    m_frameCompiledGraph.reset();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

