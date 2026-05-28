// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "runtime_cache.h"

#include <core/assets/manager.h>
#include <core/common/log.h>
#include <impl/assets_mesh/meshlet_payload_packing.h>
#include <impl/assets_mesh/skinned_asset.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_runtime_cache_source{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr RuntimeMeshDirtyFlags s_GpuUploadHandledDirtyFlags =
    RuntimeMeshDirtyFlag::TopologyDirty
    | RuntimeMeshDirtyFlag::AttributesDirty
    | RuntimeMeshDirtyFlag::SkinnedMeshInputDirty
    | RuntimeMeshDirtyFlag::GpuUploadDirty
    | RuntimeMeshDirtyFlag::MeshletBoundsDirty
;

#include <impl/assets_mesh/meshlet_ref_validation.inl>

[[nodiscard]] bool ResolveAttributeSkins(
    const SkinnedMesh& mesh,
    SkinnedMeshRuntimeMeshInstance::AttributeSkinVector& outAttributeSkins
){
    return ResolveMeshletAttributeSkins(
        mesh.meshlets(),
        mesh.meshletPositionRefs(),
        mesh.meshletLocalVertexRefs(),
        mesh.meshletAttributeRefs().size(),
        outAttributeSkins,
        [](const usize meshletIndex, const usize attributeIndex, const u32 previousSkin, const u32 skinIndex){
            static_cast<void>(attributeIndex);
            static_cast<void>(previousSkin);
            static_cast<void>(skinIndex);
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshRuntimeMeshCache: source meshlet {} shares an attribute across skin identities")
                , meshletIndex
            );
            return false;
        },
        [](const usize attributeIndex){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshRuntimeMeshCache: source attribute ref {} is unreferenced")
                , attributeIndex
            );
            return false;
        }
    );
}

[[nodiscard]] bool BuildRuntimeZippedPayload(
    const SkinnedMesh& mesh,
    SkinnedMeshRuntimeMeshInstance& instance
){
    const auto& sourcePositionRefs = mesh.meshletPositionRefs();
    const auto& sourceAttributeRefs = mesh.meshletAttributeRefs();
    if(
        sourcePositionRefs.size() > static_cast<usize>(Limit<u32>::s_Max)
        || sourceAttributeRefs.size() > static_cast<usize>(Limit<u32>::s_Max)
    )
        return false;

    instance.restPositions.clear();
    instance.restNormals.clear();
    instance.restTangents.clear();
    instance.meshletPositionRefs.clear();
    instance.meshletAttributeRefs.clear();
    instance.attributeSkins.clear();
    instance.restPositions.reserve(sourcePositionRefs.size());
    instance.restNormals.reserve(sourceAttributeRefs.size());
    instance.restTangents.reserve(sourceAttributeRefs.size());
    instance.meshletPositionRefs.reserve(sourcePositionRefs.size());
    instance.meshletAttributeRefs.reserve(sourceAttributeRefs.size());

    if(!ResolveAttributeSkins(mesh, instance.attributeSkins))
        return false;

    for(usize positionRefIndex = 0u; positionRefIndex < sourcePositionRefs.size(); ++positionRefIndex){
        const MeshletDeformedPositionRef& sourceRef = sourcePositionRefs[positionRefIndex];
        if(!MeshletPositionRefInRange(sourceRef, mesh.positionStream().size(), mesh.skinStream().size(), true)){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshRuntimeMeshCache: source position ref {} is out of range")
                , positionRefIndex
            );
            return false;
        }

        MeshletDeformedPositionRef runtimeRef = sourceRef;
        runtimeRef.position = static_cast<u32>(positionRefIndex);
        instance.restPositions.push_back(mesh.positionStream()[sourceRef.position]);
        instance.meshletPositionRefs.push_back(runtimeRef);
    }

    for(usize attributeRefIndex = 0u; attributeRefIndex < sourceAttributeRefs.size(); ++attributeRefIndex){
        const MeshletShadingAttributeRef& sourceRef = sourceAttributeRefs[attributeRefIndex];
        if(!MeshletAttributeRefInRange(
            sourceRef,
            mesh.normalStream().size(),
            mesh.tangentStream().size(),
            mesh.uv0Stream().size(),
            mesh.colorStream().size()
        )){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshRuntimeMeshCache: source attribute ref {} is out of range")
                , attributeRefIndex
            );
            return false;
        }

        instance.restNormals.push_back(mesh.normalStream()[sourceRef.normal]);
        instance.restTangents.push_back(mesh.tangentStream()[sourceRef.tangent]);

        MeshletShadingAttributeRef runtimeRef = sourceRef;
        runtimeRef.normal = static_cast<u32>(attributeRefIndex);
        runtimeRef.tangent = static_cast<u32>(attributeRefIndex);
        instance.meshletAttributeRefs.push_back(runtimeRef);
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
                    instance.dirtyFlags & ~__hidden_runtime_cache_source::s_GpuUploadHandledDirtyFlags
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
    instance.meshletLocalVertexRefs = mesh->meshletLocalVertexRefs();
    instance.meshletPrimitiveIndices = mesh->meshletPrimitiveIndices();
    instance.dirtyFlags = RuntimeMeshDirtyFlag::All;
    if(!__hidden_runtime_cache_source::BuildRuntimeZippedPayload(*mesh, instance)){
        eraseUnusedSource(sourceName);
        return false;
    }

    if(!uploadRuntimeMeshBuffers(instance)){
        eraseUnusedSource(sourceName);
        return false;
    }
    instance.dirtyFlags = static_cast<RuntimeMeshDirtyFlags>(
        instance.dirtyFlags & ~__hidden_runtime_cache_source::s_GpuUploadHandledDirtyFlags
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

