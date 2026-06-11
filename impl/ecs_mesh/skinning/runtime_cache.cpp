// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "runtime_cache.h"

#include <core/common/log.h>
#include <core/ecs/world.h>
#include <impl/assets_mesh/asset.h>
#include <impl/assets_mesh/skin_asset.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_runtime_cache{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr RuntimeMeshDirtyFlags s_KnownDirtyFlags = RuntimeMeshDirtyFlag::All;

[[nodiscard]] RuntimeMeshDirtyFlags SanitizeDirtyFlags(const RuntimeMeshDirtyFlags dirtyFlags){
    return static_cast<RuntimeMeshDirtyFlags>(dirtyFlags & s_KnownDirtyFlags);
}

[[nodiscard]] RuntimeMeshDirtyFlags ExpandDirtyFlags(const RuntimeMeshDirtyFlags dirtyFlags){
    RuntimeMeshDirtyFlags expanded = SanitizeDirtyFlags(dirtyFlags);
    if((expanded & (RuntimeMeshDirtyFlag::TopologyDirty | RuntimeMeshDirtyFlag::AttributesDirty)) != 0u){
        expanded = static_cast<RuntimeMeshDirtyFlags>(
            expanded
            | RuntimeMeshDirtyFlag::GpuUploadDirty
            | RuntimeMeshDirtyFlag::SkinnedMeshInputDirty
            | RuntimeMeshDirtyFlag::MeshletBoundsDirty
        );
    }
    else if((expanded & RuntimeMeshDirtyFlag::GpuUploadDirty) != 0u){
        expanded = static_cast<RuntimeMeshDirtyFlags>(
            expanded | RuntimeMeshDirtyFlag::SkinnedMeshInputDirty | RuntimeMeshDirtyFlag::MeshletBoundsDirty
        );
    }
    return expanded;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const Mesh* SkinnedMeshRuntimeMeshCache::SkinnedMeshSource::mesh()const{
    if(!meshAsset || meshAsset->assetType() != Mesh::AssetTypeName())
        return nullptr;
    return checked_cast<const Mesh*>(meshAsset.get());
}

const Skin* SkinnedMeshRuntimeMeshCache::SkinnedMeshSource::skin()const{
    if(!skinAsset || skinAsset->assetType() != Skin::AssetTypeName())
        return nullptr;
    return checked_cast<const Skin*>(skinAsset.get());
}


SkinnedMeshRuntimeMeshCache::SkinnedMeshRuntimeMeshCache(Core::Alloc::GlobalArena& arena, Core::Graphics& graphics, Core::Assets::AssetManager& assetManager)
    : m_arena(arena)
    , m_graphics(graphics)
    , m_assetManager(assetManager)
    , m_sources(0, Hasher<Name>(), EqualTo<Name>(), arena)
    , m_instances(0, Hasher<Core::ECS::EntityID>(), EqualTo<Core::ECS::EntityID>(), arena)
    , m_handleToEntity(0, Hasher<u64>(), EqualTo<u64>(), arena)
{}


void SkinnedMeshRuntimeMeshCache::prepareResources(Core::ECS::World& world){
    auto skinnedMeshBindingView = world.view<SkinnedMeshBindingComponent>();
    const usize rendererCandidateCount = skinnedMeshBindingView.candidateCount();
    if(rendererCandidateCount == 0u){
        clear();
        return;
    }

    m_instances.reserve(rendererCandidateCount);
    m_handleToEntity.reserve(rendererCandidateCount);
    m_sources.reserve(rendererCandidateCount);

    const bool pruneStaleInstances = !m_instances.empty();
    skinnedMeshBindingView.each(
        [&](Core::ECS::EntityID entity, SkinnedMeshBindingComponent& component){
            if(!ensureRuntimeMesh(entity, component))
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

        const SkinnedMeshRuntimeMeshInstance& instance = it.value();
        m_handleToEntity.erase(instance.handle.value);
        releaseSource(instance.sourceName);
        it = m_instances.erase(it);
    }
}

void SkinnedMeshRuntimeMeshCache::clear(){
    m_handleToEntity.clear();
    m_instances.clear();
    m_sources.clear();
}

RuntimeMeshHandle SkinnedMeshRuntimeMeshCache::handleForEntity(const Core::ECS::EntityID entity)const{
    const auto found = m_instances.find(entity);
    if(found == m_instances.end())
        return RuntimeMeshHandle{};
    return found.value().handle;
}

SkinnedMeshRuntimeMeshInstance* SkinnedMeshRuntimeMeshCache::findInstance(const RuntimeMeshHandle handle){
    return findInstanceByEntity(entityForHandle(handle));
}

const SkinnedMeshRuntimeMeshInstance* SkinnedMeshRuntimeMeshCache::findInstance(const RuntimeMeshHandle handle)const{
    return findInstanceByEntity(entityForHandle(handle));
}

u32 SkinnedMeshRuntimeMeshCache::editRevision(const RuntimeMeshHandle handle)const{
    const SkinnedMeshRuntimeMeshInstance* instance = findInstance(handle);
    return instance ? instance->editRevision : 0u;
}

bool SkinnedMeshRuntimeMeshCache::bumpEditRevision(const RuntimeMeshHandle handle, const RuntimeMeshDirtyFlags dirtyFlags){
    if(!handle.valid())
        return false;

    SkinnedMeshRuntimeMeshInstance* instance = findInstance(handle);
    if(!instance)
        return false;
    const RuntimeMeshDirtyFlags expandedDirtyFlags = __hidden_runtime_cache::ExpandDirtyFlags(dirtyFlags);
    if(expandedDirtyFlags == RuntimeMeshDirtyFlag::None){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshRuntimeMeshCache: runtime mesh '{}' revision bump requires dirty flags")
            , instance->handle.value
        );
        return false;
    }
    if(instance->editRevision == Limit<u32>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshRuntimeMeshCache: runtime mesh '{}' edit revision overflowed")
            , instance->handle.value
        );
        return false;
    }

    ++instance->editRevision;
    instance->dirtyFlags = static_cast<RuntimeMeshDirtyFlags>(instance->dirtyFlags | expandedDirtyFlags);
    return true;
}

