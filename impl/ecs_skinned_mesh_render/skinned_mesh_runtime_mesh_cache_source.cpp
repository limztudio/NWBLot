// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "skinned_mesh_runtime_mesh_cache.h"

#include <core/assets/asset_manager.h>
#include <core/common/log.h>
#include <impl/assets_mesh/skinned_mesh_asset.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skinned_mesh_runtime_mesh_cache_source{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr RuntimeMeshDirtyFlags s_GpuUploadHandledDirtyFlags =
    RuntimeMeshDirtyFlag::TopologyDirty
    | RuntimeMeshDirtyFlag::AttributesDirty
    | RuntimeMeshDirtyFlag::SkinnedMeshInputDirty
    | RuntimeMeshDirtyFlag::GpuUploadDirty
;

[[nodiscard]] bool SourceVertexRefInRange(const MeshVertexRef& ref, const SkinnedMesh& mesh){
    return
        ref.position < mesh.positionStream().size()
        && ref.normal < mesh.normalStream().size()
        && ref.tangent < mesh.tangentStream().size()
        && ref.uv0 < mesh.uv0Stream().size()
        && ref.color < mesh.colorStream().size()
        && ref.skin < mesh.skinStream().size()
    ;
}

[[nodiscard]] bool BuildRuntimeVertexPayload(
    const SkinnedMesh& mesh,
    SkinnedMeshRuntimeMeshInstance& instance
){
    const auto& sourceRefs = mesh.vertexRefs();
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
        const MeshVertexRef& sourceRef = sourceRefs[vertexRefIndex];
        if(!SourceVertexRefInRange(sourceRef, mesh)){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshRuntimeMeshCache: source vertex ref {} is out of range")
                , vertexRefIndex
            );
            return false;
        }

        instance.restPositions.push_back(mesh.positionStream()[sourceRef.position]);
        instance.restNormals.push_back(mesh.normalStream()[sourceRef.normal]);
        instance.restTangents.push_back(mesh.tangentStream()[sourceRef.tangent]);

        MeshVertexRef runtimeRef = sourceRef;
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


bool SkinnedMeshRuntimeMeshCache::ensureRuntimeMesh(Core::ECS::EntityID entity, SkinnedMeshComponent& component){
    const Name sourceName = component.skinnedMesh.name();
    if(!sourceName){
        releaseRuntimeMesh(entity);
        component.runtimeMesh.reset();
        return false;
    }

    const auto foundInstance = m_instances.find(entity);
    if(foundInstance != m_instances.end()){
        SkinnedMeshRuntimeMeshInstance& instance = foundInstance.value();
        if(instance.source.name() == sourceName){
            component.runtimeMesh = instance.handle;
            if((instance.dirtyFlags & RuntimeMeshDirtyFlag::GpuUploadDirty) != 0u){
                if(!uploadRuntimeMeshBuffers(instance)){
                    releaseRuntimeMesh(entity);
                    component.runtimeMesh.reset();
                    return false;
                }
                instance.dirtyFlags = static_cast<RuntimeMeshDirtyFlags>(
                    instance.dirtyFlags & ~__hidden_skinned_mesh_runtime_mesh_cache_source::s_GpuUploadHandledDirtyFlags
                );
            }
            return instance.valid();
        }

        releaseRuntimeMesh(entity);
        component.runtimeMesh.reset();
    }

    SkinnedMeshSource* source = nullptr;
    if(!ensureSourceLoaded(component.skinnedMesh, source))
        return false;
    const SkinnedMesh* mesh = source ? source->mesh() : nullptr;
    if(!mesh)
        return false;

    SkinnedMeshRuntimeMeshInstance instance(m_arena);
    instance.entity = entity;
    instance.handle = allocateHandle();
    if(!instance.handle.valid()){
        eraseUnusedSource(sourceName);
        return false;
    }
    instance.source = component.skinnedMesh;
    instance.meshClass = mesh->meshClass();
    instance.uv0 = mesh->uv0Stream();
    instance.colors = mesh->colorStream();
    instance.skin = mesh->skinStream();
    instance.skeletonJointCount = mesh->skeletonJointCount();
    instance.inverseBindMatrices = mesh->inverseBindMatrices();
    instance.meshlets = mesh->meshlets();
    instance.meshletBounds = mesh->meshletBounds();
    instance.meshletVertexRefs = mesh->meshletVertexRefs();
    instance.meshletPrimitiveIndices = mesh->meshletPrimitiveIndices();
    instance.dirtyFlags = RuntimeMeshDirtyFlag::All;
    if(!__hidden_skinned_mesh_runtime_mesh_cache_source::BuildRuntimeVertexPayload(*mesh, instance)){
        eraseUnusedSource(sourceName);
        return false;
    }

    if(!uploadRuntimeMeshBuffers(instance)){
        eraseUnusedSource(sourceName);
        return false;
    }
    instance.dirtyFlags = static_cast<RuntimeMeshDirtyFlags>(
        instance.dirtyFlags & ~__hidden_skinned_mesh_runtime_mesh_cache_source::s_GpuUploadHandledDirtyFlags
    );

    ++source->referenceCount;
    const RuntimeMeshHandle handle = instance.handle;
    auto result = m_instances.try_emplace(entity, Move(instance));
    auto it = result.first;
    m_handleToEntity.emplace(handle.value, entity);
    component.runtimeMesh = handle;
    return it.value().valid();
}

bool SkinnedMeshRuntimeMeshCache::ensureSourceLoaded(const Core::Assets::AssetRef<SkinnedMesh>& sourceAsset, SkinnedMeshSource*& outSource){
    outSource = nullptr;

    const Name sourceName = sourceAsset.name();
    if(!sourceName){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshRuntimeMeshCache: skinned mesh renderer source asset is empty"));
        return false;
    }

    const auto foundSource = m_sources.find(sourceName);
    if(foundSource != m_sources.end()){
        outSource = &foundSource.value();
        return outSource->mesh() != nullptr;
    }

    UniquePtr<Core::Assets::IAsset> loadedAsset;
    if(!m_assetManager.loadSync(SkinnedMesh::AssetTypeName(), sourceName, loadedAsset)){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshRuntimeMeshCache: failed to load skinned mesh '{}'")
            , StringConvert(sourceName.c_str())
        );
        return false;
    }
    if(!loadedAsset || loadedAsset->assetType() != SkinnedMesh::AssetTypeName()){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshRuntimeMeshCache: asset '{}' is not skinned mesh")
            , StringConvert(sourceName.c_str())
        );
        return false;
    }

    SkinnedMeshSource source;
    source.sourceName = sourceName;
    source.asset = Move(loadedAsset);

    auto result = m_sources.try_emplace(sourceName, Move(source));
    auto it = result.first;
    outSource = &it.value();
    return outSource->mesh() != nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

