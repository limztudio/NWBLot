// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_material_state.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RendererMaterialState::RendererMaterialState(Core::Alloc::GlobalArena& arena)
    : m_surfaceInfos(0, Hasher<Name>(), EqualTo<Name>(), arena)
    , m_resourceState(arena)
    , m_pipelines(0, MaterialPipelineKeyHasher(), MaterialPipelineKeyEqualTo(), arena)
    , m_instanceMutableCache(0, Hasher<Core::ECS::EntityID>(), EqualTo<Core::ECS::EntityID>(), arena)
    , m_loggedMaterialPaths(0, Hasher<Name>(), EqualTo<Name>(), arena)
{}


void RendererMaterialState::invalidateResources(){
    m_pipelines.clear();
    m_materialPassBindingLayout.reset();
    m_computeBindingLayout.reset();
    m_instanceBuffer.reset();
    m_materialTypedBuffer.reset();
    m_emulationVertexShader.reset();
    m_emulationInputLayout.reset();
    m_instanceMutableCache.clear();
    m_loggedMaterialPaths.clear();
    m_instanceBufferCapacity = 0u;
    m_materialTypedBufferCapacity = 0u;
    m_instanceMutableCacheComponentMutationVersion = 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

