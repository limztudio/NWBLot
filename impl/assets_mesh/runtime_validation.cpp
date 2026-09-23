// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "runtime_validation.h"

#include "arena_names.h"


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


[[nodiscard]] SIMDVector MeshRuntimeValidation::MakeMeshPositionVector(const SIMDVector position){
    return VectorSetW(position, 0.0f);
}


[[nodiscard]] SIMDVector MeshRuntimeValidation::MakeMeshNormalVector(const SIMDVector normal){
    return VectorSetW(normal, 0.0f);
}


[[nodiscard]] SIMDVector MeshRuntimeValidation::MakeMeshTangentVector(const SIMDVector tangent){
    return tangent;
}


[[nodiscard]] SIMDVector MeshRuntimeValidation::MakeMeshUvVector(const SIMDVector uv){
    return VectorSetW(VectorSetZ(uv, 0.0f), 0.0f);
}


[[nodiscard]] SIMDVector MeshRuntimeValidation::MakeMeshColorVector(const SIMDVector color){
    return color;
}


[[nodiscard]] bool MeshRuntimeValidation::ValidDirectionVector(const SIMDVector direction){
    return
        VectorIsFinite(direction, VectorComponentMask::s_XYZ)
        && Vector3NearEqual(Vector3LengthSq(direction), s_SIMDOne, VectorReplicate(__hidden_mesh_validation::s_DirectionLengthTolerance))
    ;
}


[[nodiscard]] bool MeshRuntimeValidation::ValidTangentVector(const SIMDVector tangent){
    const SIMDVector direction = VectorSetW(tangent, 0.0f);
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
    const NotNull<const tchar*> contextText,
    const TStringView meshPathText
){
    if(
        !FitsU32(positions.size())
        || !FitsU32(normals.size())
        || !FitsU32(tangents.size())
        || !FitsU32(uv0.size())
        || !FitsU32(colors.size())
    ){
        return MeshPayloadValidationDiagnostics::FailMeshPayloadValidation(
            contextText,
            meshPathText,
            NWB_TEXT("exceeds u32 stream count limits")
        );
    }

    for(usize i = 0u; i < positions.size(); ++i){
        if(VectorIsFinite(MakeMeshPositionVector(LoadFloat(positions[i])), VectorComponentMask::s_XYZ))
            continue;

        return MeshPayloadValidationDiagnostics::FailMeshPayloadIndexedValidation(
            contextText,
            meshPathText,
            NWB_TEXT("position"),
            i,
            NWB_TEXT("contains non-finite data")
        );
    }
    for(usize i = 0u; i < normals.size(); ++i){
        if(ValidDirectionVector(MakeMeshNormalVector(LoadHalf(normals[i]))))
            continue;

        return MeshPayloadValidationDiagnostics::FailMeshPayloadIndexedValidation(
            contextText,
            meshPathText,
            NWB_TEXT("normal"),
            i,
            NWB_TEXT("is invalid")
        );
    }
    for(usize i = 0u; i < tangents.size(); ++i){
        if(ValidTangentVector(MakeMeshTangentVector(LoadHalf(tangents[i]))))
            continue;

        return MeshPayloadValidationDiagnostics::FailMeshPayloadIndexedValidation(
            contextText,
            meshPathText,
            NWB_TEXT("tangent"),
            i,
            NWB_TEXT("is invalid")
        );
    }
    for(usize i = 0u; i < uv0.size(); ++i){
        if(VectorIsFinite(MakeMeshUvVector(LoadFloat(uv0[i])), VectorComponentMask::s_XY))
            continue;

        return MeshPayloadValidationDiagnostics::FailMeshPayloadIndexedValidation(
            contextText,
            meshPathText,
            NWB_TEXT("uv0"),
            i,
            NWB_TEXT("contains non-finite data")
        );
    }
    for(usize i = 0u; i < colors.size(); ++i){
        if(VectorIsFinite(MakeMeshColorVector(LoadHalf(colors[i])), VectorComponentMask::s_XYZW))
            continue;

        return MeshPayloadValidationDiagnostics::FailMeshPayloadIndexedValidation(
            contextText,
            meshPathText,
            NWB_TEXT("color"),
            i,
            NWB_TEXT("contains non-finite data")
        );
    }

    return true;
}


