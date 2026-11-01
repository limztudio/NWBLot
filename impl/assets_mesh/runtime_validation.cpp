// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "runtime_validation.h"

#include "binary_payload_io.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_mesh_runtime_validation{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr TStringView s_StreamCountLimitReason = NWB_TEXT("exceeds u32 stream count limits");
inline constexpr TStringView s_NonFiniteDataReason = NWB_TEXT("contains non-finite data");
inline constexpr TStringView s_InvalidStreamReason = NWB_TEXT("is invalid");
inline constexpr TStringView s_InvalidEncodedPositionRefReason = NWB_TEXT("has invalid encoded position ref");
inline constexpr TStringView s_InvalidEncodedAttributeRefReason = NWB_TEXT("has invalid encoded attribute ref");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void LogMeshPayloadValidation(const TStringView contextText, const TStringView meshPathText, const TStringView detailText){
    NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mesh '{}' {}"), contextText, meshPathText, detailText);
}

void LogMeshPayloadIndexedValidation(const TStringView contextText, const TStringView meshPathText, const TStringView itemText, const usize itemIndex, const TStringView detailText){
    NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mesh '{}' {} {} {}"), contextText, meshPathText, itemText, itemIndex, detailText);
}

void LogMeshletPayloadValidation(const TStringView contextText, const TStringView meshPathText, const usize meshletIndex, const TStringView detailText){
    NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mesh '{}' meshlet {} {}"), contextText, meshPathText, meshletIndex, detailText);
}

void LogMeshletPrimitivePayloadValidation(const TStringView contextText, const TStringView meshPathText, const usize meshletIndex, const usize primitiveIndex, const TStringView detailText){
    NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mesh '{}' meshlet {} primitive {} {}"), contextText, meshPathText, meshletIndex, primitiveIndex, detailText);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 MeshRuntimeValidation::ValidDirectionVector(const SIMDVectorSingle direction)noexcept{
    return VectorIsFinite(direction, VectorComponentMask::s_XYZ) && Vector3NearEqual(Vector3LengthSq(direction), s_SIMDOne, VectorReplicate(s_DirectionLengthTolerance));
}

[[nodiscard]] b8 MeshRuntimeValidation::ValidTangentVector(const SIMDVectorSingle tangent)noexcept{
    const SIMDVectorSingle direction = VectorSetW(tangent, 0.0f);
    return
        VectorIsFinite(tangent, VectorComponentMask::s_XYZW)
        && Vector3NearEqual(Vector3LengthSq(direction), s_SIMDOne, VectorReplicate(s_DirectionLengthTolerance))
        && Vector4NearEqual(VectorAbs(VectorSplatW(tangent)), s_SIMDOne, VectorReplicate(s_TangentHandednessTolerance))
    ;
}

