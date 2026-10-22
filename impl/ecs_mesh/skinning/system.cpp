// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "system.h"
#include "frame_graph_tasks.h"
#include "frame_plan_builder.h"
#include "arena_names.h"
#include "graph_resource_uses.h"
#include "live_state_buffers.h"
#include "local_bounds.h"
#include "resource_names.h"
#include "runtime_mesh_liveness.h"
#include "skin_payload.h"
#include "timing_names.h"

#include <impl/ecs_skeleton/runtime_helpers.h>

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <core/ecs/world.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/gpu_timing.h>
#include <core/graphics/runtime/runtime.h>
#include <core/task/gpu/compiler.h>
#include <core/task/gpu/scheduler.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_system{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr bool s_RuntimeSkinningMeshletFrustumCullingEnabled = true;
static constexpr bool s_RuntimeSkinningMeshletConeCullingEnabled = false; // Deformations make cones unsafe.


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


MeshSkinningSystem::MeshSkinningSystem(Core::Alloc::GlobalArena& arena, Core::ECS::World& world, Core::GraphicsRuntime& graphics, Core::Assets::AssetManager& assetManager, IRuntimeMeshRegistry& runtimeMeshRegistry, ShaderPathResolveCallback shaderPathResolver)
    : Core::ECS::ISystem(arena)
    , Core::IRenderPass(graphics)
    , m_arena(arena)
    , m_world(world)
    , m_graphics(graphics)
    , m_assetManager(assetManager)
    , m_runtimeMeshRegistry(runtimeMeshRegistry)
    , m_shaderPathResolver(Move(shaderPathResolver))
    , m_runtimeMeshCache(arena, graphics, assetManager)
    , m_runtimeResources(0, Hasher<u64>(), EqualTo<u64>(), arena)
    , m_frameDispatchPlans(arena)
    , m_frameLiveBuffers(arena)
    , m_frameTaskGraph(arena)
    , m_frameTaskGraphAnalysis(arena)
    , m_frameTaskGraphQueueAssignments(arena)
    , m_frameCompiledGraph(arena)
    , m_frameRecordedGraph(arena)
    , m_frameSubmissionTransaction(arena)
    , m_acceptedSkinningState(arena)
{
    writeAccess<SkinnedMeshBindingComponent>();
    readAccess<SkeletonJointPaletteComponent>();
    readAccess<SkeletonPoseComponent>();

    m_runtimeMeshRegistry.registerRuntimeMeshProvider(*this);
}

MeshSkinningSystem::~MeshSkinningSystem()noexcept{
    // Retire payloads while their transaction, plan, and timing owners still exist; destruction does not initialize another frame.
    {
        const Core::GpuCompiledGraph::ReadView planAccess(m_frameCompiledGraph);
        resolveFrameTaskGraphAttempt(planAccess);
    }
    m_frameTaskGraph.reset();
    m_runtimeMeshRegistry.unregisterRuntimeMeshProvider(*this);
}

void MeshSkinningSystem::prepare(Core::ECS::World& world){
    static_cast<void>(world);
    m_runtimeSetupReady = false;
    if(!m_skinningComputePipeline || !m_boundsComputePipeline || !m_localBoundsComputePipeline || !m_repackComputePipeline)
        return;
    Core::Alloc::ScratchArena scratchArena(SkinningArenaScope::s_PrepareRuntimeArena);
    m_runtimeMeshCache.prepareResources(m_world, scratchArena);
    pruneRuntimeResources();
    bool ready = true;
    m_world.view<SkinnedMeshBindingComponent>().each([&](Core::ECS::EntityID, SkinnedMeshBindingComponent& binding){
        if(!ready)
            return;
        if(!binding.runtimeMesh.valid())
            return;

        MeshSkinningRuntimeInstance* instance = m_runtimeMeshCache.findInstance(binding.runtimeMesh);
        if(!instance)
            return;
        NWB_ASSERT(instance->valid());

        ready = prepareRuntimeMeshResources(*instance, scratchArena);
    }
    );

    m_runtimeSetupReady = ready;
}

Expected<void, Core::CpuTaskAdmissionError::Enum> MeshSkinningSystem::update(Core::ECS::World& world, const f32 delta)noexcept{
    static_cast<void>(world);
    static_cast<void>(delta);
    return {};
}