[[nodiscard]] bool MeshRuntimeValidation::ValidateMeshletAttributeSkinSharing(
    const Core::Assets::AssetVector<u8>& positionRefDeltas,
    const Core::Assets::AssetVector<MeshletLocalVertexRef>& localVertexRefs,
    const Core::Assets::AssetVector<MeshletDesc>& meshlets,
    const NotNull<const tchar*> contextText,
    const TStringView meshPathText
){
    Core::Alloc::ScratchArena scratchArena(AssetsMeshArenaScope::s_MeshletAttributeSkinSharingArena);
    Vector<u32, Core::Alloc::ScratchArena> attributeSkins{scratchArena};
    usize attributeRefCount = 0u;
    for(const MeshletDesc& meshlet : meshlets)
        attributeRefCount += MeshletAttributeCount(meshlet);

    return MeshMeshletRefValidation::ResolveMeshletAttributeSkinsFromLocalVertices(
        meshlets,
        localVertexRefs,
        attributeRefCount,
        attributeSkins,
        [&](const usize meshletIndex, const MeshletDesc& meshlet, const usize, const u32 localPositionIndex, u32& outSkin){
            MeshletPositionStreamRef positionRef;
            if(!DecodeMeshletPositionRef(
                positionRefDeltas.data(),
                positionRefDeltas.size(),
                meshlet,
                localPositionIndex,
                true,
                positionRef
            )){
                return MeshPayloadValidationDiagnostics::FailMeshletPayloadValidation(
                    contextText,
                    meshPathText,
                    meshletIndex,
                    NWB_TEXT("has invalid encoded position ref")
                );
            }

            outSkin = positionRef.skin;
            return true;
        },
        [&](const usize meshletIndex, const usize attributeIndex, const u32 previousSkin, const u32 skinIndex){
            static_cast<void>(previousSkin);
            static_cast<void>(skinIndex);
            return MeshPayloadValidationDiagnostics::FailMeshletAttributePayloadValidation(
                contextText,
                meshPathText,
                meshletIndex,
                attributeIndex,
                NWB_TEXT("shared across skin identities")
            );
        },
        [&](const usize attributeIndex){
            return MeshPayloadValidationDiagnostics::FailMeshPayloadIndexedValidation(
                contextText,
                meshPathText,
                NWB_TEXT("meshlet attribute ref"),
                attributeIndex,
                NWB_TEXT("is unreferenced")
            );
        }
    );
}


