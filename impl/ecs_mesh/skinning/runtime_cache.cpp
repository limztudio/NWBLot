// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "runtime_cache.h"

#include <global/core/common/log.h>
#include <global/core/ecs/world.h>
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
            | RuntimeMeshDirtyFlag::SkinningInputDirty
            | RuntimeMeshDirtyFlag::MeshletBoundsDirty
        );
    }
    else if((expanded & RuntimeMeshDirtyFlag::GpuUploadDirty) != 0u){
        expanded = static_cast<RuntimeMeshDirtyFlags>(
            expanded | RuntimeMeshDirtyFlag::SkinningInputDirty | RuntimeMeshDirtyFlag::MeshletBoundsDirty
        );
    }
    return expanded;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const Mesh* MeshSkinningRuntimeCache::MeshSkinningSource::mesh()const{
    if(!meshAsset || meshAsset->assetType() != Mesh::AssetTypeName())
        return nullptr;
    return checked_cast<const Mesh*>(meshAsset.get());
}

const Skin* MeshSkinningRuntimeCache::MeshSkinningSource::skin()const{
    if(!skinAsset || skinAsset->assetType() != Skin::AssetTypeName())
        return nullptr;
    return checked_cast<const Skin*>(skinAsset.get());
}


MeshSkinningRuntimeCache::MeshSkinningRuntimeCache(Core::Alloc::GlobalArena& arena, Core::Graphics& graphics, Core::Assets::AssetManager& assetManager)
    : m_arena(arena)
    , m_graphics(graphics)
    , m_assetManager(assetManager)
    , m_sources(0, Hasher<Name>(), EqualTo<Name>(), arena)
    , m_instances(0, Hasher<Core::ECS::EntityID>(), EqualTo<Core::ECS::EntityID>(), arena)
    , m_handleToEntity(0, Hasher<u64>(), EqualTo<u64>(), arena)
{}


void MeshSkinningRuntimeCache::prepareResources(Core::ECS::World& world){
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
    skinningBindingView.each(
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

        const MeshSkinningRuntimeInstance& instance = it.value();
        m_handleToEntity.erase(instance.handle.value);
        releaseSource(instance.sourceName);
        it = m_instances.erase(it);
    }
}

void MeshSkinningRuntimeCache::clear(){
    m_handleToEntity.clear();
    m_instances.clear();
    m_sources.clear();
}

RuntimeMeshHandle MeshSkinningRuntimeCache::handleForEntity(const Core::ECS::EntityID entity)const{
    const auto found = m_instances.find(entity);
    if(found == m_instances.end())
        return RuntimeMeshHandle{};
    return found.value().handle;
}

MeshSkinningRuntimeInstance* MeshSkinningRuntimeCache::findInstance(const RuntimeMeshHandle handle){
    return findInstanceByEntity(entityForHandle(handle));
}

const MeshSkinningRuntimeInstance* MeshSkinningRuntimeCache::findInstance(const RuntimeMeshHandle handle)const{
    return findInstanceByEntity(entityForHandle(handle));
}

u32 MeshSkinningRuntimeCache::editRevision(const RuntimeMeshHandle handle)const{
    const MeshSkinningRuntimeInstance* instance = findInstance(handle);
    return instance ? instance->editRevision : 0u;
}

bool MeshSkinningRuntimeCache::bumpEditRevision(const RuntimeMeshHandle handle, const RuntimeMeshDirtyFlags dirtyFlags){
    if(!handle.valid())
        return false;

    MeshSkinningRuntimeInstance* instance = findInstance(handle);
    if(!instance)
        return false;
    const RuntimeMeshDirtyFlags expandedDirtyFlags = __hidden_runtime_cache::ExpandDirtyFlags(dirtyFlags);
    if(expandedDirtyFlags == RuntimeMeshDirtyFlag::None){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: runtime mesh '{}' revision bump requires dirty flags")
            , instance->handle.value
        );
        return false;
    }
    if(instance->editRevision == Limit<u32>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: runtime mesh '{}' edit revision overflowed")
            , instance->handle.value
        );
        return false;
    }

    ++instance->editRevision;
    instance->dirtyFlags = static_cast<RuntimeMeshDirtyFlags>(instance->dirtyFlags | expandedDirtyFlags);
    return true;
}

RuntimeMeshHandle MeshSkinningRuntimeCache::allocateHandle(){
    while(m_nextHandleValue != 0u){
        RuntimeMeshHandle handle;
        handle.value = m_nextHandleValue;
        ++m_nextHandleValue;
        if(m_handleToEntity.find(handle.value) == m_handleToEntity.end())
            return handle;
    }

    NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: runtime mesh handle space exhausted"));
    return RuntimeMeshHandle{};
}

void MeshSkinningRuntimeCache::releaseRuntimeMesh(const Core::ECS::EntityID entity){
    const auto foundInstance = m_instances.find(entity);
    if(foundInstance == m_instances.end())
        return;

    const MeshSkinningRuntimeInstance& instance = foundInstance.value();
    m_handleToEntity.erase(instance.handle.value);
    releaseSource(instance.sourceName);
    m_instances.erase(foundInstance);
}

void MeshSkinningRuntimeCache::releaseSource(const Name& sourceName){
    const auto foundSource = m_sources.find(sourceName);
    if(foundSource == m_sources.end())
        return;

    MeshSkinningSource& source = foundSource.value();
    if(source.referenceCount > 0u)
        --source.referenceCount;
    if(source.referenceCount == 0u)
        m_sources.erase(foundSource);
}

void MeshSkinningRuntimeCache::eraseUnusedSource(const Name& sourceName){
    const auto foundSource = m_sources.find(sourceName);
    if(foundSource != m_sources.end() && foundSource.value().referenceCount == 0u)
        m_sources.erase(foundSource);
}

Core::ECS::EntityID MeshSkinningRuntimeCache::entityForHandle(const RuntimeMeshHandle handle)const{
    const auto foundEntity = m_handleToEntity.find(handle.value);
    if(foundEntity == m_handleToEntity.end())
        return Core::ECS::ENTITY_ID_INVALID;
    return foundEntity.value();
}

MeshSkinningRuntimeInstance* MeshSkinningRuntimeCache::findInstanceByEntity(const Core::ECS::EntityID entity){
    if(!entity.valid())
        return nullptr;

    const auto foundInstance = m_instances.find(entity);
    if(foundInstance == m_instances.end())
        return nullptr;
    return &foundInstance.value();
}

const MeshSkinningRuntimeInstance* MeshSkinningRuntimeCache::findInstanceByEntity(const Core::ECS::EntityID entity)const{
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

