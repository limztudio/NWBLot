// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "avboit_system.h"

#include <impl/ecs_render/shared/renderer_state.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RendererAvboitSystem::RendererAvboitSystem(
    Core::Alloc::GlobalArena& arena,
    Core::Graphics& graphics,
    RendererAvboitState& avboitState,
    RendererShaderSystem& shaderSystem,
    RendererMaterialSystem& materialSystem,
    RendererCsgSystem& csgSystem
)
    : m_arena(arena)
    , m_graphics(graphics)
    , m_avboitState(avboitState)
    , m_shaderSystem(shaderSystem)
    , m_materialSystem(materialSystem)
    , m_csgSystem(csgSystem)
{}


bool RendererAvboitSystem::shouldClearTargets(const bool hasTransparentRenderers)const noexcept{
    return hasTransparentRenderers || m_avboitState.m_targetsNeedClear;
}

bool RendererAvboitSystem::captureTargetClearState()const noexcept{
    return m_avboitState.m_targetsNeedClear;
}

void RendererAvboitSystem::restoreTargetClearState(const bool targetsNeedClear)noexcept{
    m_avboitState.m_targetsNeedClear = targetsNeedClear;
}

void RendererAvboitSystem::markFrameTargetUsage(const bool hasTransparentRenderers)noexcept{
    m_avboitState.m_targetsNeedClear = hasTransparentRenderers;
}

void RendererAvboitSystem::invalidateResources(){
    m_avboitState.invalidateResources();
}


void RendererAvboitSystem::resetTaskGraphStage()noexcept{
    m_taskGraphStage.reset();
}


Core::Sampler& RendererAvboitSystem::linearSampler()const noexcept{
    NWB_ASSERT(m_avboitState.m_linearSampler);
    return *m_avboitState.m_linearSampler;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

