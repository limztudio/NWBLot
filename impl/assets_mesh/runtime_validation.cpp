// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "runtime_validation.h"

#include "binary_payload_io.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_mesh_validation{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr f32 s_DirectionLengthTolerance = 0.01f;
inline constexpr f32 s_TangentHandednessTolerance = 0.001f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] SIMDVectorSingle MeshRuntimeValidation::MakeMeshPositionVector(const SIMDVectorSingle position)noexcept{
    return VectorSetW(position, 0.0f);
}


[[nodiscard]] SIMDVectorSingle MeshRuntimeValidation::MakeMeshNormalVector(const SIMDVectorSingle normal)noexcept{
    return VectorSetW(normal, 0.0f);
}


[[nodiscard]] SIMDVectorSingle MeshRuntimeValidation::MakeMeshUvVector(const SIMDVectorSingle uv)noexcept{
    return VectorSetW(VectorSetZ(uv, 0.0f), 0.0f);
}


[[nodiscard]] bool MeshRuntimeValidation::ValidDirectionVector(const SIMDVectorSingle direction)noexcept{
    return VectorIsFinite(direction, VectorComponentMask::s_XYZ) && Vector3NearEqual(Vector3LengthSq(direction), s_SIMDOne, VectorReplicate(__hidden_mesh_validation::s_DirectionLengthTolerance));
}


[[nodiscard]] bool MeshRuntimeValidation::ValidTangentVector(const SIMDVectorSingle tangent)noexcept{
    const SIMDVectorSingle direction = VectorSetW(tangent, 0.0f);
    return
        VectorIsFinite(tangent, VectorComponentMask::s_XYZW)
        && Vector3NearEqual(Vector3LengthSq(direction), s_SIMDOne, VectorReplicate(__hidden_mesh_validation::s_DirectionLengthTolerance))
        && Vector4NearEqual(VectorAbs(VectorSplatW(tangent)), s_SIMDOne, VectorReplicate(__hidden_mesh_validation::s_TangentHandednessTolerance))
    ;
}


[[nodiscard]] bool MeshRuntimeValidation::ValidateMeshStreams(
    const Core::Assets::AssetVector<Float3U>& positions,
    const Core::Assets::AssetVector<Half4U>& normals,
    const Core::Assets::AssetVector<Half4U>& tangents,
    const Core::Assets::AssetVector<Float2U>& uv0,
    const Core::Assets::AssetVector<Half4U>& colors,
    const TStringView contextText,
    const TStringView meshPathText
){
    if(!FitsU32(positions.size()) || !FitsU32(normals.size()) || !FitsU32(tangents.size()) || !FitsU32(uv0.size()) || !FitsU32(colors.size())){
        MeshPayloadValidationDiagnostics::LogMeshPayloadValidation(contextText, meshPathText, NWB_TEXT("exceeds u32 stream count limits"));
        return false;
    }

    for(usize i = 0u; i < positions.size(); ++i){
        if(VectorIsFinite(MakeMeshPositionVector(LoadFloat(positions[i])), VectorComponentMask::s_XYZ))
            continue;

        MeshPayloadValidationDiagnostics::LogMeshPayloadIndexedValidation(contextText, meshPathText, MeshAssetBinaryPayload::s_PositionsStreamLabel, i, NWB_TEXT("contains non-finite data"));
        return false;
    }
    for(usize i = 0u; i < normals.size(); ++i){
        if(ValidDirectionVector(MakeMeshNormalVector(LoadHalf(normals[i]))))
            continue;

        MeshPayloadValidationDiagnostics::LogMeshPayloadIndexedValidation(contextText, meshPathText, MeshAssetBinaryPayload::s_NormalsStreamLabel, i, NWB_TEXT("is invalid"));
        return false;
    }
    for(usize i = 0u; i < tangents.size(); ++i){
        if(ValidTangentVector(LoadHalf(tangents[i])))
            continue;

        MeshPayloadValidationDiagnostics::LogMeshPayloadIndexedValidation(contextText, meshPathText, MeshAssetBinaryPayload::s_TangentsStreamLabel, i, NWB_TEXT("is invalid"));
        return false;
    }
    for(usize i = 0u; i < uv0.size(); ++i){
        if(VectorIsFinite(MakeMeshUvVector(LoadFloat(uv0[i])), VectorComponentMask::s_XY))
            continue;

        MeshPayloadValidationDiagnostics::LogMeshPayloadIndexedValidation(contextText, meshPathText, MeshAssetBinaryPayload::s_Uv0StreamLabel, i, NWB_TEXT("contains non-finite data"));
        return false;
    }
    for(usize i = 0u; i < colors.size(); ++i){
        if(VectorIsFinite(LoadHalf(colors[i]), VectorComponentMask::s_XYZW))
            continue;

        MeshPayloadValidationDiagnostics::LogMeshPayloadIndexedValidation(contextText, meshPathText, MeshAssetBinaryPayload::s_ColorsStreamLabel, i, NWB_TEXT("contains non-finite data"));
        return false;
    }

    return true;
}


