// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<
    typename MeshT,
    typename PositionRefT
>
static bool TestDecodeMeshletPositionRef(
    const MeshT& mesh,
    const NWB::Impl::MeshletDesc& meshlet,
    const u32 localPositionIndex,
    PositionRefT& outRef
){
    return NWB::Impl::DecodeMeshletPositionRef(
        mesh.meshletPositionRefDeltas().data(),
        mesh.meshletPositionRefDeltas().size(),
        meshlet,
        localPositionIndex,
        NWB::Core::Mesh::MeshClassUsesSkinning(mesh.meshClass()),
        outRef
    );
}

template<
    typename MeshT,
    typename AttributeRefT
>
static bool TestDecodeMeshletAttributeRef(
    const MeshT& mesh,
    const NWB::Impl::MeshletDesc& meshlet,
    const u32 localAttributeIndex,
    AttributeRefT& outRef
){
    return NWB::Impl::DecodeMeshletAttributeRef(
        mesh.meshletAttributeRefDeltas().data(),
        mesh.meshletAttributeRefDeltas().size(),
        meshlet,
        localAttributeIndex,
        outRef
    );
}

template<
    typename MeshT,
    typename PositionStreamT,
    typename NormalStreamT,
    typename LocalRefVectorT
>
static bool TestMeshletHasPositionNormalValue(
    const MeshT& mesh,
    const NWB::Impl::MeshletDesc& meshlet,
    const PositionStreamT& positions,
    const NormalStreamT& normals,
    const LocalRefVectorT& localRefs,
    const Float3U& expectedPosition,
    const Float4U& expectedNormal
){
    for(u32 localVertexIndex = 0u; localVertexIndex < NWB::Impl::MeshletVertexCount(meshlet); ++localVertexIndex){
        const NWB::Impl::MeshletLocalVertexRef& localRef = localRefs[meshlet.localVertexOffset + localVertexIndex];
        NWB::Impl::MeshletPositionStreamRef positionRef;
        NWB::Impl::MeshletAttributeStreamRef attributeRef;
        if(
            !TestDecodeMeshletPositionRef(mesh, meshlet, localRef.localDeformedPosition, positionRef)
            || !TestDecodeMeshletAttributeRef(mesh, meshlet, localRef.localAttribute, attributeRef)
        )
            return false;

        const Float3U& position = positions[positionRef.position];
        const Float4U normal = LoadHalf4U(normals[attributeRef.normal]);
        if(
            position.x == expectedPosition.x
            && position.y == expectedPosition.y
            && position.z == expectedPosition.z
            && normal.x == expectedNormal.x
            && normal.y == expectedNormal.y
            && normal.z == expectedNormal.z
            && normal.w == expectedNormal.w
        )
            return true;
    }

    return false;
}

template<typename MeshT>
static bool TestMeshletPositionRefsAreFirstUseOrdered(
    const NWB::Impl::MeshletDesc& meshlet,
    const MeshT& mesh
){
    for(u32 localPositionIndex = 0u; localPositionIndex < NWB::Impl::MeshletPositionCount(meshlet); ++localPositionIndex){
        NWB::Impl::MeshletPositionStreamRef ref;
        if(!TestDecodeMeshletPositionRef(mesh, meshlet, localPositionIndex, ref))
            return false;
        if(ref.position != localPositionIndex)
            return false;
    }

    return true;
}

template<typename MeshT, typename SelectorT>
static bool TestMeshletAttributeRefsAreFirstUseOrdered(
    const NWB::Impl::MeshletDesc& meshlet,
    const MeshT& mesh,
    SelectorT&& selector
){
    u8 seen[NWB::Impl::s_MeshMaxMeshletVertices] = {};
    u32 nextStreamIndex = 0u;
    for(u32 localAttributeIndex = 0u; localAttributeIndex < NWB::Impl::MeshletAttributeCount(meshlet); ++localAttributeIndex){
        NWB::Impl::MeshletAttributeStreamRef ref;
        if(!TestDecodeMeshletAttributeRef(mesh, meshlet, localAttributeIndex, ref))
            return false;
        const u32 streamIndex = selector(ref);
        if(streamIndex >= NWB::Impl::s_MeshMaxMeshletVertices)
            return false;
        if(seen[streamIndex])
            continue;

        if(streamIndex != nextStreamIndex)
            return false;
        seen[streamIndex] = 1u;
        ++nextStreamIndex;
    }

    return true;
}

template<typename MeshT>
[[nodiscard]] static usize TestMeshletLogicalPositionRefCount(const MeshT& mesh){
    usize count = 0u;
    for(const NWB::Impl::MeshletDesc& meshlet : mesh.meshlets())
        count += NWB::Impl::MeshletPositionCount(meshlet);
    return count;
}

template<typename MeshT>
[[nodiscard]] static usize TestMeshletLogicalAttributeRefCount(const MeshT& mesh){
    usize count = 0u;
    for(const NWB::Impl::MeshletDesc& meshlet : mesh.meshlets())
        count += NWB::Impl::MeshletAttributeCount(meshlet);
    return count;
}

template<typename MeshT>
[[nodiscard]] static usize TestMeshletCompressedReferencePayloadBytes(const MeshT& mesh){
    return mesh.meshlets().size() * sizeof(NWB::Impl::MeshletDesc)
        + mesh.meshletPositionRefDeltas().size()
        + mesh.meshletAttributeRefDeltas().size()
    ;
}

template<typename MeshT>
[[nodiscard]] static usize TestMeshletUncompressedReferencePayloadBytes(const MeshT& mesh){
    static constexpr usize s_PreCompressionMeshletDescBytes = sizeof(u32) * 5u;
    return mesh.meshlets().size() * s_PreCompressionMeshletDescBytes
        + TestMeshletLogicalPositionRefCount(mesh) * sizeof(NWB::Impl::MeshletPositionStreamRef)
        + TestMeshletLogicalAttributeRefCount(mesh) * sizeof(NWB::Impl::MeshletAttributeStreamRef)
    ;
}

template<typename MeshT>
[[nodiscard]] static usize TestMeshletCompressedReferenceBandwidthBytes(const MeshT& mesh){
    return mesh.meshletPositionRefDeltas().size() + mesh.meshletAttributeRefDeltas().size();
}

template<typename MeshT>
[[nodiscard]] static usize TestMeshletUncompressedReferenceBandwidthBytes(const MeshT& mesh){
    return TestMeshletLogicalPositionRefCount(mesh) * sizeof(NWB::Impl::MeshletPositionStreamRef)
        + TestMeshletLogicalAttributeRefCount(mesh) * sizeof(NWB::Impl::MeshletAttributeStreamRef)
    ;
}

template<typename MeshT>
[[nodiscard]] static bool TestMeshletReferenceCompressionShrinksPayload(const MeshT& mesh){
    return TestMeshletCompressedReferencePayloadBytes(mesh) < TestMeshletUncompressedReferencePayloadBytes(mesh);
}

template<typename MeshT>
[[nodiscard]] static bool TestMeshletReferenceCompressionBandwidthNeutralOrBetter(const MeshT& mesh){
    return TestMeshletCompressedReferenceBandwidthBytes(mesh) <= TestMeshletUncompressedReferenceBandwidthBytes(mesh);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

