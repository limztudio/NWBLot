// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "material_system.h"

#include <impl/ecs_render/material/renderer_material_state.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RendererMaterialSystem::RendererMaterialSystem(
    Core::Alloc::GlobalArena& arena,
    Core::ECS::World& world,
    Core::Graphics& graphics,
    Core::Assets::AssetManager& assetManager,
    CsgShapeRegistry& csgShapeRegistry,
    RendererMaterialState& materialState,
    RendererDrawState& drawState,
    RendererShaderSystem& shaderSystem,
    RendererMeshSystem& meshSystem,
    RendererCsgSystem& csgSystem
)
    : m_arena(arena)
    , m_world(world)
    , m_graphics(graphics)
    , m_assetManager(assetManager)
    , m_csgShapeRegistry(csgShapeRegistry)
    , m_materialState(materialState)
    , m_drawState(drawState)
    , m_shaderSystem(shaderSystem)
    , m_meshSystem(meshSystem)
    , m_csgSystem(csgSystem)
{}


void RendererMaterialSystem::invalidateResources(){
    releaseMaterialResourceReferences();
    m_materialState.invalidateResources();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