[[nodiscard]] bool MeshRuntimeValidation::ValidateMeshletPayload(
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
    const NotNull<const tchar*> contextText,
    const TStringView meshPathText
){
    if(meshlets.empty() || meshletBounds.size() != meshlets.size()){
        return MeshPayloadValidationDiagnostics::FailMeshPayloadValidation(
            contextText,
            meshPathText,
            NWB_TEXT("has incomplete meshlet payload")
        );
    }
    if(
        !FitsU32(positionRefDeltas.size())
        || !FitsU32(attributeRefDeltas.size())
        || (skinRequired && !FitsU32(skinCount))
    ){
        return MeshPayloadValidationDiagnostics::FailMeshPayloadValidation(
            contextText,
            meshPathText,
            NWB_TEXT("exceeds u32 stream count limits")
        );
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
            return MeshPayloadValidationDiagnostics::FailMeshletPayloadValidation(
                contextText,
                meshPathText,
                meshletIndex,
                NWB_TEXT("has invalid vertex count")
            );
        }
        if(primitiveCount == 0u || primitiveCount > s_MeshMaxMeshletTriangles){
            return MeshPayloadValidationDiagnostics::FailMeshletPayloadValidation(
                contextText,
                meshPathText,
                meshletIndex,
                NWB_TEXT("has invalid primitive count")
            );
        }
        if(encodedPositionCount == 0u || encodedPositionCount > s_MeshMaxMeshletVertices){
            return MeshPayloadValidationDiagnostics::FailMeshletPayloadValidation(
                contextText,
                meshPathText,
                meshletIndex,
                NWB_TEXT("has invalid deformed position count")
            );
        }
        if(attributeCount == 0u || attributeCount > s_MeshMaxMeshletVertices){
            return MeshPayloadValidationDiagnostics::FailMeshletPayloadValidation(
                contextText,
                meshPathText,
                meshletIndex,
                NWB_TEXT("has invalid attribute count")
            );
        }
        if(
            meshlet.localVertexOffset != expectedLocalVertexRefCount
            || meshlet.primitiveOffset != expectedPrimitiveIndexCount
            || meshlet.positionRefOffset != expectedPositionRefByteCount
            || meshlet.attributeRefOffset != expectedAttributeRefByteCount
        ){
            return MeshPayloadValidationDiagnostics::FailMeshletPayloadValidation(
                contextText,
                meshPathText,
                meshletIndex,
                NWB_TEXT("has non-contiguous offsets")
            );
        }

        usize encodedPositionBytes = 0u;
        usize encodedAttributeBytes = 0u;
        if(
            !MeshletEncodedPositionRefByteCount(meshlet, skinRequired, encodedPositionBytes)
            || !MeshletEncodedAttributeRefByteCount(meshlet, encodedAttributeBytes)
        ){
            return MeshPayloadValidationDiagnostics::FailMeshletPayloadValidation(
                contextText,
                meshPathText,
                meshletIndex,
                NWB_TEXT("has invalid ref encoding width")
            );
        }
        if(
            encodedPositionBytes > Limit<usize>::s_Max - expectedPositionRefByteCount
            || encodedAttributeBytes > Limit<usize>::s_Max - expectedAttributeRefByteCount
        ){
            return MeshPayloadValidationDiagnostics::FailMeshletPayloadValidation(
                contextText,
                meshPathText,
                meshletIndex,
                NWB_TEXT("encoded ref byte counts overflow")
            );
        }

        expectedLocalVertexRefCount += vertexCount;
        expectedPositionRefByteCount += encodedPositionBytes;
        expectedAttributeRefByteCount += encodedAttributeBytes;
        expectedPrimitiveIndexCount += static_cast<usize>(primitiveCount) * s_MeshletTriangleIndexCount;
        if(
            expectedLocalVertexRefCount > localVertexRefs.size()
            || expectedPositionRefByteCount > positionRefDeltas.size()
            || expectedAttributeRefByteCount > attributeRefDeltas.size()
            || expectedPrimitiveIndexCount > meshletPrimitiveIndices.size()
        ){
            return MeshPayloadValidationDiagnostics::FailMeshletPayloadValidation(
                contextText,
                meshPathText,
                meshletIndex,
                NWB_TEXT("exceeds meshlet stream bounds")
            );
        }

        const MeshletBounds& bounds = meshletBounds[meshletIndex];
        const SIMDVector sphere = LoadFloat(bounds.sphere);
        if(!VectorIsFinite(sphere, VectorComponentMask::s_XYZW) || VectorGetW(sphere) < 0.0f){
            return MeshPayloadValidationDiagnostics::FailMeshletPayloadValidation(
                contextText,
                meshPathText,
                meshletIndex,
                NWB_TEXT("has invalid bounds")
            );
        }
        if(MeshletConeFlags(bounds) & ~s_MeshletConeFlagEnabled){
            return MeshPayloadValidationDiagnostics::FailMeshletPayloadValidation(
                contextText,
                meshPathText,
                meshletIndex,
                NWB_TEXT("has invalid cone flags")
            );
        }
        if(MeshletConeEnabled(bounds) && MeshletConePackedCutoff(bounds) == 0u){
            return MeshPayloadValidationDiagnostics::FailMeshletPayloadValidation(
                contextText,
                meshPathText,
                meshletIndex,
                NWB_TEXT("has invalid cone cutoff")
            );
        }

        for(u32 localPositionIndex = 0u; localPositionIndex < encodedPositionCount; ++localPositionIndex){
            MeshletPositionStreamRef ref;
            if(
                DecodeMeshletPositionRef(
                    positionRefDeltas.data(),
                    positionRefDeltas.size(),
                    meshlet,
                    localPositionIndex,
                    skinRequired,
                    ref
                )
                && MeshMeshletRefValidation::MeshletPositionRefInRange(ref, positionCount, skinCount, skinRequired)
            )
                continue;

            return MeshPayloadValidationDiagnostics::FailMeshletPayloadValidation(
                contextText,
                meshPathText,
                meshletIndex,
                NWB_TEXT("has invalid encoded position ref")
            );
        }

        for(u32 localAttributeIndex = 0u; localAttributeIndex < attributeCount; ++localAttributeIndex){
            MeshletAttributeStreamRef ref;
            if(
                DecodeMeshletAttributeRef(
                    attributeRefDeltas.data(),
                    attributeRefDeltas.size(),
                    meshlet,
                    localAttributeIndex,
                    ref
                )
                && MeshMeshletRefValidation::MeshletAttributeRefInRange(ref, normalCount, tangentCount, uv0Count, colorCount)
            )
                continue;

            return MeshPayloadValidationDiagnostics::FailMeshletPayloadValidation(
                contextText,
                meshPathText,
                meshletIndex,
                NWB_TEXT("has invalid encoded attribute ref")
            );
        }
        for(u32 localVertexIndex = 0u; localVertexIndex < vertexCount; ++localVertexIndex){
            const MeshletLocalVertexRef& ref = localVertexRefs[meshlet.localVertexOffset + localVertexIndex];
            if(ref.localDeformedPosition < encodedPositionCount && ref.localAttribute < attributeCount)
                continue;

            return MeshPayloadValidationDiagnostics::FailMeshletPayloadValidation(
                contextText,
                meshPathText,
                meshletIndex,
                NWB_TEXT("local vertex ref is out of range")
            );
        }
        for(u32 primitiveIndex = 0u; primitiveIndex < primitiveCount; ++primitiveIndex){
            const usize primitiveOffset = meshlet.primitiveOffset
                + static_cast<usize>(primitiveIndex) * s_MeshletTriangleIndexCount;
            for(usize corner = 0u; corner < s_MeshletTriangleIndexCount; ++corner){
                if(meshletPrimitiveIndices[primitiveOffset + corner] < vertexCount)
                    continue;

                return MeshPayloadValidationDiagnostics::FailMeshletPrimitivePayloadValidation(
                    contextText,
                    meshPathText,
                    meshletIndex,
                    primitiveIndex,
                    NWB_TEXT("has an out-of-range local vertex")
                );
            }
        }
    }

    if(
        expectedLocalVertexRefCount != localVertexRefs.size()
        || expectedPositionRefByteCount != positionRefDeltas.size()
        || expectedAttributeRefByteCount != attributeRefDeltas.size()
        || expectedPrimitiveIndexCount != meshletPrimitiveIndices.size()
    ){
        return MeshPayloadValidationDiagnostics::FailMeshPayloadValidation(
            contextText,
            meshPathText,
            NWB_TEXT("meshlet streams contain trailing data")
        );
    }

    if(
        skinRequired
        && !ValidateMeshletAttributeSkinSharing(
            positionRefDeltas,
            localVertexRefs,
            meshlets,
            contextText,
            meshPathText
        )
    )
        return false;

    return true;
}


[[nodiscard]] bool MeshRuntimeValidation::ValidateSharedMeshPayload(
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
    const NotNull<const tchar*> contextText,
    const TStringView meshPathText
){
    if(!ValidateMeshStreams(positions, normals, tangents, uv0, colors, contextText, meshPathText))
        return false;

    return ValidateMeshletPayload(
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

