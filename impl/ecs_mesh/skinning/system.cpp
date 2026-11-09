// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "system.h"
#include "live_state_buffers.h"
#include "runtime_mesh_liveness.h"
#include "skin_payload.h"
#include "timing_names.h"

#include <core/common/log.h>
#include <core/ecs/world.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/gpu_timing.h>
#include <core/graphics/runtime/runtime.h>
#include <core/task/gpu/compiled_graph.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 MeshSkinningSystem::RuntimeResources::hasPersistentHeapDescriptors(const b8 hasActiveSkin, const b8 hasAttributeBuffer)const noexcept{
    const auto storageHandle = [this](const RuntimeStorageDescriptorSlot::Enum slot)noexcept{
        const Core::GpuDescriptorHandle handle = bindlessHeapHandles.storageBuffers[static_cast<usize>(slot)];
        return handle.valid() && handle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer;
    };
    const auto uniformHandle = [](const Core::GpuDescriptorHandle handle)noexcept{
        return handle.valid() && handle.descriptorClass() == Core::GpuDescriptorClass::UniformBuffer;
    };
    if(!bindlessResourceSlotsBuffer || !uniformHandle(bindlessHeapHandles.resourceSlots))
        return false;
    for(usize index = 0u; index < s_RuntimeRequiredStorageDescriptorCount; ++index)
        if(!storageHandle(static_cast<RuntimeStorageDescriptorSlot::Enum>(index)))
            return false;
    return (!hasActiveSkin || (storageHandle(RuntimeStorageDescriptorSlot::SkinInfluences) && storageHandle(RuntimeStorageDescriptorSlot::JointPalette))) && (!hasAttributeBuffer || storageHandle(RuntimeStorageDescriptorSlot::AttributeBuffer));
}

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
    , m_defaultSceneSubsystems(arena)
    , m_runtimeResources(0, Hasher<u64>(), EqualTo<u64>(), arena)
    , m_frameDispatchPlans(arena)
    , m_frameDeformationPlans(arena)
    , m_frameLocalBoundsDispatches(arena)
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

void MeshSkinningSystem::prepare([[maybe_unused]] Core::ECS::World& world){
    m_runtimeSetupReady = false;
    if(!ensureActiveSceneSubsystems())
        return;
    if(!isSkinningFeatureEnabled()){
        m_runtimeSetupReady = true;
        return;
    }
    if(!m_skinningComputePipeline || !m_boundsComputePipeline || !m_localBoundsComputePipeline || !m_repackComputePipeline)
        return;
    Core::Alloc::ScratchArena scratchArena(s_PrepareRuntimeArena);
    m_runtimeMeshCache.prepareResources(m_world, scratchArena);
    pruneRuntimeResources();
    b8 ready = true;
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
    });

    m_runtimeSetupReady = ready;
}

