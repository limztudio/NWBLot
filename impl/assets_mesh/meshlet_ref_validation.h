// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "asset.h"
#include "meshlet_payload_packing.h"

#include <core/common/log.h>


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Mesh meshlet reference range and skin resolution.


class MeshMeshletRefValidation final : NoCopy{
public:
    [[nodiscard]] static bool MeshletPositionRefInRange(
    const MeshletPositionStreamRef& ref,
    const usize positionCount,
    const usize skinCount,
    const bool skinRequired
    );
    [[nodiscard]] static bool MeshletAttributeRefInRange(
    const MeshletAttributeStreamRef& ref,
    const usize normalCount,
    const usize tangentCount,
    const usize uv0Count,
    const usize colorCount
    );
    template<
    typename MeshletContainer,
    typename LocalVertexRefContainer,
    typename AttributeSkinContainer,
    typename SkinResolver,
    typename ConflictHandler,
    typename UnreferencedHandler
    >
    [[nodiscard]] static bool ResolveMeshletAttributeSkinsFromLocalVertices(
    const MeshletContainer& meshlets,
    const LocalVertexRefContainer& localVertexRefs,
    const usize attributeCount,
    AttributeSkinContainer& outAttributeSkins,
    SkinResolver resolveSkin,
    ConflictHandler onConflict,
    UnreferencedHandler onUnreferenced
    );
    template<
    typename MeshletContainer,
    typename PositionRefContainer,
    typename LocalVertexRefContainer,
    typename AttributeSkinContainer,
    typename ConflictHandler,
    typename UnreferencedHandler
    >
    [[nodiscard]] static bool ResolveMeshletAttributeSkins(
    const MeshletContainer& meshlets,
    const PositionRefContainer& positionRefs,
    const LocalVertexRefContainer& localVertexRefs,
    const usize attributeCount,
    AttributeSkinContainer& outAttributeSkins,
    ConflictHandler onConflict,
    UnreferencedHandler onUnreferenced
    );



public:
    MeshMeshletRefValidation() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<
    typename MeshletContainer,
    typename LocalVertexRefContainer,
    typename AttributeSkinContainer,
    typename SkinResolver,
    typename ConflictHandler,
    typename UnreferencedHandler
>
[[nodiscard]] bool MeshMeshletRefValidation::ResolveMeshletAttributeSkinsFromLocalVertices(
    const MeshletContainer& meshlets,
    const LocalVertexRefContainer& localVertexRefs,
    const usize attributeCount,
    AttributeSkinContainer& outAttributeSkins,
    SkinResolver resolveSkin,
    ConflictHandler onConflict,
    UnreferencedHandler onUnreferenced
){
    outAttributeSkins.clear();
    outAttributeSkins.resize(attributeCount, s_MeshMissingStreamIndex);

    usize meshletPositionBase = 0u;
    usize meshletAttributeBase = 0u;
    for(usize meshletIndex = 0u; meshletIndex < meshlets.size(); ++meshletIndex){
        const MeshletDesc& meshlet = meshlets[meshletIndex];
        const u32 vertexCount = MeshletVertexCount(meshlet);
        for(u32 localVertexIndex = 0u; localVertexIndex < vertexCount; ++localVertexIndex){
            const MeshletLocalVertexRef& localVertexRef = localVertexRefs[meshlet.localVertexOffset + localVertexIndex];
            const usize attributeIndex = meshletAttributeBase + localVertexRef.localAttribute;
            u32 skinIndex = s_MeshMissingStreamIndex;
            if(!resolveSkin(meshletIndex, meshlet, meshletPositionBase, localVertexRef.localDeformedPosition, skinIndex))
                return false;

            u32& attributeSkin = outAttributeSkins[attributeIndex];
            if(attributeSkin == s_MeshMissingStreamIndex){
                attributeSkin = skinIndex;
                continue;
            }
            if(attributeSkin == skinIndex)
                continue;
            return onConflict(meshletIndex, attributeIndex, attributeSkin, skinIndex);
        }

        meshletPositionBase += MeshletPositionCount(meshlet);
        meshletAttributeBase += MeshletAttributeCount(meshlet);
    }

    for(usize attributeIndex = 0u; attributeIndex < outAttributeSkins.size(); ++attributeIndex){
        if(outAttributeSkins[attributeIndex] != s_MeshMissingStreamIndex)
            continue;
        return onUnreferenced(attributeIndex);
    }

    return true;
}


template<
    typename MeshletContainer,
    typename PositionRefContainer,
    typename LocalVertexRefContainer,
    typename AttributeSkinContainer,
    typename ConflictHandler,
    typename UnreferencedHandler
>
[[nodiscard]] bool MeshMeshletRefValidation::ResolveMeshletAttributeSkins(
    const MeshletContainer& meshlets,
    const PositionRefContainer& positionRefs,
    const LocalVertexRefContainer& localVertexRefs,
    const usize attributeCount,
    AttributeSkinContainer& outAttributeSkins,
    ConflictHandler onConflict,
    UnreferencedHandler onUnreferenced
){
    return ResolveMeshletAttributeSkinsFromLocalVertices(
        meshlets,
        localVertexRefs,
        attributeCount,
        outAttributeSkins,
        [&](const usize, const MeshletDesc&, const usize meshletPositionBase, const u32 localPositionIndex, u32& outSkin){
            outSkin = positionRefs[meshletPositionBase + localPositionIndex].skin;
            return true;
        },
        onConflict,
        onUnreferenced
    );
}


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
