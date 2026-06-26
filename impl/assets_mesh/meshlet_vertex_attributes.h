// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "geometry_payload.h"
#include "meshlet_ref_decode.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Flat per-vertex shadow-trace attribute record, parallel to the reconstructed positionStream-space triangle index
// buffer (see BuildMeshletTriangleIndices). Element[p] holds the attributes of the vertex whose deformed position
// resolves to global position index p, so a trace can fetch + barycentric-interpolate normal/uv0 with the SAME
// i0/i1/i2 it already loads for positions. normal copies the packed Half4U normal stream; uv0 copies the Float2U
// uv0 stream (both streams already arrive in these formats, so packing is a lossless copy).
struct AttribGpu{
    Half4U normal;
    Float2U uv0;
};
static_assert(IsStandardLayout_V<AttribGpu>, "AttribGpu must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<AttribGpu>, "AttribGpu must stay binary-serializable");
static_assert(sizeof(AttribGpu) == sizeof(Half4U) + sizeof(Float2U), "AttribGpu layout drifted");
static_assert(sizeof(AttribGpu) == 16u, "AttribGpu must stay 16 bytes for the shadow trace attribute buffer");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Reconstructs a flat positionStream-space per-vertex attribute buffer (AttribGpu, one per position) from the
// meshlet-packed streams. Decode chain mirrors nwbMeshBuildMeshletVertex / BuildMeshletTriangleIndices: each
// meshletLocalVertexRef pairs localDeformedPosition -> DecodeMeshletPositionRef -> global position index p with
// localAttribute -> DecodeMeshletAttributeRef -> normal/uv0 stream indices; the resolved normal+uv0 are gathered
// into attributeOut[p]. Output count equals positionCount. UV seams (multiple attributes per position) resolve to
// the last writer, which is acceptable for smooth transmittance. Takes the raw streams so it works for both the
// cooked asset payload and the runtime/skinned mesh instance.
template<
    typename MeshletContainer,
    typename LocalVertexRefContainer,
    typename PositionDeltaContainer,
    typename AttributeDeltaContainer,
    typename NormalContainer,
    typename Uv0Container,
    typename AttributeOutContainer
>
[[nodiscard]] bool BuildMeshletVertexAttributes(
    const MeshletContainer& meshlets,
    const LocalVertexRefContainer& localVertexRefs,
    const PositionDeltaContainer& positionRefDeltas,
    const AttributeDeltaContainer& attributeRefDeltas,
    const NormalContainer& normalStream,
    const Uv0Container& uv0Stream,
    const usize positionCount,
    AttributeOutContainer& outAttributes
){
    const u8* const positionDeltaBytes = positionRefDeltas.data();
    const usize positionDeltaByteCount = positionRefDeltas.size();
    const u8* const attributeDeltaBytes = attributeRefDeltas.data();
    const usize attributeDeltaByteCount = attributeRefDeltas.size();
    const usize localVertexRefCount = localVertexRefs.size();
    const usize normalCount = normalStream.size();
    const usize uv0Count = uv0Stream.size();

    outAttributes.assign(positionCount, AttribGpu{});

    for(usize meshletIndex = 0u; meshletIndex < meshlets.size(); ++meshletIndex){
        const MeshletDesc& meshlet = meshlets[meshletIndex];
        const u32 vertexCount = MeshletVertexCount(meshlet);
        const bool skinRequired = meshlet.skinBase != s_MeshMissingStreamIndex;

        for(u32 localVertex = 0u; localVertex < vertexCount; ++localVertex){
            const usize localVertexRefIndex = static_cast<usize>(meshlet.localVertexOffset) + localVertex;
            if(localVertexRefIndex >= localVertexRefCount)
                return false;

            const MeshletLocalVertexRef& localVertexRef = localVertexRefs[localVertexRefIndex];

            const u32 localPositionIndex = static_cast<u32>(localVertexRef.localDeformedPosition);
            MeshletPositionStreamRef positionRef;
            if(!DecodeMeshletPositionRef(positionDeltaBytes, positionDeltaByteCount, meshlet, localPositionIndex, skinRequired, positionRef))
                return false;
            if(static_cast<usize>(positionRef.position) >= positionCount)
                return false;

            const u32 localAttributeIndex = static_cast<u32>(localVertexRef.localAttribute);
            MeshletAttributeStreamRef attributeRef;
            if(!DecodeMeshletAttributeRef(attributeDeltaBytes, attributeDeltaByteCount, meshlet, localAttributeIndex, attributeRef))
                return false;
            if(static_cast<usize>(attributeRef.normal) >= normalCount || static_cast<usize>(attributeRef.uv0) >= uv0Count)
                return false;

            AttribGpu& attribute = outAttributes[static_cast<usize>(positionRef.position)];
            attribute.normal = normalStream[attributeRef.normal];
            attribute.uv0 = uv0Stream[attributeRef.uv0];
        }
    }

    return true;
}

template<typename AttributeOutContainer>
[[nodiscard]] bool BuildMeshletVertexAttributes(const MeshGeometryPayload& payload, AttributeOutContainer& outAttributes){
    return BuildMeshletVertexAttributes(
        payload.meshlets(),
        payload.meshletLocalVertexRefs(),
        payload.meshletPositionRefDeltas(),
        payload.meshletAttributeRefDeltas(),
        payload.normalStream(),
        payload.uv0Stream(),
        payload.positionStream().size(),
        outAttributes
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