[[nodiscard]] bool MeshRuntimeValidation::ValidateMeshletAttributeSkinSharing(
    Core::Alloc::ScratchArena& scratchArena,
    const Core::Assets::AssetVector<u8>& positionRefDeltas,
    const Core::Assets::AssetVector<MeshletLocalVertexRef>& localVertexRefs,
    const Core::Assets::AssetVector<MeshletDesc>& meshlets,
    const TStringView contextText,
    const TStringView meshPathText
){
    Vector<u32, Core::Alloc::ScratchArena> attributeSkins{scratchArena};
    usize attributeRefCount = 0u;
    for(const MeshletDesc& meshlet : meshlets)
        attributeRefCount += MeshletAttributeCount(meshlet);

    return MeshMeshletRefValidation::ResolveMeshletAttributeSkinsFromLocalVertices(
        meshlets,
        localVertexRefs,
        attributeRefCount,
        attributeSkins,
        [&](const usize meshletIndex, const MeshletDesc& meshlet, const usize, const u32 localPositionIndex)->Expected<u32>{
            const auto positionRef = DecodeMeshletPositionRef(positionRefDeltas.data(), positionRefDeltas.size(), meshlet, localPositionIndex, true);
            if(!positionRef){
                MeshPayloadValidationDiagnostics::LogMeshletPayloadValidation(contextText, meshPathText, meshletIndex, NWB_TEXT("has invalid encoded position ref"));
                return MakeUnexpected(Failure{});
            }

            return positionRef->skin;
        },
        [&](const usize meshletIndex, const usize attributeIndex, const u32 previousSkin, const u32 skinIndex){
            static_cast<void>(previousSkin);
            static_cast<void>(skinIndex);
            MeshPayloadValidationDiagnostics::LogMeshletAttributePayloadValidation(contextText, meshPathText, meshletIndex, attributeIndex, NWB_TEXT("shared across skin identities"));
            return false;
        },
        [&](const usize attributeIndex){
            MeshPayloadValidationDiagnostics::LogMeshPayloadIndexedValidation(contextText, meshPathText, NWB_TEXT("meshlet attribute ref"), attributeIndex, NWB_TEXT("is unreferenced"));
            return false;
        }
    );
}