bool MeshSkinningSystem::validateResources(const u32 width, const u32 height, const u32 sampleCount){
    static_cast<void>(sampleCount);
    if(width == 0 || height == 0)
        return true;

    auto& device = m_graphics.getDevice();

    constexpr u32 s_PerRuntimeMeshTimingQueries = 128u;
    const bool timingReady =
        m_graphics.gpuTiming().prepareScopeQueries(MeshSkinningGpuTimingScope::s_Skinning.identity, device, s_PerRuntimeMeshTimingQueries)
        && m_graphics.gpuTiming().prepareScopeQueries(MeshSkinningGpuTimingScope::s_MeshletBounds.identity, device, s_PerRuntimeMeshTimingQueries)
        && m_graphics.gpuTiming().prepareScopeQueries(MeshSkinningGpuTimingScope::s_LocalBounds.identity, device, s_PerRuntimeMeshTimingQueries)
        && m_graphics.gpuTiming().prepareScopeQueries(MeshSkinningGpuTimingScope::s_RepackNormals.identity, device, s_PerRuntimeMeshTimingQueries)
    ;
    if(!timingReady)
        NWB_LOGGER_WARNING(NWB_TEXT("MeshSkinningSystem: GPU timing scope preparation failed; timing samples may be skipped"));

    // Pipeline creation lives in resource validation, never in the per-frame prepare path below; prepare only consumes the handles created here.
    if(!ensureSkinningPipeline() || !ensureBoundsPipeline() || !ensureLocalBoundsPipeline() || !ensureRepackPipeline()){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to create skinning pipelines during resource validation"));
        return false;
    }
    return true;
}

bool MeshSkinningSystem::prepareResources(Core::Framebuffer* framebuffer){
    static_cast<void>(framebuffer);
    Core::Alloc::ScratchArena scratchArena(SkinningArenaScope::s_PrepareRuntimeArena);
    // Frames may lack native submissions; filter retired buffers so handles pin nothing.
    if(m_acceptedSkinningState.valid() && !replaceAcceptedSkinningState(*m_acceptedSkinningState.source(), scratchArena)){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to prune the accepted skinning state handoff"));
        return false;
    }

    return m_runtimeSetupReady;
}

void MeshSkinningSystem::render(Core::Framebuffer* framebuffer){
    static_cast<void>(framebuffer);

    if(!submitFrameSkinningGraph())
        NWB_LOGGER_WARNING(NWB_TEXT("MeshSkinningSystem: skipped skinning because graph-owned work was not accepted"));
}

void MeshSkinningSystem::invalidateResources(){
    m_runtimeSetupReady = false;
    resetFrameTaskGraph();
    NWB_FATAL_ASSERT_MSG(m_frameRecordedGraph.releaseCommandListCache(), "skinning invalidation requires joined graph recording before device-bound cache release");
    m_acceptedSkinningState.reset();
    for(auto it = m_runtimeResources.begin(); it != m_runtimeResources.end(); ++it)
        releaseRuntimeResourceBindlessHeapHandles(it.value());
    m_runtimeResources.clear();
    m_runtimeMeshCache.clear();

    m_skinningBindingLayout.reset();
    m_skinningComputeShader.reset();
    m_skinningComputePipeline.reset();
    m_boundsBindingLayout.reset();
    m_boundsComputeShader.reset();
    m_boundsComputePipeline.reset();
    m_localBoundsComputeShader.reset();
    m_localBoundsComputePipeline.reset();
    m_repackBindingLayout.reset();
    m_repackComputeShader.reset();
    m_repackComputePipeline.reset();
}

Expected<RuntimeMeshDesc> MeshSkinningSystem::resolveRuntimeMesh(const Core::ECS::EntityID entity){
    RuntimeMeshHandle runtimeMesh;
    if(const SkinnedMeshBindingComponent* binding = m_world.tryGetComponent<SkinnedMeshBindingComponent>(entity))
        runtimeMesh = binding->runtimeMesh;
    const MeshSkinningRuntimeInstance* const instance = runtimeMesh.valid() ? m_runtimeMeshCache.findInstance(runtimeMesh) : nullptr;
    return BuildSkinnedRuntimeMeshDesc(entity, runtimeMesh, instance, __hidden_system::s_RuntimeSkinningMeshletFrustumCullingEnabled, __hidden_system::s_RuntimeSkinningMeshletConeCullingEnabled);
}

