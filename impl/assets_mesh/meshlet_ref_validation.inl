// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool MeshletPositionRefInRange(
    const MeshletDeformedPositionRef& ref,
    const usize positionCount,
    const usize skinCount,
    const bool skinRequired
){
    return ref.position < positionCount && ( skinRequired ? ref.skin < skinCount : ref.skin == s_MeshMissingStreamIndex );
}

[[nodiscard]] static bool MeshletAttributeRefInRange(
    const MeshletShadingAttributeRef& ref,
    const usize normalCount,
    const usize tangentCount,
    const usize uv0Count,
    const usize colorCount
){
    return ref.normal < normalCount && ref.tangent < tangentCount && ref.uv0 < uv0Count && ref.color < colorCount;
}

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
){
    outAttributeSkins.clear();
    outAttributeSkins.resize(attributeCount, s_MeshMissingStreamIndex);

    for(usize meshletIndex = 0u; meshletIndex < meshlets.size(); ++meshletIndex){
        const MeshletDesc& meshlet = meshlets[meshletIndex];
        const u32 vertexCount = MeshletVertexCount(meshlet);
        for(u32 localVertexIndex = 0u; localVertexIndex < vertexCount; ++localVertexIndex){
            const MeshletLocalVertexRef& localVertexRef = localVertexRefs[meshlet.localVertexOffset + localVertexIndex];
            const usize attributeIndex = static_cast<usize>(meshlet.attributeOffset) + localVertexRef.localAttribute;
            const usize positionIndex = static_cast<usize>(meshlet.positionOffset) + localVertexRef.localDeformedPosition;
            const u32 skinIndex = positionRefs[positionIndex].skin;
            u32& attributeSkin = outAttributeSkins[attributeIndex];
            if(attributeSkin == s_MeshMissingStreamIndex){
                attributeSkin = skinIndex;
                continue;
            }
            if(attributeSkin == skinIndex)
                continue;
            return onConflict(meshletIndex, attributeIndex, attributeSkin, skinIndex);
        }
    }

    for(usize attributeIndex = 0u; attributeIndex < outAttributeSkins.size(); ++attributeIndex){
        if(outAttributeSkins[attributeIndex] != s_MeshMissingStreamIndex)
            continue;
        return onUnreferenced(attributeIndex);
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