[[nodiscard]] bool MeshRuntimeValidation::ValidateMeshletPayload(
    Core::Alloc::ScratchArena& scratchArena,
    const Core::Assets::AssetVector<u8>& positionRefDeltas,
    const Core::Assets::AssetVector<u8>& attributeRefDeltas,
    const Core::Assets::AssetVector<MeshletLocalVertexRef>& localVertexRefs,
    const Core::Assets::AssetVector<MeshletDesc>& meshlets,
    const Core::Assets::AssetVector<MeshletBounds>& meshletBounds,
    const Core::Assets::AssetVector<u8>& meshletPrimitiveIndices,
    const usize positionCount,
    const usize skinCount,
    const usize normalCount,
    const usize tangentCount,
    const usize uv0Count,
    const usize colorCount,
    const bool skinRequired,
    const TStringView contextText,
    const TStringView meshPathText
){
    if(meshlets.empty() || meshletBounds.size() != meshlets.size()){
        MeshPayloadValidationDiagnostics::LogMeshPayloadValidation(contextText, meshPathText, NWB_TEXT("has incomplete meshlet payload"));
        return false;
    }
    if(meshlets.size() > Limit<u32>::s_Max / NWB_MESHLET_BOUNDS_STRIDE){
        MeshPayloadValidationDiagnostics::LogMeshPayloadValidation(contextText, meshPathText, NWB_TEXT("meshlet bounds exceed u32 byte-address limits"));
        return false;
    }
    if(!FitsU32(positionRefDeltas.size()) || !FitsU32(attributeRefDeltas.size()) || (skinRequired && !FitsU32(skinCount))){
        MeshPayloadValidationDiagnostics::LogMeshPayloadValidation(contextText, meshPathText, NWB_TEXT("exceeds u32 stream count limits"));
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
            MeshPayloadValidationDiagnostics::LogMeshletPayloadValidation(contextText, meshPathText, meshletIndex, NWB_TEXT("has invalid vertex count"));
            return false;
        }
        if(primitiveCount == 0u || primitiveCount > s_MeshMaxMeshletTriangles){
            MeshPayloadValidationDiagnostics::LogMeshletPayloadValidation(contextText, meshPathText, meshletIndex, NWB_TEXT("has invalid primitive count"));
            return false;
        }
        if(encodedPositionCount == 0u || encodedPositionCount > s_MeshMaxMeshletVertices){
            MeshPayloadValidationDiagnostics::LogMeshletPayloadValidation(contextText, meshPathText, meshletIndex, NWB_TEXT("has invalid deformed position count"));
            return false;
        }
        if(attributeCount == 0u || attributeCount > s_MeshMaxMeshletVertices){
            MeshPayloadValidationDiagnostics::LogMeshletPayloadValidation(contextText, meshPathText, meshletIndex, NWB_TEXT("has invalid attribute count"));
            return false;
        }
        if(meshlet.localVertexOffset != expectedLocalVertexRefCount || meshlet.primitiveOffset != expectedPrimitiveIndexCount || meshlet.positionRefOffset != expectedPositionRefByteCount || meshlet.attributeRefOffset != expectedAttributeRefByteCount){
            MeshPayloadValidationDiagnostics::LogMeshletPayloadValidation(contextText, meshPathText, meshletIndex, NWB_TEXT("has non-contiguous offsets"));
            return false;
        }

        const auto encodedPositionBytes = MeshletEncodedPositionRefByteCount(meshlet, skinRequired);
        const auto encodedAttributeBytes = MeshletEncodedAttributeRefByteCount(meshlet);
        if(!encodedPositionBytes || !encodedAttributeBytes){
            MeshPayloadValidationDiagnostics::LogMeshletPayloadValidation(contextText, meshPathText, meshletIndex, NWB_TEXT("has invalid ref encoding width"));
            return false;
        }
        if(*encodedPositionBytes > Limit<usize>::s_Max - expectedPositionRefByteCount || *encodedAttributeBytes > Limit<usize>::s_Max - expectedAttributeRefByteCount){
            MeshPayloadValidationDiagnostics::LogMeshletPayloadValidation(contextText, meshPathText, meshletIndex, NWB_TEXT("encoded ref byte counts overflow"));
            return false;
        }

        expectedLocalVertexRefCount += vertexCount;
        expectedPositionRefByteCount += *encodedPositionBytes;
        expectedAttributeRefByteCount += *encodedAttributeBytes;
        expectedPrimitiveIndexCount += static_cast<usize>(primitiveCount) * s_MeshletTriangleIndexCount;
        if(expectedLocalVertexRefCount > localVertexRefs.size() || expectedPositionRefByteCount > positionRefDeltas.size() || expectedAttributeRefByteCount > attributeRefDeltas.size() || expectedPrimitiveIndexCount > meshletPrimitiveIndices.size()){
            MeshPayloadValidationDiagnostics::LogMeshletPayloadValidation(contextText, meshPathText, meshletIndex, NWB_TEXT("exceeds meshlet stream bounds"));
            return false;
        }

        const MeshletBounds& bounds = meshletBounds[meshletIndex];
        const SIMDVectorSingle sphere = LoadFloat(bounds.sphere);
        if(!VectorIsFinite(sphere, VectorComponentMask::s_XYZW) || VectorGetW(sphere) < 0.0f){
            MeshPayloadValidationDiagnostics::LogMeshletPayloadValidation(contextText, meshPathText, meshletIndex, NWB_TEXT("has invalid bounds"));
            return false;
        }
        if(MeshletConeFlags(bounds) & ~s_MeshletConeFlagEnabled){
            MeshPayloadValidationDiagnostics::LogMeshletPayloadValidation(contextText, meshPathText, meshletIndex, NWB_TEXT("has invalid cone flags"));
            return false;
        }
        if(MeshletConeEnabled(bounds) && MeshletConePackedCutoff(bounds) == 0u){
            MeshPayloadValidationDiagnostics::LogMeshletPayloadValidation(contextText, meshPathText, meshletIndex, NWB_TEXT("has invalid cone cutoff"));
            return false;
        }

        for(u32 localPositionIndex = 0u; localPositionIndex < encodedPositionCount; ++localPositionIndex){
            const auto ref = DecodeMeshletPositionRef(positionRefDeltas.data(), positionRefDeltas.size(), meshlet, localPositionIndex, skinRequired);
            if(ref && MeshMeshletRefValidation::MeshletPositionRefInRange(*ref, positionCount, skinCount, skinRequired))
                continue;

            MeshPayloadValidationDiagnostics::LogMeshletPayloadValidation(contextText, meshPathText, meshletIndex, NWB_TEXT("has invalid encoded position ref"));
            return false;
        }

        for(u32 localAttributeIndex = 0u; localAttributeIndex < attributeCount; ++localAttributeIndex){
            const auto ref = DecodeMeshletAttributeRef(attributeRefDeltas.data(), attributeRefDeltas.size(), meshlet, localAttributeIndex);
            if(ref && MeshMeshletRefValidation::MeshletAttributeRefInRange(*ref, normalCount, tangentCount, uv0Count, colorCount))
                continue;

            MeshPayloadValidationDiagnostics::LogMeshletPayloadValidation(contextText, meshPathText, meshletIndex, NWB_TEXT("has invalid encoded attribute ref"));
            return false;
        }
        for(u32 localVertexIndex = 0u; localVertexIndex < vertexCount; ++localVertexIndex){
            const MeshletLocalVertexRef& ref = localVertexRefs[meshlet.localVertexOffset + localVertexIndex];
            if(ref.localDeformedPosition < encodedPositionCount && ref.localAttribute < attributeCount)
                continue;

            MeshPayloadValidationDiagnostics::LogMeshletPayloadValidation(contextText, meshPathText, meshletIndex, NWB_TEXT("local vertex ref is out of range"));
            return false;
        }
        for(u32 primitiveIndex = 0u; primitiveIndex < primitiveCount; ++primitiveIndex){
            const usize primitiveOffset = meshlet.primitiveOffset + static_cast<usize>(primitiveIndex) * s_MeshletTriangleIndexCount;
            for(usize corner = 0u; corner < s_MeshletTriangleIndexCount; ++corner){
                if(meshletPrimitiveIndices[primitiveOffset + corner] < vertexCount)
                    continue;

                MeshPayloadValidationDiagnostics::LogMeshletPrimitivePayloadValidation(contextText, meshPathText, meshletIndex, primitiveIndex, NWB_TEXT("has an out-of-range local vertex"));
                return false;
            }
        }
    }

    if(expectedLocalVertexRefCount != localVertexRefs.size() || expectedPositionRefByteCount != positionRefDeltas.size() || expectedAttributeRefByteCount != attributeRefDeltas.size() || expectedPrimitiveIndexCount != meshletPrimitiveIndices.size()){
        MeshPayloadValidationDiagnostics::LogMeshPayloadValidation(contextText, meshPathText, NWB_TEXT("meshlet streams contain trailing data"));
        return false;
    }

    if(skinRequired && !ValidateMeshletAttributeSkinSharing(scratchArena, positionRefDeltas, localVertexRefs, meshlets, contextText, meshPathText))
        return false;

    return true;
}