b8 MeshSkinningSystem::validateResources(const u32 width, const u32 height, [[maybe_unused]] const u32 sampleCount){
    if(width == 0 || height == 0)
        return true;
    if(!ensureActiveSceneSubsystems())
        return false;
    if(!isSkinningFeatureEnabled())
        return true;

    auto& device = m_graphics.getDevice();

    const b8 timingReady =
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

b8 MeshSkinningSystem::prepareResources([[maybe_unused]] Core::Framebuffer* framebuffer){
    Core::Alloc::ScratchArena scratchArena(s_PrepareRuntimeArena);
    // Frames may lack native submissions; filter retired buffers so handles pin nothing.
    if(m_acceptedSkinningState.valid() && !replaceAcceptedSkinningState(*m_acceptedSkinningState.source(), scratchArena)){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to prune the accepted skinning state handoff"));
        return false;
    }

    return m_runtimeSetupReady;
}

void MeshSkinningSystem::render([[maybe_unused]] Core::Framebuffer* framebuffer){
    if(!isSkinningFeatureEnabled())
        return;

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
    return BuildSkinnedRuntimeMeshDesc(entity, runtimeMesh, instance, s_RuntimeSkinningMeshletFrustumCullingEnabled, s_RuntimeSkinningMeshletConeCullingEnabled);
}

void MeshSkinningSystem::markLiveRuntimeMeshes(RuntimeMeshRequestSet& requests){
    if(requests.complete())
        return;
    for(auto&& [entity, binding] : m_world.view<SkinnedMeshBindingComponent>()){
        const MeshSkinningRuntimeInstance* const instance = binding.runtimeMesh.valid() ? m_runtimeMeshCache.findInstance(binding.runtimeMesh) : nullptr;
        if(const auto identity = ResolveSkinnedRuntimeMeshIdentity(entity, binding.runtimeMesh, instance))
            requests.markLive(identity->meshKey, identity->version);
        if(requests.complete())
            return;
    }
}

b8 MeshSkinningSystem::ensureActiveSceneSubsystems(){
    if(m_activeSceneSubsystems)
        return true;
    if(!m_defaultSceneSubsystems.loadProjectStartScene(m_assetManager))
        return false;
    m_activeSceneSubsystems = &m_defaultSceneSubsystems;
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

void MeshSkinningSystem::collectLiveSkinningStateBuffers(Core::Alloc::ScratchArena& scratchArena, Vector<Core::BufferHandle, Core::Alloc::GlobalArena>& outBuffers)const{
    MeshSkinningStateBufferCollector collector(scratchArena, outBuffers);
    m_world.view<SkinnedMeshBindingComponent>().each([&](Core::ECS::EntityID, const SkinnedMeshBindingComponent& binding){
        if(!binding.runtimeMesh.valid())
            return;

        const MeshSkinningRuntimeInstance* const instance = m_runtimeMeshCache.findInstance(binding.runtimeMesh);
        if(!instance)
            return;

        MeshSkinningStateBufferResources resources;
        const auto foundResources = m_runtimeResources.find(instance->handle.value);
        if(foundResources != m_runtimeResources.end()){
            resources = {
                .editRevision = foundResources.value().editRevision,
                .skinBuffer = &foundResources.value().skinBuffer,
                .jointPaletteBuffer = &foundResources.value().jointPaletteBuffer,
                .bindlessResourceSlotsBuffer = &foundResources.value().bindlessResourceSlotsBuffer,
            };
        }
        collector.collect(*instance, resources);
    });
}

b8 MeshSkinningSystem::replaceAcceptedSkinningState(const Core::CommandListResourceStateHandoff& state, Core::Alloc::ScratchArena& scratchArena){
    Vector<Core::BufferHandle, Core::Alloc::GlobalArena>& liveBuffers = m_frameLiveBuffers;
    ScopeExit releaseLiveBuffers([&]()noexcept{ liveBuffers.clear(); });

    collectLiveSkinningStateBuffers(scratchArena, liveBuffers);
    return m_acceptedSkinningState.replaceBufferSubset(state, Span<const Core::BufferHandle>(liveBuffers), scratchArena);
}

b8 MeshSkinningSystem::prepareRuntimeMeshResources(MeshSkinningRuntimeInstance& instance, Core::Alloc::ScratchArena& scratchArena){
    instance.deformationState.invalidateCurrent();
    if(instance.meshlets.empty())
        return true;
    if(!skinningPipelineReady() || !boundsPipelinesReady() || (instance.attributeBuffer && !repackPipelineReady()))
        return false;

    const auto resourcesChanged = ensureRuntimeResources(instance, scratchArena);
    if(!resourcesChanged)
        return false;
    if(*resourcesChanged)
        instance.dirtyFlags |= RuntimeMeshDirtyFlag::SkinningInputDirty | RuntimeMeshDirtyFlag::MeshletBoundsDirty;
    return true;
}

b8 MeshSkinningSystem::recordGraphOwnedSkinningDeformation(const MeshSkinningGraphDispatchPlan& plan, const Core::GpuTaskRecordContext& context, Core::CommandList& commandList){
    if(!plan.hasActiveSkin)
        return false;

    const auto resolveBuffer = [&](const Core::GpuGraphResourceId resource)noexcept{
        return context.declarations.bufferForResource(resource);
    };

    Core::Buffer* const bindlessResourceSlots = resolveBuffer(plan.bindlessResourceSlotsResource);
    Core::Buffer* const skinnedPosition = resolveBuffer(plan.skinnedPositionResource);
    Core::Buffer* const restPosition = resolveBuffer(plan.restPositionResource);
    Core::Buffer* const restNormal = resolveBuffer(plan.restNormalResource);
    Core::Buffer* const restTangent = resolveBuffer(plan.restTangentResource);
    Core::Buffer* const skinnedNormal = resolveBuffer(plan.skinnedNormalResource);
    Core::Buffer* const skinnedTangent = resolveBuffer(plan.skinnedTangentResource);
    Core::Buffer* const meshletDesc = resolveBuffer(plan.meshletDescResource);
    Core::Buffer* const meshletPositionRefDeltas = resolveBuffer(plan.meshletPositionRefDeltaResource);
    Core::Buffer* const meshletAttributeRefDeltas = resolveBuffer(plan.meshletAttributeRefDeltaResource);
    Core::Buffer* const attributeSkins = resolveBuffer(plan.attributeSkinResource);
    Core::Buffer* const skinInfluences = resolveBuffer(plan.skinResource);
    Core::Buffer* const jointPalette = resolveBuffer(plan.jointPaletteResource);
    Core::ComputePipeline* const skinningPipeline = context.declarations.computePipelineFor(plan.skinningPipeline);
    if(
        !bindlessResourceSlots
        || !skinnedPosition
        || !restPosition
        || !restNormal
        || !restTangent
        || !skinnedNormal
        || !skinnedTangent
        || !meshletDesc
        || !meshletPositionRefDeltas
        || !meshletAttributeRefDeltas
        || !attributeSkins
        || !skinInfluences
        || !jointPalette
        || !skinningPipeline
    )
        return false;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized())
        return false;

    // Graph establishes inputs and declares UAV outputs; callback records dispatch only.
    Core::ComputeState computeState;
    computeState.setPipeline(skinningPipeline);
    commandList.setComputeState(computeState);
    heap.bindCompute(commandList, *skinningPipeline);

    const MeshSkinningPushConstants pushConstants = MakeMeshSkinningPushConstants(plan.meshletCount, plan.skinCount, plan.jointCount, plan.skinningMode, plan.bindlessResourceSlots);
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
    {
        Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), MeshSkinningGpuTimingScope::s_Skinning, device, commandList);

        commandList.dispatch(pushConstants.meshletCount, 1u, 1u);
    }

    return true;
}

