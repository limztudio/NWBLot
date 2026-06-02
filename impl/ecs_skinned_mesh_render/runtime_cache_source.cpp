// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "runtime_cache.h"

#include <core/assets/manager.h>
#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <impl/assets_mesh/meshlet_ref_encoding.h>
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

template<typename MeshletVectorT, typename PositionRefVectorT, typename LocalVertexRefVectorT>
[[nodiscard]] bool ResolveAttributeSkins(
    const MeshletVectorT& meshlets,
    const PositionRefVectorT& positionRefs,
    const LocalVertexRefVectorT& localVertexRefs,
    const usize attributeRefCount,
    SkinnedMeshRuntimeMeshInstance::AttributeSkinVector& outAttributeSkins
){
    return ResolveMeshletAttributeSkins(
        meshlets,
        positionRefs,
        localVertexRefs,
        attributeRefCount,
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

[[nodiscard]] bool DecodeRuntimeCapSourceVertex(
    const SkinnedMeshRuntimeMeshInstance& instance,
    const MeshletDesc& meshlet,
    const u32 localVertexIndex,
    RuntimeMeshCapSourceVertex& outVertex
){
    outVertex = RuntimeMeshCapSourceVertex{};
    if(localVertexIndex >= MeshletVertexCount(meshlet))
        return false;

    const usize localVertexOffset = static_cast<usize>(meshlet.localVertexOffset) + static_cast<usize>(localVertexIndex);
    if(localVertexOffset >= instance.meshletLocalVertexRefs.size())
        return false;

    const MeshletLocalVertexRef& localVertexRef = instance.meshletLocalVertexRefs[localVertexOffset];
    MeshletPositionStreamRef positionRef;
    if(!DecodeMeshletPositionRef(
        instance.meshletPositionRefDeltas.data(),
        instance.meshletPositionRefDeltas.size(),
        meshlet,
        localVertexRef.localDeformedPosition,
        true,
        positionRef
    ))
        return false;
    if(positionRef.position >= instance.restPositions.size())
        return false;

    MeshletAttributeStreamRef attributeRef;
    if(!DecodeMeshletAttributeRef(
        instance.meshletAttributeRefDeltas.data(),
        instance.meshletAttributeRefDeltas.size(),
        meshlet,
        localVertexRef.localAttribute,
        attributeRef
    ))
        return false;
    if(
        attributeRef.normal >= instance.restNormals.size()
        || attributeRef.tangent >= instance.restTangents.size()
        || attributeRef.uv0 >= instance.uv0.size()
        || attributeRef.color >= instance.colors.size()
    )
        return false;

    StoreFloat(VectorSetW(LoadFloat(instance.restPositions[positionRef.position]), 0.0f), &outVertex.position);
    StoreFloat(VectorSetW(LoadFloat(LoadHalf4U(instance.restNormals[attributeRef.normal])), 0.0f), &outVertex.normal);
    StoreFloat(LoadFloat(LoadHalf4U(instance.restTangents[attributeRef.tangent])), &outVertex.tangent);
    StoreFloat(VectorSetW(LoadFloat(instance.uv0[attributeRef.uv0]), 0.0f), &outVertex.uv0);
    StoreFloat(LoadFloat(LoadHalf4U(instance.colors[attributeRef.color])), &outVertex.color);
    return true;
}

[[nodiscard]] bool BuildCapSourceTriangles(SkinnedMeshRuntimeMeshInstance& instance){
    instance.capSourceTriangles.clear();

    usize triangleCapacity = 0u;
    for(const MeshletDesc& meshlet : instance.meshlets){
        const usize primitiveCount = static_cast<usize>(MeshletPrimitiveCount(meshlet));
        if(primitiveCount > Limit<usize>::s_Max - triangleCapacity){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshRuntimeMeshCache: source mesh '{}' CSG cap triangle count overflows")
                , StringConvert(instance.source.name().c_str())
            );
            return false;
        }
        triangleCapacity += primitiveCount;
    }
    if(triangleCapacity == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshRuntimeMeshCache: source mesh '{}' has no CSG cap source triangles")
            , StringConvert(instance.source.name().c_str())
        );
        return false;
    }
    instance.capSourceTriangles.reserve(triangleCapacity);

    for(const MeshletDesc& meshlet : instance.meshlets){
        const usize primitiveCount = static_cast<usize>(MeshletPrimitiveCount(meshlet));
        const usize primitiveByteBegin = static_cast<usize>(meshlet.primitiveOffset);
        const usize primitiveByteCount = primitiveCount * 3u;
        if(
            primitiveByteBegin > instance.meshletPrimitiveIndices.size()
            || primitiveByteCount > instance.meshletPrimitiveIndices.size() - primitiveByteBegin
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshRuntimeMeshCache: source mesh '{}' has invalid CSG cap primitive index range")
                , StringConvert(instance.source.name().c_str())
            );
            return false;
        }

        for(usize primitiveIndex = 0u; primitiveIndex < primitiveCount; ++primitiveIndex){
            RuntimeMeshCapSourceTriangle triangle;
            for(u32 corner = 0u; corner < 3u; ++corner){
                const usize primitiveByteOffset = primitiveByteBegin + primitiveIndex * 3u + static_cast<usize>(corner);
                const u32 localVertexIndex = static_cast<u32>(instance.meshletPrimitiveIndices[primitiveByteOffset]);
                if(!DecodeRuntimeCapSourceVertex(instance, meshlet, localVertexIndex, triangle.vertices[corner])){
                    NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshRuntimeMeshCache: source mesh '{}' failed to decode CSG cap source vertex")
                        , StringConvert(instance.source.name().c_str())
                    );
                    return false;
                }
            }
            instance.capSourceTriangles.push_back(triangle);
        }
    }

    return true;
}

