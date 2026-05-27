// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "skinned_geometry_runtime_mesh_cache.h"

#include <core/common/log.h>
#include <core/ecs/world.h>
#include <impl/assets_geometry/skinned_geometry_asset.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skinned_geometry_runtime_mesh_cache{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr RuntimeMeshDirtyFlags s_KnownDirtyFlags = RuntimeMeshDirtyFlag::All;

[[nodiscard]] RuntimeMeshDirtyFlags SanitizeDirtyFlags(const RuntimeMeshDirtyFlags dirtyFlags){
    return static_cast<RuntimeMeshDirtyFlags>(dirtyFlags & s_KnownDirtyFlags);
}

[[nodiscard]] RuntimeMeshDirtyFlags ExpandDirtyFlags(const RuntimeMeshDirtyFlags dirtyFlags){
    RuntimeMeshDirtyFlags expanded = SanitizeDirtyFlags(dirtyFlags);
    if((expanded & (RuntimeMeshDirtyFlag::TopologyDirty | RuntimeMeshDirtyFlag::AttributesDirty)) != 0u){
        expanded = static_cast<RuntimeMeshDirtyFlags>(
            expanded | RuntimeMeshDirtyFlag::GpuUploadDirty | RuntimeMeshDirtyFlag::SkinnedGeometryInputDirty
        );
    }
    else if((expanded & RuntimeMeshDirtyFlag::GpuUploadDirty) != 0u){
        expanded = static_cast<RuntimeMeshDirtyFlags>(expanded | RuntimeMeshDirtyFlag::SkinnedGeometryInputDirty);
    }
    return expanded;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const SkinnedGeometry* SkinnedGeometryRuntimeMeshCache::SkinnedGeometrySource::geometry()const{
    if(!asset || asset->assetType() != SkinnedGeometry::AssetTypeName())
        return nullptr;
    return checked_cast<const SkinnedGeometry*>(asset.get());
}


SkinnedGeometryRuntimeMeshCache::SkinnedGeometryRuntimeMeshCache(Core::Alloc::GlobalArena& arena, Core::Graphics& graphics, Core::Assets::AssetManager& assetManager)
    : m_arena(arena)
    , m_graphics(graphics)
    , m_assetManager(assetManager)
    , m_sources(0, Hasher<Name>(), EqualTo<Name>(), arena)
    , m_instances(0, Hasher<Core::ECS::EntityID>(), EqualTo<Core::ECS::EntityID>(), arena)
    , m_handleToEntity(0, Hasher<u64>(), EqualTo<u64>(), arena)
{}


void SkinnedGeometryRuntimeMeshCache::update(Core::ECS::World& world){
    auto skinnedGeometryView = world.view<SkinnedGeometryComponent>();
    const usize rendererCandidateCount = skinnedGeometryView.candidateCount();
    if(rendererCandidateCount == 0u){
        clear();
        return;
    }

    m_instances.reserve(rendererCandidateCount);
    m_handleToEntity.reserve(rendererCandidateCount);
    m_sources.reserve(rendererCandidateCount);

    const bool pruneStaleInstances = !m_instances.empty();
    skinnedGeometryView.each(
        [&](Core::ECS::EntityID entity, SkinnedGeometryComponent& component){
            if(!ensureRuntimeMesh(entity, component))
                component.runtimeMesh.reset();
        }
    );

    if(!pruneStaleInstances)
        return;

    for(auto it = m_instances.begin(); it != m_instances.end();){
        const Core::ECS::EntityID entity = it->first;
        if(world.tryGetComponent<SkinnedGeometryComponent>(entity)){
            ++it;
            continue;
        }

        const SkinnedGeometryRuntimeMeshInstance& instance = it.value();
        m_handleToEntity.erase(instance.handle.value);
        releaseSource(instance.source.name());
        it = m_instances.erase(it);
    }
}

void SkinnedGeometryRuntimeMeshCache::clear(){
    m_handleToEntity.clear();
    m_instances.clear();
    m_sources.clear();
}

RuntimeMeshHandle SkinnedGeometryRuntimeMeshCache::handleForEntity(const Core::ECS::EntityID entity)const{
    const auto found = m_instances.find(entity);
    if(found == m_instances.end())
        return RuntimeMeshHandle{};
    return found.value().handle;
}

SkinnedGeometryRuntimeMeshInstance* SkinnedGeometryRuntimeMeshCache::findInstance(const RuntimeMeshHandle handle){
    return findInstanceByEntity(entityForHandle(handle));
}

const SkinnedGeometryRuntimeMeshInstance* SkinnedGeometryRuntimeMeshCache::findInstance(const RuntimeMeshHandle handle)const{
    return findInstanceByEntity(entityForHandle(handle));
}

u32 SkinnedGeometryRuntimeMeshCache::editRevision(const RuntimeMeshHandle handle)const{
    const SkinnedGeometryRuntimeMeshInstance* instance = findInstance(handle);
    return instance ? instance->editRevision : 0u;
}

bool SkinnedGeometryRuntimeMeshCache::bumpEditRevision(const RuntimeMeshHandle handle, const RuntimeMeshDirtyFlags dirtyFlags){
    if(!handle.valid())
        return false;

    SkinnedGeometryRuntimeMeshInstance* instance = findInstance(handle);
    if(!instance)
        return false;
    const RuntimeMeshDirtyFlags expandedDirtyFlags =
        __hidden_skinned_geometry_runtime_mesh_cache::ExpandDirtyFlags(dirtyFlags)
    ;
    if(expandedDirtyFlags == RuntimeMeshDirtyFlag::None){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometryRuntimeMeshCache: runtime mesh '{}' revision bump requires dirty flags")
            , instance->handle.value
        );
        return false;
    }
    if(instance->editRevision == Limit<u32>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometryRuntimeMeshCache: runtime mesh '{}' edit revision overflowed")
            , instance->handle.value
        );
        return false;
    }

    ++instance->editRevision;
    instance->dirtyFlags = static_cast<RuntimeMeshDirtyFlags>(
        instance->dirtyFlags | expandedDirtyFlags
    );
    return true;
}