[[nodiscard]] b8 MeshRuntimeValidation::ValidateMeshStreams(
    const Span<const Float3U> positions,
    const Span<const Half4U> normals,
    const Span<const Half4U> tangents,
    const Span<const Float2U> uv0,
    const Span<const Half4U> colors,
    const TStringView contextText,
    const TStringView meshPathText
){
    if(!FitsU32(positions.size()) || !FitsU32(normals.size()) || !FitsU32(tangents.size()) || !FitsU32(uv0.size()) || !FitsU32(colors.size())){
        __hidden_mesh_runtime_validation::LogMeshPayloadValidation(contextText, meshPathText, __hidden_mesh_runtime_validation::s_StreamCountLimitReason);
        return false;
    }

    for(usize i = 0u; i < positions.size(); ++i){
        if(VectorIsFinite(VectorSetW(LoadFloat(positions[i]), 0.0f), VectorComponentMask::s_XYZ))
            continue;

        __hidden_mesh_runtime_validation::LogMeshPayloadIndexedValidation(contextText, meshPathText, MeshAssetBinaryPayload::s_PositionsStreamLabel, i, __hidden_mesh_runtime_validation::s_NonFiniteDataReason);
        return false;
    }
    for(usize i = 0u; i < normals.size(); ++i){
        if(ValidDirectionVector(VectorSetW(LoadHalf(normals[i]), 0.0f)))
            continue;

        __hidden_mesh_runtime_validation::LogMeshPayloadIndexedValidation(contextText, meshPathText, MeshAssetBinaryPayload::s_NormalsStreamLabel, i, __hidden_mesh_runtime_validation::s_InvalidStreamReason);
        return false;
    }
    for(usize i = 0u; i < tangents.size(); ++i){
        if(ValidTangentVector(LoadHalf(tangents[i])))
            continue;

        __hidden_mesh_runtime_validation::LogMeshPayloadIndexedValidation(contextText, meshPathText, MeshAssetBinaryPayload::s_TangentsStreamLabel, i, __hidden_mesh_runtime_validation::s_InvalidStreamReason);
        return false;
    }
    for(usize i = 0u; i < uv0.size(); ++i){
        if(VectorIsFinite(VectorSetW(VectorSetZ(LoadFloat(uv0[i]), 0.0f), 0.0f), VectorComponentMask::s_XY))
            continue;

        __hidden_mesh_runtime_validation::LogMeshPayloadIndexedValidation(contextText, meshPathText, MeshAssetBinaryPayload::s_Uv0StreamLabel, i, __hidden_mesh_runtime_validation::s_NonFiniteDataReason);
        return false;
    }
    for(usize i = 0u; i < colors.size(); ++i){
        if(VectorIsFinite(LoadHalf(colors[i]), VectorComponentMask::s_XYZW))
            continue;

        __hidden_mesh_runtime_validation::LogMeshPayloadIndexedValidation(contextText, meshPathText, MeshAssetBinaryPayload::s_ColorsStreamLabel, i, __hidden_mesh_runtime_validation::s_NonFiniteDataReason);
        return false;
    }

    return true;
}

