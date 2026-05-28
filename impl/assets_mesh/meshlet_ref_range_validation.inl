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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