b8 MeshSkinningSystem::recordGraphOwnedSkinningPostDispatch(const MeshSkinningGraphDispatchPlan& plan, const Core::GpuTaskRecordContext& context, Core::CommandList& commandList){
    if(!plan.updatesMeshletBounds)
        return false;

    const auto resolveBuffer = [&](const Core::GpuGraphResourceId resource)noexcept{
        return context.declarations.bufferForResource(resource);
    };

    Core::Buffer* const bindlessResourceSlots = resolveBuffer(plan.bindlessResourceSlotsResource);
    Core::Buffer* const skinnedPosition = resolveBuffer(plan.skinnedPositionResource);
    Core::Buffer* const meshletDesc = resolveBuffer(plan.meshletDescResource);
    Core::Buffer* const meshletPositionRefDeltas = resolveBuffer(plan.meshletPositionRefDeltaResource);
    Core::Buffer* const meshletLocalVertexRefs = resolveBuffer(plan.meshletLocalVertexRefResource);
    Core::Buffer* const meshletPrimitiveIndices = resolveBuffer(plan.meshletPrimitiveIndexResource);
    Core::Buffer* const meshletBounds = resolveBuffer(plan.meshletBoundsResource);
    Core::Buffer* const meshletLocalBounds = resolveBuffer(plan.meshletLocalBoundsResource);
    Core::ComputePipeline* const boundsPipeline = context.declarations.computePipelineFor(plan.boundsPipeline);
    if(!bindlessResourceSlots || !skinnedPosition || !meshletDesc || !meshletPositionRefDeltas || !meshletLocalVertexRefs || !meshletPrimitiveIndices || !meshletBounds || !meshletLocalBounds || !boundsPipeline)
        return false;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized())
        return false;

    // Bounds consumes declared skinned positions and writes declared bounds output.
    Core::ComputeState computeState;
    computeState.setPipeline(boundsPipeline);
    commandList.setComputeState(computeState);
    heap.bindCompute(commandList, *boundsPipeline);

    const MeshletBoundsPushConstants boundsPushConstants = MakeMeshletBoundsPushConstants(plan.meshletCount, plan.bindlessResourceSlots);
    commandList.setPushConstants(&boundsPushConstants, sizeof(boundsPushConstants));
    {
        Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), MeshSkinningGpuTimingScope::s_MeshletBounds, device, commandList);

        commandList.dispatch(boundsPushConstants.meshletCount, 1u, 1u);
    }

    if(plan.repacksNormals){
        Core::Buffer* const skinnedNormal = resolveBuffer(plan.skinnedNormalResource);
        Core::Buffer* const meshletAttributeRefDeltas = resolveBuffer(plan.meshletAttributeRefDeltaResource);
        Core::Buffer* const attributeBuffer = resolveBuffer(plan.attributeResource);
        Core::ComputePipeline* const repackPipeline = context.declarations.computePipelineFor(plan.repackPipeline);
        if(!skinnedNormal || !meshletAttributeRefDeltas || !attributeBuffer || !repackPipeline)
            return false;

        // Graph lowers the handoff before repack; publishes packed attributes as output.
        computeState.setPipeline(repackPipeline);
        commandList.setComputeState(computeState);
        heap.bindCompute(commandList, *repackPipeline);

        commandList.setPushConstants(&boundsPushConstants, sizeof(boundsPushConstants));
        {
            Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), MeshSkinningGpuTimingScope::s_RepackNormals, device, commandList);

            commandList.dispatch(boundsPushConstants.meshletCount, 1u, 1u);
        }
    }
    return true;
}