[[nodiscard]] b8 MeshRuntimeValidation::ValidateMeshletPayload(
    const Span<const u8> positionRefDeltas,
    const Span<const u8> attributeRefDeltas,
    const Span<const MeshletLocalVertexRef> localVertexRefs,
    const Span<const MeshletDesc> meshlets,
    const Span<const MeshletBounds> meshletBounds,
    const Span<const u8> meshletPrimitiveIndices,
    const usize positionCount,
    const usize normalCount,
    const usize tangentCount,
    const usize uv0Count,
    const usize colorCount,
    const TStringView contextText,
    const TStringView meshPathText
){
    if(meshlets.empty() || meshletBounds.size() != meshlets.size()){
        __hidden_mesh_runtime_validation::LogMeshPayloadValidation(contextText, meshPathText, NWB_TEXT("has incomplete meshlet payload"));
        return false;
    }
    if(meshlets.size() > Limit<u32>::s_Max / s_MeshletBoundsStride){
        __hidden_mesh_runtime_validation::LogMeshPayloadValidation(contextText, meshPathText, NWB_TEXT("meshlet bounds exceed u32 byte-address limits"));
        return false;
    }
    if(!FitsU32(positionRefDeltas.size()) || !FitsU32(attributeRefDeltas.size())){
        __hidden_mesh_runtime_validation::LogMeshPayloadValidation(contextText, meshPathText, __hidden_mesh_runtime_validation::s_StreamCountLimitReason);
        return false;
    }

    usize expectedLocalVertexRefCount = 0u;
    usize expectedPositionRefByteCount = 0u;
    usize expectedAttributeRefByteCount = 0u;
    usize expectedPrimitiveIndexCount = 0u;
    for(usize meshletIndex = 0u; meshletIndex < meshlets.size(); ++meshletIndex){
        const MeshletDesc& meshlet = meshlets[meshletIndex];
        const u32 vertexCount = MeshletVertexCount(meshlet);
        const u32 primitiveCount = MeshletPrimitiveCount(meshlet);
        const u32 encodedPositionCount = MeshletPositionCount(meshlet);
        const u32 attributeCount = MeshletAttributeCount(meshlet);
        if(vertexCount == 0u || vertexCount > s_MeshMaxMeshletVertices){
            __hidden_mesh_runtime_validation::LogMeshletPayloadValidation(contextText, meshPathText, meshletIndex, NWB_TEXT("has invalid vertex count"));
            return false;
        }
        if(primitiveCount == 0u || primitiveCount > s_MeshMaxMeshletTriangles){
            __hidden_mesh_runtime_validation::LogMeshletPayloadValidation(contextText, meshPathText, meshletIndex, NWB_TEXT("has invalid primitive count"));
            return false;
        }
        if(encodedPositionCount == 0u || encodedPositionCount > s_MeshMaxMeshletVertices){
            __hidden_mesh_runtime_validation::LogMeshletPayloadValidation(contextText, meshPathText, meshletIndex, NWB_TEXT("has invalid deformed position count"));
            return false;
        }
        if(attributeCount == 0u || attributeCount > s_MeshMaxMeshletVertices){
            __hidden_mesh_runtime_validation::LogMeshletPayloadValidation(contextText, meshPathText, meshletIndex, NWB_TEXT("has invalid attribute count"));
            return false;
        }
        if(meshlet.localVertexOffset != expectedLocalVertexRefCount || meshlet.primitiveOffset != expectedPrimitiveIndexCount || meshlet.positionRefOffset != expectedPositionRefByteCount || meshlet.attributeRefOffset != expectedAttributeRefByteCount){
            __hidden_mesh_runtime_validation::LogMeshletPayloadValidation(contextText, meshPathText, meshletIndex, NWB_TEXT("has non-contiguous offsets"));
            return false;
        }

        usize encodedPositionBytes = 0u;
        usize encodedAttributeBytes = 0u;
        if(!TryComputeMeshletEncodedPositionRefByteCount(meshlet, false, encodedPositionBytes) || !TryComputeMeshletEncodedAttributeRefByteCount(meshlet, encodedAttributeBytes)){
            __hidden_mesh_runtime_validation::LogMeshletPayloadValidation(contextText, meshPathText, meshletIndex, NWB_TEXT("has invalid ref encoding width"));
            return false;
        }
        if(encodedPositionBytes > Limit<usize>::s_Max - expectedPositionRefByteCount || encodedAttributeBytes > Limit<usize>::s_Max - expectedAttributeRefByteCount){
            __hidden_mesh_runtime_validation::LogMeshletPayloadValidation(contextText, meshPathText, meshletIndex, NWB_TEXT("encoded ref byte counts overflow"));
            return false;
        }

        expectedLocalVertexRefCount += vertexCount;
        expectedPositionRefByteCount += encodedPositionBytes;
        expectedAttributeRefByteCount += encodedAttributeBytes;
        expectedPrimitiveIndexCount += static_cast<usize>(primitiveCount) * s_MeshletTriangleIndexCount;
        if(expectedLocalVertexRefCount > localVertexRefs.size() || expectedPositionRefByteCount > positionRefDeltas.size() || expectedAttributeRefByteCount > attributeRefDeltas.size() || expectedPrimitiveIndexCount > meshletPrimitiveIndices.size()){
            __hidden_mesh_runtime_validation::LogMeshletPayloadValidation(contextText, meshPathText, meshletIndex, NWB_TEXT("exceeds meshlet stream bounds"));
            return false;
        }

        const MeshletBounds& bounds = meshletBounds[meshletIndex];
        const SIMDVectorSingle sphere = LoadFloat(bounds.sphere);
        if(!VectorIsFinite(sphere, VectorComponentMask::s_XYZW) || VectorGetW(sphere) < 0.0f){
            __hidden_mesh_runtime_validation::LogMeshletPayloadValidation(contextText, meshPathText, meshletIndex, NWB_TEXT("has invalid bounds"));
            return false;
        }
        if(MeshletConeFlags(bounds) & ~s_MeshletConeFlagEnabled){
            __hidden_mesh_runtime_validation::LogMeshletPayloadValidation(contextText, meshPathText, meshletIndex, NWB_TEXT("has invalid cone flags"));
            return false;
        }
        if(MeshletConeEnabled(bounds) && MeshletConePackedCutoff(bounds) == 0u){
            __hidden_mesh_runtime_validation::LogMeshletPayloadValidation(contextText, meshPathText, meshletIndex, NWB_TEXT("has invalid cone cutoff"));
            return false;
        }

        for(u32 localPositionIndex = 0u; localPositionIndex < encodedPositionCount; ++localPositionIndex){
            MeshletPositionStreamRef ref;
            if(TryDecodeMeshletPositionRef(positionRefDeltas, meshlet, localPositionIndex, false, ref) && MeshletPositionRefInRange(ref, positionCount, 0u, false))
                continue;

            __hidden_mesh_runtime_validation::LogMeshletPayloadValidation(contextText, meshPathText, meshletIndex, __hidden_mesh_runtime_validation::s_InvalidEncodedPositionRefReason);
            return false;
        }

        for(u32 localAttributeIndex = 0u; localAttributeIndex < attributeCount; ++localAttributeIndex){
            MeshletAttributeStreamRef ref;
            if(TryDecodeMeshletAttributeRef(attributeRefDeltas, meshlet, localAttributeIndex, ref) && MeshletAttributeRefInRange(ref, normalCount, tangentCount, uv0Count, colorCount))
                continue;

            __hidden_mesh_runtime_validation::LogMeshletPayloadValidation(contextText, meshPathText, meshletIndex, __hidden_mesh_runtime_validation::s_InvalidEncodedAttributeRefReason);
            return false;
        }
        for(u32 localVertexIndex = 0u; localVertexIndex < vertexCount; ++localVertexIndex){
            const MeshletLocalVertexRef& ref = localVertexRefs[meshlet.localVertexOffset + localVertexIndex];
            if(ref.localDeformedPosition < encodedPositionCount && ref.localAttribute < attributeCount)
                continue;

            __hidden_mesh_runtime_validation::LogMeshletPayloadValidation(contextText, meshPathText, meshletIndex, NWB_TEXT("local vertex ref is out of range"));
            return false;
        }
        for(u32 primitiveIndex = 0u; primitiveIndex < primitiveCount; ++primitiveIndex){
            const usize primitiveOffset = meshlet.primitiveOffset + static_cast<usize>(primitiveIndex) * s_MeshletTriangleIndexCount;
            for(usize corner = 0u; corner < s_MeshletTriangleIndexCount; ++corner){
                if(meshletPrimitiveIndices[primitiveOffset + corner] < vertexCount)
                    continue;

                __hidden_mesh_runtime_validation::LogMeshletPrimitivePayloadValidation(contextText, meshPathText, meshletIndex, primitiveIndex, NWB_TEXT("has an out-of-range local vertex"));
                return false;
            }
        }
    }

    if(expectedLocalVertexRefCount != localVertexRefs.size() || expectedPositionRefByteCount != positionRefDeltas.size() || expectedAttributeRefByteCount != attributeRefDeltas.size() || expectedPrimitiveIndexCount != meshletPrimitiveIndices.size()){
        __hidden_mesh_runtime_validation::LogMeshPayloadValidation(contextText, meshPathText, NWB_TEXT("meshlet streams contain trailing data"));
        return false;
    }

    return true;
}

[[nodiscard]] b8 MeshRuntimeValidation::ValidateSharedMeshPayload(
    const Span<const Float3U> positions,
    const Span<const Half4U> normals,
    const Span<const Half4U> tangents,
    const Span<const Float2U> uv0,
    const Span<const Half4U> colors,
    const Span<const u8> positionRefDeltas,
    const Span<const u8> attributeRefDeltas,
    const Span<const MeshletLocalVertexRef> localVertexRefs,
    const Span<const MeshletDesc> meshlets,
    const Span<const MeshletBounds> meshletBounds,
    const Span<const u8> meshletPrimitiveIndices,
    const TStringView contextText,
    const TStringView meshPathText
){
    if(!ValidateMeshStreams(positions, normals, tangents, uv0, colors, contextText, meshPathText))
        return false;

    return ValidateMeshletPayload(positionRefDeltas, attributeRefDeltas, localVertexRefs, meshlets, meshletBounds, meshletPrimitiveIndices, positions.size(), normals.size(), tangents.size(), uv0.size(), colors.size(), contextText, meshPathText);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