bool MeshSkinningSystem::hasRuntimeMeshBinding(const Core::ECS::EntityID entity)const noexcept{
    return m_world.tryGetComponent<SkinnedMeshBindingComponent>(entity) != nullptr;
}

void MeshSkinningSystem::markLiveRuntimeMeshes(RuntimeMeshRequestSet& requests){
    MarkLiveSkinnedRuntimeMeshes(m_world, requests, [this](const Core::ECS::EntityID entity, const SkinnedMeshBindingComponent& binding){
        const MeshSkinningRuntimeInstance* const instance = binding.runtimeMesh.valid() ? m_runtimeMeshCache.findInstance(binding.runtimeMesh) : nullptr;
        return ResolveSkinnedRuntimeMeshIdentity(entity, binding.runtimeMesh, instance);
    }
    );
}

bool MeshSkinningSystem::submitFrameSkinningGraph(){
    auto& device = m_graphics.getDevice();
    const Core::GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    if(!topology.queues || topology.queueCount == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: cannot declare graph-owned skinning without a physical queue topology"));
        return false;
    }

    // Compiler artifacts are reused member storage reset per submission; render() never constructs graph objects.
    resetFrameTaskGraph();
    Core::GpuTaskGraph& graph = m_frameTaskGraph;
    Core::Alloc::ScratchArena scratchArena(SkinningArenaScope::s_FrameUploadArena);
    // Dispatch plans are reused member storage; only capacity growth may allocate, never per-frame creation.
    Vector<MeshSkinningGraphDispatchPlan, Core::Alloc::GlobalArena>& dispatchPlans = m_frameDispatchPlans;
    dispatchPlans.clear();
    auto skinningBindings = m_world.view<SkinnedMeshBindingComponent>();
    dispatchPlans.reserve(skinningBindings.candidateCount());
    bool declarationFailed = false;

    FramePlanBuilder planBuilder(*this, graph, scratchArena, dispatchPlans);
    skinningBindings.each([&](Core::ECS::EntityID entity, SkinnedMeshBindingComponent& binding){
        if(!declarationFailed && !planBuilder.declare(entity, binding))
            declarationFailed = true;
    }
    );
    Core::GpuTaskId terminalTask = planBuilder.terminalTask();

    if(declarationFailed)
        return false;
    if(dispatchPlans.empty())
        return true;

    const auto resourceUses = BuildMeshSkinningGraphResourceUses(dispatchPlans.data(), dispatchPlans.size(), scratchArena);
    if(!resourceUses)
        return false;

    // Retain prior-frame state with its consuming task; source stays serial by contract.
    const Core::GpuTaskExternalStateSource previousFrameStateSources[] = { Core::GpuTaskExternalStateSource{ .states = m_acceptedSkinningState.source(), }, };
    const usize previousFrameStateSourceCount = m_acceptedSkinningState.valid() ? LengthOf(previousFrameStateSources) : 0u;

    Core::GpuTimingSubmissionTicket timingTicket(m_graphics.gpuTiming());
    // Each graph payload owns the dispatch data it needs until submission acceptance.
    TaskGraphSkinningFinalizerTask::Payload finalizerPayload(m_arena, *this);
    finalizerPayload.plans.clear();
    finalizerPayload.plans.reserve(dispatchPlans.size());
    MeshSkinningLocalBoundsTask::Payload localBoundsPayload(m_arena, m_graphics, timingTicket);
    localBoundsPayload.dispatches.reserve(dispatchPlans.size());
    for(const MeshSkinningGraphDispatchPlan& plan : dispatchPlans){
        finalizerPayload.plans.push_back(plan);
        if(plan.updatesMeshletBounds){
            localBoundsPayload.dispatches.push_back({
                .bindlessResourceSlotsResource = plan.bindlessResourceSlotsResource,
                .meshletLocalBoundsResource = plan.meshletLocalBoundsResource,
                .localBoundsResource = plan.localBoundsResource,
                .localBoundsPipeline = plan.localBoundsPipeline,
                .meshletCount = plan.meshletCount,
                .bindlessResourceSlots = plan.bindlessResourceSlots,
            }
            );
        }
    }
    Core::GpuTaskId postDispatchDependency = terminalTask;
    if(!resourceUses->deformation.empty()){
        TaskGraphSkinningDeformationTask::Payload deformationPayload(m_arena, *this, timingTicket);
        deformationPayload.plans.clear();
        deformationPayload.plans.reserve(dispatchPlans.size());
        for(const MeshSkinningGraphDispatchPlan& plan : dispatchPlans){
            if(plan.hasActiveSkin)
                deformationPayload.plans.push_back(plan);
        }

        Core::GpuTaskDesc deformationDesc;
        deformationDesc
            .setIdentity(Name("mesh_skinning.frame_deformation"))
            .setMarkerLabel("Runtime Skinning Deformation")
            .setScheduling(MeshSkinningGraphDetail::SkinningDispatchScheduling())
            .setResourceUses(resourceUses->deformation.data(), resourceUses->deformation.size())
        ;
        if(previousFrameStateSourceCount != 0u)
            deformationDesc.setExternalStateSources(previousFrameStateSources, previousFrameStateSourceCount);
        if(terminalTask.valid())
            deformationDesc.setDependencies(&terminalTask, 1u);
        const auto deformationTaskResult = graph.addTask<TaskGraphSkinningDeformationTask>(deformationDesc, Move(deformationPayload));
        if(!deformationTaskResult){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to declare graph-owned skinning deformation"));
            return false;
        }
        const Core::GpuTaskId deformationTask = *deformationTaskResult;
        postDispatchDependency = deformationTask;
    }

    TaskGraphSkinningPostDispatchTask::Payload postDispatchPayload(m_arena, *this, timingTicket);
    postDispatchPayload.plans = Move(dispatchPlans);
    Core::GpuTaskDesc postDispatchDesc;
    postDispatchDesc
        .setIdentity(Name("mesh_skinning.frame_bounds_repack"))
        .setMarkerLabel("Runtime Skinning Bounds and Repack")
        .setScheduling(MeshSkinningGraphDetail::SkinningDispatchScheduling())
        .setResourceUses(resourceUses->postDispatch.data(), resourceUses->postDispatch.size())
    ;
    if(previousFrameStateSourceCount != 0u)
        postDispatchDesc.setExternalStateSources(previousFrameStateSources, previousFrameStateSourceCount);
    if(postDispatchDependency.valid())
        postDispatchDesc.setDependencies(&postDispatchDependency, 1u);
    const auto postDispatchTaskResult = graph.addTask<TaskGraphSkinningPostDispatchTask>(postDispatchDesc, Move(postDispatchPayload));
    if(!postDispatchTaskResult){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to declare graph-owned skinning bounds/repack"));
        return false;
    }
    const Core::GpuTaskId postDispatchTask = *postDispatchTaskResult;

    Core::GpuTaskDesc localBoundsDesc;
    localBoundsDesc
        .setIdentity(Name("mesh_skinning.frame_local_bounds"))
        .setMarkerLabel("Runtime Skinning Local Bounds")
        .setScheduling(MeshSkinningGraphDetail::SkinningDispatchScheduling())
        .setDependencies(&postDispatchTask, 1u)
        .setResourceUses(resourceUses->localBounds.data(), resourceUses->localBounds.size())
    ;
    if(previousFrameStateSourceCount != 0u)
        localBoundsDesc.setExternalStateSources(previousFrameStateSources, previousFrameStateSourceCount);
    const auto localBoundsTaskResult = graph.addTask<MeshSkinningLocalBoundsTask>(localBoundsDesc, Move(localBoundsPayload));
    if(!localBoundsTaskResult){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to declare graph-owned local bounds reduction"));
        return false;
    }
    const Core::GpuTaskId localBoundsTask = *localBoundsTaskResult;

    const Core::GpuTaskDesc finalizerDesc = Core::GpuTaskDesc{}
        .setIdentity(Name("mesh_skinning.frame_finalize_states"))
        .setMarkerLabel("Runtime Skinning Finalize States")
        .setScheduling(MeshSkinningGraphDetail::SkinningDispatchScheduling())
        .setDependencies(&localBoundsTask, 1u)
        .setResourceUses(resourceUses->finalizer.data(), resourceUses->finalizer.size())
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
    if(!scheduler.scheduleGraph(graph, analysis, assignments, compiledGraph, recordedGraph, transaction, scratchArena, compileOptions)){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to prepare graph-owned skinning work"));
        return false;
    }

    const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
    const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);
    if(!declarations.valid() || !compiledPlan.validFor(declarations)){
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
    collectLiveSkinningStateBuffers(liveBuffers, scratchArena);
    Core::GpuPersistentResourceStateCache::Candidate acceptedStateCandidate(m_acceptedSkinningState);
    struct SkinningStateContext{
        Core::GpuPersistentResourceStateCache& cache;
        Core::Alloc::ScratchArena& scratchArena;
        Core::GpuPersistentResourceStateCache::Candidate& candidate;
        const Core::BufferHandle* buffers = nullptr;
        usize bufferCount = 0u;
        bool preparationInvoked = false;
        bool statePrepared = false;
        bool stateAccepted = false;
    } skinningState{ .cache = m_acceptedSkinningState, .scratchArena = scratchArena, .candidate = acceptedStateCandidate, .buffers = liveBuffers.data(), .bufferCount = liveBuffers.size(), };
    const auto prepareSkinningState = [](void* const rawContext, const Core::CommandListResourceStateHandoff* const finalState) -> bool {
        SkinningStateContext* const context = static_cast<SkinningStateContext*>(rawContext);
        if(!context)
            return false;
        context->preparationInvoked = true;
        if(!finalState || (context->bufferCount != 0u && !context->buffers))
            return false;
        context->statePrepared = context->cache.buildMergedBufferSubset(context->candidate, *finalState, context->buffers, context->bufferCount, context->scratchArena);
        return context->statePrepared;
    };
    const Core::GpuTaskGraphTaskRecordedCallback recordedCallback{ .task = terminalTask, .context = &skinningState, .invoke = prepareSkinningState, };
    const auto acceptSkinningTask = [](void* const rawContext, const Core::QueueSubmissionToken& token)noexcept -> bool {
        static_cast<void>(token);
        SkinningStateContext* const context = static_cast<SkinningStateContext*>(rawContext);
        if(!context || !context->statePrepared)
            return false;
        context->stateAccepted = context->cache.commit(context->candidate);
        return context->stateAccepted;
    };
    const Core::GpuTaskGraphTaskAcceptedCallback acceptedCallback{ .task = terminalTask, .context = &skinningState, .invoke = acceptSkinningTask, };

    const Core::GpuTaskGraphTaskTimingTicket timingTickets[] = { Core::GpuTaskGraphTaskTimingTicket{ .task = terminalTask, .timingTicket = &timingTicket, }, };
    Core::GpuTaskGraphNormalExecutionDesc normalExecution;
    normalExecution.taskRecordedCallbacks = &recordedCallback;
    normalExecution.taskRecordedCallbackCount = 1u;
    normalExecution.taskTimingTickets = timingTickets;
    normalExecution.taskTimingTicketCount = LengthOf(timingTickets);
    normalExecution.taskAcceptedCallbacks = &acceptedCallback;
    normalExecution.taskAcceptedCallbackCount = 1u;
    const bool skinningGraphAccepted = scheduler.executeGraph(graph, compiledGraph, recordedGraph, normalExecution, transaction, nullptr, scratchArena);
    const Core::QueueSubmissionToken skinningToken = transaction.taskToken(compiledPlan, terminalTask);
    if(!skinningToken.valid()){
        if(!transaction.discardUnaccepted(graph, compiledGraph, recordedGraph.recordingAttemptGeneration()))
            m_graphics.requestDeviceRecreation();
        if(!skinningState.preparationInvoked)
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to record graph-owned skinning work"));
        else if(!skinningState.statePrepared)
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to prepare graph-owned skinning frame state"));
        else
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: graph-owned skinning submission was rejected"));
        return false;
    }
    if(!skinningGraphAccepted || !skinningState.stateAccepted){
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

void MeshSkinningSystem::pruneRuntimeResources(){
    if(m_runtimeResources.empty())
        return;

    for(auto it = m_runtimeResources.begin(); it != m_runtimeResources.end();){
        RuntimeResources& resources = it.value();
        const MeshSkinningRuntimeInstance* instance = m_runtimeMeshCache.findInstance(resources.handle);
        if(instance && instance->valid() && instance->editRevision == resources.editRevision){
            ++it;
            continue;
        }

        // Heap descriptors retain backing buffers; retire before erasing for quarantine.
        releaseRuntimeResourceBindlessHeapHandles(resources);
        it = m_runtimeResources.erase(it);
    }
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

