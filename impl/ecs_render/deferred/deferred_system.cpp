// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deferred_system.h"

#include <impl/ecs_render/shared/renderer_state.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RendererDeferredSystem::RendererDeferredSystem(
    Core::Alloc::GlobalArena& arena,
    Core::ECS::World& world,
    Core::Graphics& graphics,
    RendererDeferredState& deferredState,
    RendererRayTracingState& rayTracingState,
    RendererShaderSystem& shaderSystem
)
    : m_arena(arena)
    , m_world(world)
    , m_graphics(graphics)
    , m_deferredState(deferredState)
    , m_rayTracingState(rayTracingState)
    , m_shaderSystem(shaderSystem)
{}


bool RendererDeferredSystem::frameTargetsMatch(const u32 width, const u32 height)const noexcept{
    const DeferredFrameTargets& targets = m_deferredState.m_targets;
    return targets.valid() && targets.width == width && targets.height == height;
}

DeferredFrameTargets* RendererDeferredSystem::tryFrameTargets()noexcept{
    return m_deferredState.m_targets.valid() ? &m_deferredState.m_targets : nullptr;
}

const DeferredFrameTargets* RendererDeferredSystem::tryFrameTargets()const noexcept{
    return m_deferredState.m_targets.valid() ? &m_deferredState.m_targets : nullptr;
}

DeferredLightingGraphResources RendererDeferredSystem::lightingGraphResources()const noexcept{
    return {
        m_deferredState.m_sceneShadingBuffer,
        m_deferredState.m_lightBuffer,
    };
}

void RendererDeferredSystem::invalidateSceneLightingUploadMirrors()noexcept{
    m_deferredState.m_sceneShadingGpuDataValid = false;
    m_deferredState.m_lightGpuDataValid = false;
}

void RendererDeferredSystem::invalidateResources(){
    m_deferredState.invalidateResources();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

