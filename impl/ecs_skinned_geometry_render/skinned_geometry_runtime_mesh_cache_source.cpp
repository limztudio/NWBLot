// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "skinned_geometry_runtime_mesh_cache.h"

#include <core/assets/asset_manager.h>
#include <core/common/log.h>
#include <impl/assets_geometry/skinned_geometry_asset.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skinned_geometry_runtime_mesh_cache_source{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr RuntimeMeshDirtyFlags s_GpuUploadHandledDirtyFlags =
    RuntimeMeshDirtyFlag::TopologyDirty
    | RuntimeMeshDirtyFlag::AttributesDirty
    | RuntimeMeshDirtyFlag::SkinnedGeometryInputDirty
    | RuntimeMeshDirtyFlag::GpuUploadDirty
;

[[nodiscard]] bool SourceVertexRefInRange(const GeometryVertexRef& ref, const SkinnedGeometry& geometry){
    return
        ref.position < geometry.positionStream().size()
        && ref.normal < geometry.normalStream().size()
        && ref.tangent < geometry.tangentStream().size()
        && ref.uv0 < geometry.uv0Stream().size()
        && ref.color < geometry.colorStream().size()
        && ref.skin < geometry.skinStream().size()
    ;
}

[[nodiscard]] bool BuildRuntimeVertexPayload(
    const SkinnedGeometry& geometry,
    SkinnedGeometryRuntimeMeshInstance& instance
){
    const auto& sourceRefs = geometry.vertexRefs();
    if(sourceRefs.size() > static_cast<usize>(Limit<u32>::s_Max))
        return false;

    instance.restPositions.clear();
    instance.restNormals.clear();
    instance.restTangents.clear();
    instance.vertexRefs.clear();
    instance.restPositions.reserve(sourceRefs.size());
    instance.restNormals.reserve(sourceRefs.size());
    instance.restTangents.reserve(sourceRefs.size());
    instance.vertexRefs.reserve(sourceRefs.size());

    for(usize vertexRefIndex = 0u; vertexRefIndex < sourceRefs.size(); ++vertexRefIndex){
        const GeometryVertexRef& sourceRef = sourceRefs[vertexRefIndex];
        if(!SourceVertexRefInRange(sourceRef, geometry)){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometryRuntimeMeshCache: source vertex ref {} is out of range")
                , vertexRefIndex
            );
            return false;
        }

        instance.restPositions.push_back(geometry.positionStream()[sourceRef.position]);
        instance.restNormals.push_back(geometry.normalStream()[sourceRef.normal]);
        instance.restTangents.push_back(geometry.tangentStream()[sourceRef.tangent]);

        GeometryVertexRef runtimeRef = sourceRef;
        runtimeRef.position = static_cast<u32>(vertexRefIndex);
        runtimeRef.normal = static_cast<u32>(vertexRefIndex);
        runtimeRef.tangent = static_cast<u32>(vertexRefIndex);
        instance.vertexRefs.push_back(runtimeRef);
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool SkinnedGeometryRuntimeMeshCache::ensureRuntimeMesh(Core::ECS::EntityID entity, SkinnedGeometryComponent& component){
    const Name sourceName = component.skinnedGeometry.name();
    if(!sourceName){
        releaseRuntimeMesh(entity);
        component.runtimeMesh.reset();
        return false;
    }

    const auto foundInstance = m_instances.find(entity);
    if(foundInstance != m_instances.end()){
        SkinnedGeometryRuntimeMeshInstance& instance = foundInstance.value();
        if(instance.source.name() == sourceName){
            component.runtimeMesh = instance.handle;
            if((instance.dirtyFlags & RuntimeMeshDirtyFlag::GpuUploadDirty) != 0u){
                if(!uploadRuntimeMeshBuffers(instance)){
                    releaseRuntimeMesh(entity);
                    component.runtimeMesh.reset();
                    return false;
                }
                instance.dirtyFlags = static_cast<RuntimeMeshDirtyFlags>(
                    instance.dirtyFlags & ~__hidden_skinned_geometry_runtime_mesh_cache_source::s_GpuUploadHandledDirtyFlags
                );
            }
            return instance.valid();
        }

        releaseRuntimeMesh(entity);
        component.runtimeMesh.reset();
    }

    SkinnedGeometrySource* source = nullptr;
    if(!ensureSourceLoaded(component.skinnedGeometry, source))
        return false;
    const SkinnedGeometry* geometry = source ? source->geometry() : nullptr;
    if(!geometry)
        return false;

    SkinnedGeometryRuntimeMeshInstance instance(m_arena);
    instance.entity = entity;
    instance.handle = allocateHandle();
    if(!instance.handle.valid()){
        eraseUnusedSource(sourceName);
        return false;
    }
    instance.source = component.skinnedGeometry;
    instance.geometryClass = geometry->geometryClass();
    instance.uv0 = geometry->uv0Stream();
    instance.colors = geometry->colorStream();
    instance.skin = geometry->skinStream();
    instance.skeletonJointCount = geometry->skeletonJointCount();
    instance.inverseBindMatrices = geometry->inverseBindMatrices();
    instance.meshlets = geometry->meshlets();
    instance.meshletBounds = geometry->meshletBounds();
    instance.meshletVertexRefs = geometry->meshletVertexRefs();
    instance.meshletPrimitiveIndices = geometry->meshletPrimitiveIndices();
    instance.dirtyFlags = RuntimeMeshDirtyFlag::All;
    if(!__hidden_skinned_geometry_runtime_mesh_cache_source::BuildRuntimeVertexPayload(*geometry, instance)){
        eraseUnusedSource(sourceName);
        return false;
    }

    if(!uploadRuntimeMeshBuffers(instance)){
        eraseUnusedSource(sourceName);
        return false;
    }
    instance.dirtyFlags = static_cast<RuntimeMeshDirtyFlags>(
        instance.dirtyFlags & ~__hidden_skinned_geometry_runtime_mesh_cache_source::s_GpuUploadHandledDirtyFlags
    );

    ++source->referenceCount;
    const RuntimeMeshHandle handle = instance.handle;
    auto result = m_instances.try_emplace(entity, Move(instance));
    auto it = result.first;
    m_handleToEntity.emplace(handle.value, entity);
    component.runtimeMesh = handle;
    return it.value().valid();
}

bool SkinnedGeometryRuntimeMeshCache::ensureSourceLoaded(const Core::Assets::AssetRef<SkinnedGeometry>& sourceAsset, SkinnedGeometrySource*& outSource){
    outSource = nullptr;

    const Name sourceName = sourceAsset.name();
    if(!sourceName){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometryRuntimeMeshCache: skinned geometry renderer source asset is empty"));
        return false;
    }

    const auto foundSource = m_sources.find(sourceName);
    if(foundSource != m_sources.end()){
        outSource = &foundSource.value();
        return outSource->geometry() != nullptr;
    }

    UniquePtr<Core::Assets::IAsset> loadedAsset;
    if(!m_assetManager.loadSync(SkinnedGeometry::AssetTypeName(), sourceName, loadedAsset)){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometryRuntimeMeshCache: failed to load skinned geometry '{}'")
            , StringConvert(sourceName.c_str())
        );
        return false;
    }
    if(!loadedAsset || loadedAsset->assetType() != SkinnedGeometry::AssetTypeName()){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometryRuntimeMeshCache: asset '{}' is not skinned geometry")
            , StringConvert(sourceName.c_str())
        );
        return false;
    }

    SkinnedGeometrySource source;
    source.sourceName = sourceName;
    source.asset = Move(loadedAsset);

    auto result = m_sources.try_emplace(sourceName, Move(source));
    auto it = result.first;
    outSource = &it.value();
    return outSource->geometry() != nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