RuntimeMeshHandle SkinnedGeometryRuntimeMeshCache::allocateHandle(){
    while(m_nextHandleValue != 0u){
        RuntimeMeshHandle handle;
        handle.value = m_nextHandleValue;
        ++m_nextHandleValue;
        if(m_handleToEntity.find(handle.value) == m_handleToEntity.end())
            return handle;
    }

    NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometryRuntimeMeshCache: runtime mesh handle space exhausted"));
    return RuntimeMeshHandle{};
}

void SkinnedGeometryRuntimeMeshCache::releaseRuntimeMesh(const Core::ECS::EntityID entity){
    const auto foundInstance = m_instances.find(entity);
    if(foundInstance == m_instances.end())
        return;

    const SkinnedGeometryRuntimeMeshInstance& instance = foundInstance.value();
    m_handleToEntity.erase(instance.handle.value);
    releaseSource(instance.source.name());
    m_instances.erase(foundInstance);
}

void SkinnedGeometryRuntimeMeshCache::releaseSource(const Name& sourceName){
    const auto foundSource = m_sources.find(sourceName);
    if(foundSource == m_sources.end())
        return;

    SkinnedGeometrySource& source = foundSource.value();
    if(source.referenceCount > 0u)
        --source.referenceCount;
    if(source.referenceCount == 0u)
        m_sources.erase(foundSource);
}

void SkinnedGeometryRuntimeMeshCache::eraseUnusedSource(const Name& sourceName){
    const auto foundSource = m_sources.find(sourceName);
    if(foundSource != m_sources.end() && foundSource.value().referenceCount == 0u)
        m_sources.erase(foundSource);
}

Core::ECS::EntityID SkinnedGeometryRuntimeMeshCache::entityForHandle(const RuntimeMeshHandle handle)const{
    const auto foundEntity = m_handleToEntity.find(handle.value);
    if(foundEntity == m_handleToEntity.end())
        return Core::ECS::ENTITY_ID_INVALID;
    return foundEntity.value();
}

SkinnedGeometryRuntimeMeshInstance* SkinnedGeometryRuntimeMeshCache::findInstanceByEntity(const Core::ECS::EntityID entity){
    if(!entity.valid())
        return nullptr;

    const auto foundInstance = m_instances.find(entity);
    if(foundInstance == m_instances.end())
        return nullptr;
    return &foundInstance.value();
}

const SkinnedGeometryRuntimeMeshInstance* SkinnedGeometryRuntimeMeshCache::findInstanceByEntity(const Core::ECS::EntityID entity)const{
    if(!entity.valid())
        return nullptr;

    const auto foundInstance = m_instances.find(entity);
    if(foundInstance == m_instances.end())
        return nullptr;
    return &foundInstance.value();
}

NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