void MeshSkinningSystem::confirmGraphOwnedSkinningDispatch(const MeshSkinningGraphDispatchPlan& plan)noexcept{
    MeshSkinningRuntimeInstance* const instance = m_runtimeMeshCache.findInstance(plan.handle);
    if(!instance || instance->editRevision != plan.submissionCommit.editRevision)
        return;

    const auto foundResources = m_runtimeResources.find(plan.handle.value);
    if(foundResources == m_runtimeResources.end())
        return;
    RuntimeResources& resources = foundResources.value();
    if(
        !plan.updatesMeshletBounds || !plan.localBoundsResource.valid() || !plan.meshletLocalBoundsResource.valid()
        || resources.editRevision != plan.submissionCommit.editRevision
        || resources.buffers != CaptureMeshSkinningResourceBuffers(*instance)
        || resources.bindlessResourceSlotsBuffer.get() != plan.bindlessResourceSlotsBuffer.get()
        || resources.bindlessHeapHandles.resourceSlots != plan.bindlessResourceSlotsDescriptor
        || resources.bindlessResourceSlots != plan.bindlessResourceSlotsPayload
    )
        return;

    if(!instance->deformationState.accept(plan.deformationCandidate))
        return;

    ApplyMeshSkinningSubmissionCommit(true, instance->editRevision, plan.submissionCommit, instance->dirtyFlags, resources.bindlessResourceSlotsUploaded);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