[[nodiscard]] bool BuildRuntimeZippedPayload(
    const SkinnedMesh& mesh,
    SkinnedMeshRuntimeMeshInstance& instance
){
    Core::Alloc::ScratchArena scratchArena;
    Vector<MeshletPositionStreamRef, Core::Alloc::ScratchArena> runtimePositionRefs{scratchArena};
    Vector<MeshletAttributeStreamRef, Core::Alloc::ScratchArena> runtimeAttributeRefs{scratchArena};
    usize positionRefCount = 0u;
    usize attributeRefCount = 0u;
    for(const MeshletDesc& meshlet : mesh.meshlets()){
        positionRefCount += MeshletPositionCount(meshlet);
        attributeRefCount += MeshletAttributeCount(meshlet);
    }
    if(positionRefCount > static_cast<usize>(Limit<u32>::s_Max) || attributeRefCount > static_cast<usize>(Limit<u32>::s_Max))
        return false;

    instance.restPositions.clear();
    instance.restNormals.clear();
    instance.restTangents.clear();
    instance.meshlets.clear();
    instance.meshletPositionRefDeltas.clear();
    instance.meshletAttributeRefDeltas.clear();
    instance.attributeSkins.clear();
    instance.capSourceTriangles.clear();
    instance.meshlets.reserve(mesh.meshlets().size());
    instance.restPositions.reserve(positionRefCount);
    instance.restNormals.reserve(attributeRefCount);
    instance.restTangents.reserve(attributeRefCount);
    runtimePositionRefs.reserve(positionRefCount);
    runtimeAttributeRefs.reserve(attributeRefCount);

    for(usize meshletIndex = 0u; meshletIndex < mesh.meshlets().size(); ++meshletIndex){
        const MeshletDesc& sourceMeshlet = mesh.meshlets()[meshletIndex];
        MeshletDesc runtimeMeshlet = sourceMeshlet;
        runtimeMeshlet.positionRefOffset = static_cast<u32>(runtimePositionRefs.size());
        runtimeMeshlet.attributeRefOffset = static_cast<u32>(runtimeAttributeRefs.size());
        instance.meshlets.push_back(runtimeMeshlet);

        for(u32 localPositionIndex = 0u; localPositionIndex < MeshletPositionCount(sourceMeshlet); ++localPositionIndex){
            MeshletPositionStreamRef sourceRef;
            if(
                !DecodeMeshletPositionRef(
                    mesh.meshletPositionRefDeltas().data(),
                    mesh.meshletPositionRefDeltas().size(),
                    sourceMeshlet,
                    localPositionIndex,
                    true,
                    sourceRef
                )
                || !MeshletPositionRefInRange(sourceRef, mesh.positionStream().size(), mesh.skinStream().size(), true)
            ){
                NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshRuntimeMeshCache: source meshlet {} position ref {} is invalid")
                    , meshletIndex
                    , localPositionIndex
                );
                return false;
            }

            MeshletPositionStreamRef runtimeRef = sourceRef;
            runtimeRef.position = static_cast<u32>(runtimePositionRefs.size());
            instance.restPositions.push_back(mesh.positionStream()[sourceRef.position]);
            runtimePositionRefs.push_back(runtimeRef);
        }

        for(u32 localAttributeIndex = 0u; localAttributeIndex < MeshletAttributeCount(sourceMeshlet); ++localAttributeIndex){
            MeshletAttributeStreamRef sourceRef;
            if(
                !DecodeMeshletAttributeRef(
                    mesh.meshletAttributeRefDeltas().data(),
                    mesh.meshletAttributeRefDeltas().size(),
                    sourceMeshlet,
                    localAttributeIndex,
                    sourceRef
                )
                || !MeshletAttributeRefInRange(
                    sourceRef,
                    mesh.normalStream().size(),
                    mesh.tangentStream().size(),
                    mesh.uv0Stream().size(),
                    mesh.colorStream().size()
                )
            ){
                NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshRuntimeMeshCache: source meshlet {} attribute ref {} is invalid")
                    , meshletIndex
                    , localAttributeIndex
                );
                return false;
            }

            instance.restNormals.push_back(mesh.normalStream()[sourceRef.normal]);
            instance.restTangents.push_back(mesh.tangentStream()[sourceRef.tangent]);

            MeshletAttributeStreamRef runtimeRef = sourceRef;
            runtimeRef.normal = static_cast<u32>(runtimeAttributeRefs.size());
            runtimeRef.tangent = static_cast<u32>(runtimeAttributeRefs.size());
            runtimeAttributeRefs.push_back(runtimeRef);
        }
    }

    if(!ResolveAttributeSkins(
        instance.meshlets,
        runtimePositionRefs,
        instance.meshletLocalVertexRefs,
        runtimeAttributeRefs.size(),
        instance.attributeSkins
    ))
        return false;

    if(!EncodeMeshletRefDeltas(
        instance.meshlets,
        runtimePositionRefs,
        runtimeAttributeRefs,
        instance.meshletPositionRefDeltas,
        instance.meshletAttributeRefDeltas,
        true,
        [&](const usize meshletIndex, const tchar* reason){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshRuntimeMeshCache: runtime meshlet {} {}")
                , meshletIndex
                , reason
            );
            return false;
        }
    ))
        return false;

    instance.meshletPositionRefCount = static_cast<u32>(runtimePositionRefs.size());
    instance.meshletAttributeRefCount = static_cast<u32>(runtimeAttributeRefs.size());
    return BuildCapSourceTriangles(instance);
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

