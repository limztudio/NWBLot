// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deferred_system.h"

#include <impl/ecs_render/deferred/renderer_deferred_state.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RendererDeferredSystem::RendererDeferredSystem(
    Core::Alloc::GlobalArena& arena,
    Core::ECS::World& world,
    Core::Graphics& graphics,
    RendererDeferredState& deferredState,
    RendererShaderSystem& shaderSystem
)
    : m_arena(arena)
    , m_world(world)
    , m_graphics(graphics)
    , m_deferredState(deferredState)
    , m_shaderSystem(shaderSystem)
{}


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