RuntimeMeshHandle SkinnedMeshRuntimeMeshCache::allocateHandle(){
    while(m_nextHandleValue != 0u){
        RuntimeMeshHandle handle;
        handle.value = m_nextHandleValue;
        ++m_nextHandleValue;
        if(m_handleToEntity.find(handle.value) == m_handleToEntity.end())
            return handle;
    }

    NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshRuntimeMeshCache: runtime mesh handle space exhausted"));
    return RuntimeMeshHandle{};
}

void SkinnedMeshRuntimeMeshCache::releaseRuntimeMesh(const Core::ECS::EntityID entity){
    const auto foundInstance = m_instances.find(entity);
    if(foundInstance == m_instances.end())
        return;

    const SkinnedMeshRuntimeMeshInstance& instance = foundInstance.value();
    m_handleToEntity.erase(instance.handle.value);
    releaseSource(instance.sourceName);
    m_instances.erase(foundInstance);
}

void SkinnedMeshRuntimeMeshCache::releaseSource(const Name& sourceName){
    const auto foundSource = m_sources.find(sourceName);
    if(foundSource == m_sources.end())
        return;

    SkinnedMeshSource& source = foundSource.value();
    if(source.referenceCount > 0u)
        --source.referenceCount;
    if(source.referenceCount == 0u)
        m_sources.erase(foundSource);
}

void SkinnedMeshRuntimeMeshCache::eraseUnusedSource(const Name& sourceName){
    const auto foundSource = m_sources.find(sourceName);
    if(foundSource != m_sources.end() && foundSource.value().referenceCount == 0u)
        m_sources.erase(foundSource);
}

Core::ECS::EntityID SkinnedMeshRuntimeMeshCache::entityForHandle(const RuntimeMeshHandle handle)const{
    const auto foundEntity = m_handleToEntity.find(handle.value);
    if(foundEntity == m_handleToEntity.end())
        return Core::ECS::ENTITY_ID_INVALID;
    return foundEntity.value();
}

SkinnedMeshRuntimeMeshInstance* SkinnedMeshRuntimeMeshCache::findInstanceByEntity(const Core::ECS::EntityID entity){
    if(!entity.valid())
        return nullptr;

    const auto foundInstance = m_instances.find(entity);
    if(foundInstance == m_instances.end())
        return nullptr;
    return &foundInstance.value();
}

const SkinnedMeshRuntimeMeshInstance* SkinnedMeshRuntimeMeshCache::findInstanceByEntity(const Core::ECS::EntityID entity)const{
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

