
#include "runtime_cache.h"

#include "arena_names.h"

#include <core/assets/manager.h>
#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <impl/assets_mesh/asset.h>
#include <impl/assets_mesh/meshlet_ref_codec.h>
#include <impl/assets_mesh/meshlet_payload_packing.h>
#include <impl/assets_mesh/skin_asset.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_runtime_cache_source{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr RuntimeMeshDirtyFlags s_GpuUploadHandledDirtyFlags =
    RuntimeMeshDirtyFlag::TopologyDirty
    | RuntimeMeshDirtyFlag::AttributesDirty
    | RuntimeMeshDirtyFlag::SkinningInputDirty
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
    MeshSkinningRuntimeInstance::AttributeSkinVector& outAttributeSkins
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
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: source meshlet {} shares an attribute across skin identities")
                , meshletIndex
            );
            return false;
        },
        [](const usize attributeIndex){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: source attribute ref {} is unreferenced")
                , attributeIndex
            );
            return false;
        }
    );
}

[[nodiscard]] bool BuildRuntimeLocalBounds(MeshSkinningRuntimeInstance& instance){
    instance.localBounds = RuntimeMeshLocalBounds{};
    if(instance.restPositions.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: source mesh '{}' has no positions for runtime bounds")
            , StringConvert(instance.sourceName.c_str())
        );
        return false;
    }

    SIMDVector minBounds;
    SIMDVector maxBounds;
    AabbTests::Reset(minBounds, maxBounds);
    for(const Float3U& position : instance.restPositions)
        AabbTests::Expand(LoadFloat(position), minBounds, maxBounds);

    if(!AabbTests::Valid(minBounds, maxBounds)){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: source mesh '{}' has invalid runtime bounds")
            , StringConvert(instance.sourceName.c_str())
        );
        return false;
    }

    StoreFloatInt(VectorSetW(minBounds, 0.0f), s_RuntimeMeshBoundsValidFlag, &instance.localBounds.minBounds);
    StoreFloatInt(VectorSetW(maxBounds, 0.0f), 0, &instance.localBounds.maxBounds);
    return true;
}


template<typename MeshT, typename SkinStreamT>
[[nodiscard]] bool BuildRuntimeZippedPayload(
    const MeshT& mesh,
    const SkinStreamT& skinStream,
    const bool sourceHasSkinRefs,
    MeshSkinningRuntimeInstance& instance
){
    Core::Alloc::ScratchArena scratchArena(SkinningArenaScope::s_ZippedPayloadArena);
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
                    sourceHasSkinRefs,
                    sourceRef
                )
                || !MeshletPositionRefInRange(sourceRef, mesh.positionStream().size(), skinStream.size(), sourceHasSkinRefs)
                || (!sourceHasSkinRefs && sourceRef.position >= skinStream.size())
            ){
                NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: source meshlet {} position ref {} is invalid")
                    , meshletIndex
                    , localPositionIndex
                );
                return false;
            }

            MeshletPositionStreamRef runtimeRef = sourceRef;
            runtimeRef.position = static_cast<u32>(runtimePositionRefs.size());
            if(!sourceHasSkinRefs)
                runtimeRef.skin = sourceRef.position;
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
                NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: source meshlet {} attribute ref {} is invalid")
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
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: runtime meshlet {} {}")
                , meshletIndex
                , reason
            );
            return false;
        }
    ))
        return false;

    instance.meshletPositionRefCount = static_cast<u32>(runtimePositionRefs.size());
    instance.meshletAttributeRefCount = static_cast<u32>(runtimeAttributeRefs.size());
    return BuildRuntimeLocalBounds(instance);
}

