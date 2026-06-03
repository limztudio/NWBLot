// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "system.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RendererSystemSubsystemBase::RendererSystemSubsystemBase(RendererSystem& renderer)
    : m_renderer(renderer)
    , m_arena(renderer.m_arena)
    , m_world(renderer.m_world)
    , m_graphics(renderer.m_graphics)
    , m_assetManager(renderer.m_assetManager)
    , m_shaderPathResolver(renderer.m_shaderPathResolver)
    , m_csgShapeRegistry(renderer.m_csgShapeRegistry)
    , m_meshState(renderer.m_meshState)
    , m_materialState(renderer.m_materialState)
    , m_drawState(renderer.m_drawState)
    , m_csgState(renderer.m_csgState)
    , m_deferredState(renderer.m_deferredState)
    , m_avboitState(renderer.m_avboitState)
{}

RendererShaderSystem& RendererSystemSubsystemBase::shaderSystem()noexcept{
    return m_renderer.m_shaderSystem;
}

const RendererShaderSystem& RendererSystemSubsystemBase::shaderSystem()const noexcept{
    return m_renderer.m_shaderSystem;
}

RendererMeshSystem& RendererSystemSubsystemBase::meshSystem()noexcept{
    return m_renderer.m_meshSystem;
}

const RendererMeshSystem& RendererSystemSubsystemBase::meshSystem()const noexcept{
    return m_renderer.m_meshSystem;
}

RendererMaterialSystem& RendererSystemSubsystemBase::materialSystem()noexcept{
    return m_renderer.m_materialSystem;
}

const RendererMaterialSystem& RendererSystemSubsystemBase::materialSystem()const noexcept{
    return m_renderer.m_materialSystem;
}

RendererCsgSystem& RendererSystemSubsystemBase::csgSystem()noexcept{
    return m_renderer.m_csgSystem;
}

const RendererCsgSystem& RendererSystemSubsystemBase::csgSystem()const noexcept{
    return m_renderer.m_csgSystem;
}

RendererDeferredSystem& RendererSystemSubsystemBase::deferredSystem()noexcept{
    return m_renderer.m_deferredSystem;
}

const RendererDeferredSystem& RendererSystemSubsystemBase::deferredSystem()const noexcept{
    return m_renderer.m_deferredSystem;
}

RendererAvboitSystem& RendererSystemSubsystemBase::avboitSystem()noexcept{
    return m_renderer.m_avboitSystem;
}

const RendererAvboitSystem& RendererSystemSubsystemBase::avboitSystem()const noexcept{
    return m_renderer.m_avboitSystem;
}

RendererShaderSystem::RendererShaderSystem(RendererSystem& renderer)
    : RendererSystemSubsystemBase(renderer)
{}

RendererMeshSystem::RendererMeshSystem(RendererSystem& renderer)
    : RendererSystemSubsystemBase(renderer)
{}

RendererMaterialSystem::RendererMaterialSystem(RendererSystem& renderer)
    : RendererSystemSubsystemBase(renderer)
{}

RendererCsgSystem::RendererCsgSystem(RendererSystem& renderer)
    : RendererSystemSubsystemBase(renderer)
{}

RendererDeferredSystem::RendererDeferredSystem(RendererSystem& renderer)
    : RendererSystemSubsystemBase(renderer)
{}

RendererAvboitSystem::RendererAvboitSystem(RendererSystem& renderer)
    : RendererSystemSubsystemBase(renderer)
{}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
