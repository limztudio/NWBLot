// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "geometry_payload.h"
#include "meshlet_ref_decode.h"
#include "meshlet_triangle_visit.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Per-corner trace attributes parallel to the triangle index buffer; preserves smooth vs hard edges (no position-indexed normal collapse).
static_assert(sizeof(AttribGpu) == sizeof(Half4U) + sizeof(Float2U), "AttribGpu layout drifted");
static_assert(sizeof(AttribGpu) == s_AttribGpuByteSize, "AttribGpu must stay 16 bytes for the shadow trace attribute buffer");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Flat per-corner attribute buffer from meshlet streams (mirrors BuildMeshletTriangleIndices decode chain).
// PrimitiveIndex * 3 + corner fetch; raw streams cover cooked + skinned instances.
[[nodiscard]] inline Expected<Vector<AttribGpu, Core::Alloc::ScratchArena>> BuildMeshletTriangleAttributes(
    const Span<const MeshletDesc> meshlets,
    const Span<const MeshletLocalVertexRef> localVertexRefs,
    const Span<const u8> attributeRefDeltas,
    const Span<const u8> primitiveIndices,
    const Span<const Half4U> normalStream,
    const Span<const Float2U> uv0Stream,
    Core::Alloc::ScratchArena& scratchArena
){
    const u8* const attributeDeltaBytes = attributeRefDeltas.data();
    const usize attributeDeltaByteCount = attributeRefDeltas.size();
    const usize primitiveIndexCount = primitiveIndices.size();
    const usize normalCount = normalStream.size();
    const usize uv0Count = uv0Stream.size();

    Vector<AttribGpu, Core::Alloc::ScratchArena> outAttributes(primitiveIndexCount, scratchArena);

    if(!ForEachMeshletTriangleCorner(meshlets, localVertexRefs, primitiveIndices, [&](const MeshletDesc& meshlet, const usize primitiveByte, const MeshletLocalVertexRef& localVertexRef)noexcept -> b8 {
            const u32 localAttributeIndex = static_cast<u32>(localVertexRef.localAttribute);
            const auto attributeRef = DecodeMeshletAttributeRef(attributeDeltaBytes, attributeDeltaByteCount, meshlet, localAttributeIndex);
            if(!attributeRef)
                return false;
            if(static_cast<usize>(attributeRef->normal) >= normalCount || static_cast<usize>(attributeRef->uv0) >= uv0Count)
                return false;

            AttribGpu& attribute = outAttributes[primitiveByte];
            attribute.normal = normalStream[attributeRef->normal];
            attribute.uv0 = uv0Stream[attributeRef->uv0];
            return true;
        }
    ))
        return MakeUnexpected(Failure{});
    return outAttributes;
}

[[nodiscard]] inline Expected<Vector<AttribGpu, Core::Alloc::ScratchArena>> BuildMeshletTriangleAttributes(const MeshGeometryPayload& payload, Core::Alloc::ScratchArena& scratchArena){
    return BuildMeshletTriangleAttributes(payload.meshlets(), payload.meshletLocalVertexRefs(), payload.meshletAttributeRefDeltas(), payload.meshletPrimitiveIndices(), payload.normalStream(), payload.uv0Stream(), scratchArena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

