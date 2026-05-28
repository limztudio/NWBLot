// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ValidateMeshletAttributeSkinSharing(
    const Core::Assets::AssetVector<u8>& positionRefDeltas,
    const Core::Assets::AssetVector<MeshletLocalVertexRef>& localVertexRefs,
    const Core::Assets::AssetVector<MeshletDesc>& meshlets,
    const tchar* contextText,
    const TStringView meshPathText
){
    Core::Alloc::ScratchArena scratchArena;
    Vector<u32, Core::Alloc::ScratchArena> attributeSkins{scratchArena};
    usize attributeRefCount = 0u;
    for(const MeshletDesc& meshlet : meshlets)
        attributeRefCount += MeshletAttributeCount(meshlet);

    return ResolveMeshletAttributeSkinsFromLocalVertices(
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
                return FailMeshletPayloadValidation(
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
            return FailMeshletAttributePayloadValidation(
                contextText,
                meshPathText,
                meshletIndex,
                attributeIndex,
                NWB_TEXT("shared across skin identities")
            );
        },
        [&](const usize attributeIndex){
            return FailMeshPayloadIndexedValidation(
                contextText,
                meshPathText,
                NWB_TEXT("meshlet attribute ref"),
                attributeIndex,
                NWB_TEXT("is unreferenced")
            );
        }
    );
}

[[nodiscard]] static bool ValidateMeshletPayload(
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
    const tchar* contextText,
    const TStringView meshPathText
){
    if(meshlets.empty() || meshletBounds.size() != meshlets.size()){
        return FailMeshPayloadValidation(
            contextText,
            meshPathText,
            NWB_TEXT("has incomplete meshlet payload")
        );
    }
    if(
        !CountFitsU32(positionRefDeltas.size())
        || !CountFitsU32(attributeRefDeltas.size())
        || (skinRequired && !CountFitsU32(skinCount))
    ){
        return FailMeshPayloadValidation(
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
            return FailMeshletPayloadValidation(
                contextText,
                meshPathText,
                meshletIndex,
                NWB_TEXT("has invalid vertex count")
            );
        }
        if(primitiveCount == 0u || primitiveCount > s_MeshMaxMeshletTriangles){
            return FailMeshletPayloadValidation(
                contextText,
                meshPathText,
                meshletIndex,
                NWB_TEXT("has invalid primitive count")
            );
        }
        if(encodedPositionCount == 0u || encodedPositionCount > s_MeshMaxMeshletVertices){
            return FailMeshletPayloadValidation(
                contextText,
                meshPathText,
                meshletIndex,
                NWB_TEXT("has invalid deformed position count")
            );
        }
        if(attributeCount == 0u || attributeCount > s_MeshMaxMeshletVertices){
            return FailMeshletPayloadValidation(
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
            return FailMeshletPayloadValidation(
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
            return FailMeshletPayloadValidation(
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
            return FailMeshletPayloadValidation(
                contextText,
                meshPathText,
                meshletIndex,
                NWB_TEXT("encoded ref byte counts overflow")
            );
        }

        expectedLocalVertexRefCount += vertexCount;
        expectedPositionRefByteCount += encodedPositionBytes;
        expectedAttributeRefByteCount += encodedAttributeBytes;
        expectedPrimitiveIndexCount += static_cast<usize>(primitiveCount) * 3u;
        if(
            expectedLocalVertexRefCount > localVertexRefs.size()
            || expectedPositionRefByteCount > positionRefDeltas.size()
            || expectedAttributeRefByteCount > attributeRefDeltas.size()
            || expectedPrimitiveIndexCount > meshletPrimitiveIndices.size()
        ){
            return FailMeshletPayloadValidation(
                contextText,
                meshPathText,
                meshletIndex,
                NWB_TEXT("exceeds meshlet stream bounds")
            );
        }

        const MeshletBounds& bounds = meshletBounds[meshletIndex];
        const SIMDVector sphere = LoadFloat(bounds.sphere);
        if(!FiniteVector(sphere, 0xFu) || VectorGetW(sphere) < 0.0f){
            return FailMeshletPayloadValidation(
                contextText,
                meshPathText,
                meshletIndex,
                NWB_TEXT("has invalid bounds")
            );
        }
        if(MeshletConeFlags(bounds) & ~s_MeshletConeFlagEnabled){
            return FailMeshletPayloadValidation(
                contextText,
                meshPathText,
                meshletIndex,
                NWB_TEXT("has invalid cone flags")
            );
        }
        if(MeshletConeEnabled(bounds) && MeshletConePackedCutoff(bounds) == 0u){
            return FailMeshletPayloadValidation(
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
                && MeshletPositionRefInRange(ref, positionCount, skinCount, skinRequired)
            )
                continue;

            return FailMeshletPayloadValidation(
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
                && MeshletAttributeRefInRange(ref, normalCount, tangentCount, uv0Count, colorCount)
            )
                continue;

            return FailMeshletPayloadValidation(
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

            return FailMeshletPayloadValidation(
                contextText,
                meshPathText,
                meshletIndex,
                NWB_TEXT("local vertex ref is out of range")
            );
        }
        for(u32 primitiveIndex = 0u; primitiveIndex < primitiveCount; ++primitiveIndex){
            const usize primitiveOffset = meshlet.primitiveOffset + static_cast<usize>(primitiveIndex) * 3u;
            for(usize corner = 0u; corner < 3u; ++corner){
                if(meshletPrimitiveIndices[primitiveOffset + corner] < vertexCount)
                    continue;

                return FailMeshletPrimitivePayloadValidation(
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
        return FailMeshPayloadValidation(
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