[[nodiscard]] Name BuildBindingSourceName(const Name& meshName, const Name& skinName){
    if(!meshName || !skinName)
        return NAME_NONE;

    NameHash derivedHash = {};
    if(
        !BeginDerivedNameHash(meshName, derivedHash)
        || !UpdateDerivedNameHashText(derivedHash, AStringView(":skin:"))
        || !UpdateDerivedNameHashText(derivedHash, AStringView(skinName.c_str()))
    )
        return NAME_NONE;

    return FinishDerivedNameHash(derivedHash);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool MeshSkinningRuntimeCache::ensureRuntimeMesh(Core::ECS::EntityID entity, SkinnedMeshBindingComponent& component){
    const Name sourceName = __hidden_runtime_cache_source::BuildBindingSourceName(component.mesh.name(), component.skin.name());
    if(!sourceName){
        releaseRuntimeMesh(entity);
        component.runtimeMesh.reset();
        return false;
    }

    const auto foundInstance = m_instances.find(entity);
    if(foundInstance != m_instances.end()){
        MeshSkinningRuntimeInstance& instance = foundInstance.value();
        if(instance.sourceName == sourceName){
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

    MeshSkinningSource* source = nullptr;
    if(!ensureSourceLoaded(component.mesh, component.skin, source))
        return false;
    const Mesh* mesh = source ? source->mesh() : nullptr;
    const Skin* skin = source ? source->skin() : nullptr;
    if(!mesh || !skin)
        return false;

    MeshSkinningRuntimeInstance instance(m_arena);
    instance.entity = entity;
    instance.handle = allocateHandle();
    if(!instance.handle.valid()){
        eraseUnusedSource(sourceName);
        return false;
    }
    instance.sourceName = sourceName;
    instance.meshClass = Core::Mesh::MeshClass::Skinned;
    instance.uv0 = mesh->uv0Stream();
    instance.colors = mesh->colorStream();
    instance.skin = skin->influences();
    instance.skeletonJointCount = static_cast<u32>(skin->inverseBindMatrices().size());
    instance.inverseBindMatrices = skin->inverseBindMatrices();
    instance.meshletBounds = mesh->meshletBounds();
    instance.meshletLocalVertexRefs = mesh->meshletLocalVertexRefs();
    instance.meshletPrimitiveIndices = mesh->meshletPrimitiveIndices();
    instance.dirtyFlags = RuntimeMeshDirtyFlag::All;
    if(!__hidden_runtime_cache_source::BuildRuntimeZippedPayload(*mesh, skin->influences(), false, instance)){
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

bool MeshSkinningRuntimeCache::ensureSourceLoaded(
    const Core::Assets::AssetRef<Mesh>& meshAsset,
    const Core::Assets::AssetRef<Skin>& skinAsset,
    MeshSkinningSource*& outSource
){
    outSource = nullptr;

    const Name sourceName = __hidden_runtime_cache_source::BuildBindingSourceName(meshAsset.name(), skinAsset.name());
    if(!sourceName){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: skinning binding source assets are incomplete"));
        return false;
    }

    const auto foundSource = m_sources.find(sourceName);
    if(foundSource != m_sources.end()){
        outSource = &foundSource.value();
        return outSource->mesh() != nullptr && outSource->skin() != nullptr;
    }

    UniquePtr<Core::Assets::IAsset> loadedMeshAsset;
    if(!m_assetManager.loadSync(Mesh::AssetTypeName(), meshAsset.name(), loadedMeshAsset)){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: failed to load mesh '{}' for skinning binding")
            , StringConvert(meshAsset.name().c_str())
        );
        return false;
    }
    if(!loadedMeshAsset || loadedMeshAsset->assetType() != Mesh::AssetTypeName()){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: asset '{}' is not mesh")
            , StringConvert(meshAsset.name().c_str())
        );
        return false;
    }

    UniquePtr<Core::Assets::IAsset> loadedSkinAsset;
    if(!m_assetManager.loadSync(Skin::AssetTypeName(), skinAsset.name(), loadedSkinAsset)){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: failed to load skin '{}' for skinning binding")
            , StringConvert(skinAsset.name().c_str())
        );
        return false;
    }
    if(!loadedSkinAsset || loadedSkinAsset->assetType() != Skin::AssetTypeName()){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: asset '{}' is not skin")
            , StringConvert(skinAsset.name().c_str())
        );
        return false;
    }

    const Skin* loadedSkin = checked_cast<const Skin*>(loadedSkinAsset.get());
    if(loadedSkin->mesh().name() != meshAsset.name()){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: skin '{}' targets a different mesh than '{}'")
            , StringConvert(skinAsset.name().c_str())
            , StringConvert(meshAsset.name().c_str())
        );
        return false;
    }

    MeshSkinningSource source;
    source.sourceName = sourceName;
    source.meshAsset = Move(loadedMeshAsset);
    source.skinAsset = Move(loadedSkinAsset);

    auto result = m_sources.try_emplace(sourceName, Move(source));
    auto it = result.first;
    outSource = &it.value();
    return outSource->mesh() != nullptr && outSource->skin() != nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

