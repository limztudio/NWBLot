// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "system.h"

#include <impl/ecs_mesh/skinning/submission_state.adhd.h>
#include "runtime_mesh_liveness.h"
#include "timing_names.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <core/ecs/world.h>
#include <core/graphics/gpu_timing.h>
#include <core/graphics/runtime/runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skinning_system{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr b8 s_RuntimeSkinningMeshletFrustumCullingEnabled = true;
static constexpr b8 s_RuntimeSkinningMeshletConeCullingEnabled = false; // Deformations make cones unsafe.


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

void MeshSkinningSystem::prepare(Core::ECS::World& world){
    static_cast<void>(world);
    m_runtimeSetupReady = false;
    if(!m_activeSceneSubsystems){
        if(!m_defaultSceneSubsystems.loadProjectStartScene(m_assetManager))
            return;
        m_activeSceneSubsystems = &m_defaultSceneSubsystems;
    }
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

Expected<void, Core::CpuTaskAdmissionError::Enum> MeshSkinningSystem::update(Core::ECS::World& world, const f32 delta)noexcept{
    static_cast<void>(world);
    static_cast<void>(delta);
    return {};
}

b8 MeshSkinningSystem::validateResources(const u32 width, const u32 height, const u32 sampleCount){
    static_cast<void>(sampleCount);
    if(width == 0 || height == 0)
        return true;
    if(!m_activeSceneSubsystems){
        if(!m_defaultSceneSubsystems.loadProjectStartScene(m_assetManager))
            return false;
        m_activeSceneSubsystems = &m_defaultSceneSubsystems;
    }
    if(!isSkinningFeatureEnabled())
        return true;

    auto& device = m_graphics.getDevice();

    constexpr u32 s_PerRuntimeMeshTimingQueries = 128u;
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

b8 MeshSkinningSystem::prepareResources(Core::Framebuffer* framebuffer){
    static_cast<void>(framebuffer);
    Core::Alloc::ScratchArena scratchArena(s_PrepareRuntimeArena);
    // Frames may lack native submissions; filter retired buffers so handles pin nothing.
    if(m_acceptedSkinningState.valid() && !replaceAcceptedSkinningState(*m_acceptedSkinningState.source(), scratchArena)){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to prune the accepted skinning state handoff"));
        return false;
    }

    return m_runtimeSetupReady;
}

void MeshSkinningSystem::render(Core::Framebuffer* framebuffer){
    static_cast<void>(framebuffer);
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
    return BuildSkinnedRuntimeMeshDesc(entity, runtimeMesh, instance, __hidden_skinning_system::s_RuntimeSkinningMeshletFrustumCullingEnabled, __hidden_skinning_system::s_RuntimeSkinningMeshletConeCullingEnabled);
}

b8 MeshSkinningSystem::hasRuntimeMeshBinding(const Core::ECS::EntityID entity)const noexcept{
    return m_world.tryGetComponent<SkinnedMeshBindingComponent>(entity) != nullptr;
}

void MeshSkinningSystem::markLiveRuntimeMeshes(RuntimeMeshRequestSet& requests){
    MarkLiveSkinnedRuntimeMeshes(m_world, [this](const Core::ECS::EntityID entity, const SkinnedMeshBindingComponent& binding){
        const MeshSkinningRuntimeInstance* const instance = binding.runtimeMesh.valid() ? m_runtimeMeshCache.findInstance(binding.runtimeMesh) : nullptr;
        return ResolveSkinnedRuntimeMeshIdentity(entity, binding.runtimeMesh, instance);
    }, requests);
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