[[nodiscard]] bool MeshRuntimeValidation::ValidateSharedMeshPayload(
    Core::Alloc::ScratchArena& scratchArena,
    const Core::Assets::AssetVector<Float3U>& positions,
    const Core::Assets::AssetVector<Half4U>& normals,
    const Core::Assets::AssetVector<Half4U>& tangents,
    const Core::Assets::AssetVector<Float2U>& uv0,
    const Core::Assets::AssetVector<Half4U>& colors,
    const Core::Assets::AssetVector<u8>& positionRefDeltas,
    const Core::Assets::AssetVector<u8>& attributeRefDeltas,
    const Core::Assets::AssetVector<MeshletLocalVertexRef>& localVertexRefs,
    const Core::Assets::AssetVector<MeshletDesc>& meshlets,
    const Core::Assets::AssetVector<MeshletBounds>& meshletBounds,
    const Core::Assets::AssetVector<u8>& meshletPrimitiveIndices,
    const usize skinCount,
    const bool skinRequired,
    const TStringView contextText,
    const TStringView meshPathText
){
    if(!ValidateMeshStreams(positions, normals, tangents, uv0, colors, contextText, meshPathText))
        return false;

    return ValidateMeshletPayload(
        scratchArena,
        positionRefDeltas,
        attributeRefDeltas,
        localVertexRefs,
        meshlets,
        meshletBounds,
        meshletPrimitiveIndices,
        positions.size(),
        skinCount,
        normals.size(),
        tangents.size(),
        uv0.size(),
        colors.size(),
        skinRequired,
        contextText,
        meshPathText
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

