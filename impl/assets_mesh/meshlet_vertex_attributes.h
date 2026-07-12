
#pragma once


#include "geometry_payload.h"
#include "meshlet_ref_decode.h"
#include "meshlet_triangle_visit.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Flat per-triangle-corner shadow/caustic trace attribute record, parallel to the reconstructed triangle index buffer
// (see BuildMeshletTriangleIndices). Element[primitive * 3 + corner] holds the exact normal/uv0 corner attribute that
// rasterization would use for that primitive corner. That preserves smooth edges (shared normal refs) and hard edges
// (same position with distinct normal refs) instead of collapsing them into one position-indexed normal.
struct AttribGpu{
    Half4U normal;
    Float2U uv0;
};
static_assert(IsStandardLayout_V<AttribGpu>, "AttribGpu must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<AttribGpu>, "AttribGpu must stay binary-serializable");
static_assert(sizeof(AttribGpu) == sizeof(Half4U) + sizeof(Float2U), "AttribGpu layout drifted");
static_assert(sizeof(AttribGpu) == 16u, "AttribGpu must stay 16 bytes for the shadow trace attribute buffer");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Reconstructs a flat per-triangle-corner attribute buffer (AttribGpu, one per triangle index) from the meshlet-packed
// streams. Decode chain mirrors BuildMeshletTriangleIndices: primitive u8 -> meshletLocalVertexRef.localAttribute ->
// DecodeMeshletAttributeRef -> global normal/uv0 stream indices. Output count/order equals the meshlet primitive
// index count, so shaders can fetch attributes with PrimitiveIndex * 3 + corner. Takes the raw streams so it works for
// both the cooked asset payload and the runtime/skinned mesh instance.
template<
    typename MeshletContainer,
    typename LocalVertexRefContainer,
    typename AttributeDeltaContainer,
    typename PrimitiveContainer,
    typename NormalContainer,
    typename Uv0Container,
    typename AttributeOutContainer
>
[[nodiscard]] bool BuildMeshletTriangleAttributes(
    const MeshletContainer& meshlets,
    const LocalVertexRefContainer& localVertexRefs,
    const AttributeDeltaContainer& attributeRefDeltas,
    const PrimitiveContainer& primitiveIndices,
    const NormalContainer& normalStream,
    const Uv0Container& uv0Stream,
    AttributeOutContainer& outAttributes
){
    const u8* const attributeDeltaBytes = attributeRefDeltas.data();
    const usize attributeDeltaByteCount = attributeRefDeltas.size();
    const usize primitiveIndexCount = primitiveIndices.size();
    const usize normalCount = normalStream.size();
    const usize uv0Count = uv0Stream.size();

    outAttributes.assign(primitiveIndexCount, AttribGpu{});

    return ForEachMeshletTriangleCorner(
        meshlets,
        localVertexRefs,
        primitiveIndices,
        [&](const MeshletDesc& meshlet, const usize primitiveByte, const MeshletLocalVertexRef& localVertexRef) -> bool {
            const u32 localAttributeIndex = static_cast<u32>(localVertexRef.localAttribute);
            MeshletAttributeStreamRef attributeRef;
            if(!DecodeMeshletAttributeRef(attributeDeltaBytes, attributeDeltaByteCount, meshlet, localAttributeIndex, attributeRef))
                return false;
            if(static_cast<usize>(attributeRef.normal) >= normalCount || static_cast<usize>(attributeRef.uv0) >= uv0Count)
                return false;

            AttribGpu& attribute = outAttributes[primitiveByte];
            attribute.normal = normalStream[attributeRef.normal];
            attribute.uv0 = uv0Stream[attributeRef.uv0];
            return true;
        }
    );
}

template<typename AttributeOutContainer>
[[nodiscard]] bool BuildMeshletTriangleAttributes(const MeshGeometryPayload& payload, AttributeOutContainer& outAttributes){
    return BuildMeshletTriangleAttributes(
        payload.meshlets(),
        payload.meshletLocalVertexRefs(),
        payload.meshletAttributeRefDeltas(),
        payload.meshletPrimitiveIndices(),
        payload.normalStream(),
        payload.uv0Stream(),
        outAttributes
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

