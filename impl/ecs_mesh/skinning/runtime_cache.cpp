// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "runtime_cache.h"

#include <impl/assets_mesh/asset.h>
#include <impl/assets_mesh/skin_asset.h>

#include <core/common/log.h>
#include <core/ecs/world.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const Mesh& MeshSkinningRuntimeCache::MeshSkinningSource::mesh()const noexcept{
    const auto asset = MakeNotNull(Core::Assets::CastAsset<Mesh>(meshAsset.get()));
    return *asset;
}

const Skin& MeshSkinningRuntimeCache::MeshSkinningSource::skin()const noexcept{
    const auto asset = MakeNotNull(Core::Assets::CastAsset<Skin>(skinAsset.get()));
    return *asset;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void MeshSkinningRuntimeCache::prepareResources(Core::ECS::World& world, Core::Alloc::ScratchArena& scratchArena){
    auto skinningBindingView = world.view<SkinnedMeshBindingComponent>();
    const usize rendererCandidateCount = skinningBindingView.candidateCount();
    if(rendererCandidateCount == 0u){
        clear();
        return;
    }

    m_instances.reserve(rendererCandidateCount);
    m_handleToEntity.reserve(rendererCandidateCount);
    m_sources.reserve(rendererCandidateCount);

    const b8 pruneStaleInstances = !m_instances.empty();
    skinningBindingView.each([&](Core::ECS::EntityID entity, SkinnedMeshBindingComponent& component){
        if(!ensureRuntimeMesh(entity, scratchArena, component))
            component.runtimeMesh.reset();
    });

    if(!pruneStaleInstances)
        return;

    for(auto it = m_instances.begin(); it != m_instances.end();){
        const Core::ECS::EntityID entity = it->first;
        if(world.tryGetComponent<SkinnedMeshBindingComponent>(entity)){
            ++it;
            continue;
        }

        it = eraseInstance(it);
    }
}

void MeshSkinningRuntimeCache::clear()noexcept{
    m_handleToEntity.clear();
    m_instances.clear();
    m_sources.clear();
}

MeshSkinningRuntimeInstance* MeshSkinningRuntimeCache::findInstance(const RuntimeMeshHandle handle)noexcept{
    return const_cast<MeshSkinningRuntimeInstance*>(static_cast<const MeshSkinningRuntimeCache&>(*this).findInstance(handle));
}

const MeshSkinningRuntimeInstance* MeshSkinningRuntimeCache::findInstance(const RuntimeMeshHandle handle)const noexcept{
    const auto foundEntity = m_handleToEntity.find(handle.value);
    if(foundEntity == m_handleToEntity.end() || !foundEntity.value().valid())
        return nullptr;

    const auto foundInstance = m_instances.find(foundEntity.value());
    if(foundInstance == m_instances.end())
        return nullptr;
    return &foundInstance.value();
}

Expected<RuntimeMeshHandle> MeshSkinningRuntimeCache::allocateHandle(){
    while(m_nextHandleValue != 0u){
        RuntimeMeshHandle handle;
        handle.value = m_nextHandleValue;
        ++m_nextHandleValue;
        if(m_handleToEntity.find(handle.value) == m_handleToEntity.end())
            return handle;
    }

    NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: runtime mesh handle space exhausted"));
    return MakeUnexpected(Failure{});
}

void MeshSkinningRuntimeCache::releaseRuntimeMesh(const Core::ECS::EntityID entity)noexcept{
    const auto foundInstance = m_instances.find(entity);
    if(foundInstance == m_instances.end())
        return;

    eraseInstance(foundInstance);
}

MeshSkinningRuntimeCache::InstanceMap::iterator MeshSkinningRuntimeCache::eraseInstance(const InstanceMap::iterator it)noexcept{
    const MeshSkinningRuntimeInstance& instance = it.value();
    m_handleToEntity.erase(instance.handle.value);
    releaseSource(instance.sourceName);
    return m_instances.erase(it);
}

void MeshSkinningRuntimeCache::releaseSource(const Name& sourceName)noexcept{
    const auto foundSource = m_sources.find(sourceName);
    if(foundSource == m_sources.end())
        return;

    MeshSkinningSource& source = foundSource.value();
    if(source.referenceCount > 0u)
        --source.referenceCount;
    if(source.referenceCount == 0u)
        m_sources.erase(foundSource);
}

void MeshSkinningRuntimeCache::eraseUnusedSource(const Name& sourceName)noexcept{
    const auto foundSource = m_sources.find(sourceName);
    if(foundSource != m_sources.end() && foundSource.value().referenceCount == 0u)
        m_sources.erase(foundSource);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

