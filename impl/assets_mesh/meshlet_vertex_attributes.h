// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "geometry_payload.h"
#include "meshlet_ref_decode.h"
#include "meshlet_triangle_visit.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Per-corner trace attributes parallel to the triangle index buffer; preserves smooth vs hard edges (no
// position-indexed normal collapse).
struct AttribGpu{
    Half4U normal;
    Float2U uv0;
};
static_assert(IsStandardLayout_V<AttribGpu>, "AttribGpu must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<AttribGpu>, "AttribGpu must stay binary-serializable");
static_assert(sizeof(AttribGpu) == sizeof(Half4U) + sizeof(Float2U), "AttribGpu layout drifted");
static constexpr usize s_AttribGpuByteSize = 16u;
static_assert(sizeof(AttribGpu) == s_AttribGpuByteSize, "AttribGpu must stay 16 bytes for the shadow trace attribute buffer");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Flat per-corner attribute buffer from meshlet streams (mirrors BuildMeshletTriangleIndices decode chain).
// PrimitiveIndex * 3 + corner fetch; raw streams cover cooked + skinned instances.
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

