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


MeshSkinningRuntimeCache::MeshSkinningRuntimeCache(Core::Alloc::GlobalArena& arena, Core::GraphicsRuntime& graphics, Core::Assets::AssetManager& assetManager)
    : m_arena(arena)
    , m_graphics(graphics)
    , m_assetManager(assetManager)
    , m_sources(0, Hasher<Name>(), EqualTo<Name>(), arena)
    , m_instances(0, Hasher<Core::ECS::EntityID>(), EqualTo<Core::ECS::EntityID>(), arena)
    , m_handleToEntity(0, Hasher<u64>(), EqualTo<u64>(), arena)
{}


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

    const bool pruneStaleInstances = !m_instances.empty();
    skinningBindingView.each([&](Core::ECS::EntityID entity, SkinnedMeshBindingComponent& component){
        if(!ensureRuntimeMesh(entity, component, scratchArena))
            component.runtimeMesh.reset();
    }
    );

    if(!pruneStaleInstances)
        return;

    for(auto it = m_instances.begin(); it != m_instances.end();){
        const Core::ECS::EntityID entity = it->first;
        if(world.tryGetComponent<SkinnedMeshBindingComponent>(entity)){
            ++it;
            continue;
        }

        const MeshSkinningRuntimeInstance& instance = it.value();
        m_handleToEntity.erase(instance.handle.value);
        releaseSource(instance.sourceName);
        it = m_instances.erase(it);
    }
}

void MeshSkinningRuntimeCache::clear()noexcept{
    m_handleToEntity.clear();
    m_instances.clear();
    m_sources.clear();
}

MeshSkinningRuntimeInstance* MeshSkinningRuntimeCache::findInstance(const RuntimeMeshHandle handle)noexcept{
    return findInstanceByEntity(entityForHandle(handle));
}

const MeshSkinningRuntimeInstance* MeshSkinningRuntimeCache::findInstance(const RuntimeMeshHandle handle)const noexcept{
    return findInstanceByEntity(entityForHandle(handle));
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

    const MeshSkinningRuntimeInstance& instance = foundInstance.value();
    m_handleToEntity.erase(instance.handle.value);
    releaseSource(instance.sourceName);
    m_instances.erase(foundInstance);
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

Core::ECS::EntityID MeshSkinningRuntimeCache::entityForHandle(const RuntimeMeshHandle handle)const noexcept{
    const auto foundEntity = m_handleToEntity.find(handle.value);
    if(foundEntity == m_handleToEntity.end())
        return Core::ECS::s_InvalidEntityId;
    return foundEntity.value();
}

MeshSkinningRuntimeInstance* MeshSkinningRuntimeCache::findInstanceByEntity(const Core::ECS::EntityID entity)noexcept{
    if(!entity.valid())
        return nullptr;

    const auto foundInstance = m_instances.find(entity);
    if(foundInstance == m_instances.end())
        return nullptr;
    return &foundInstance.value();
}

const MeshSkinningRuntimeInstance* MeshSkinningRuntimeCache::findInstanceByEntity(const Core::ECS::EntityID entity)const noexcept{
    if(!entity.valid())
        return nullptr;

    const auto foundInstance = m_instances.find(entity);
    if(foundInstance == m_instances.end())
        return nullptr;
    return &foundInstance.value();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

