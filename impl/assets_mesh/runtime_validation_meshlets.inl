// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ValidateMeshletPositionRefs(
    const Core::Assets::AssetVector<MeshletDeformedPositionRef>& positionRefs,
    const usize positionCount,
    const usize skinCount,
    const bool skinRequired,
    const tchar* contextText,
    const TStringView meshPathText
){
    if(!CountFitsU32(positionRefs.size()) || (skinRequired && !CountFitsU32(skinCount))){
        return FailMeshPayloadValidation(
            contextText,
            meshPathText,
            NWB_TEXT("exceeds u32 stream count limits")
        );
    }

    for(usize positionRefIndex = 0u; positionRefIndex < positionRefs.size(); ++positionRefIndex){
        const MeshletDeformedPositionRef& ref = positionRefs[positionRefIndex];
        if(MeshletPositionRefInRange(ref, positionCount, skinCount, skinRequired))
            continue;

        return FailMeshPayloadIndexedValidation(
            contextText,
            meshPathText,
            NWB_TEXT("meshlet position ref"),
            positionRefIndex,
            NWB_TEXT("is out of range")
        );
    }

    return true;
}

[[nodiscard]] static bool ValidateMeshletAttributeSkinSharing(
    const Core::Assets::AssetVector<MeshletDeformedPositionRef>& positionRefs,
    const Core::Assets::AssetVector<MeshletShadingAttributeRef>& attributeRefs,
    const Core::Assets::AssetVector<MeshletLocalVertexRef>& localVertexRefs,
    const Core::Assets::AssetVector<MeshletDesc>& meshlets,
    const tchar* contextText,
    const TStringView meshPathText
){
    Core::Alloc::ScratchArena scratchArena;
    Vector<u32, Core::Alloc::ScratchArena> attributeSkins{scratchArena};
    return ResolveMeshletAttributeSkins(
        meshlets,
        positionRefs,
        localVertexRefs,
        attributeRefs.size(),
        attributeSkins,
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
    const Core::Assets::AssetVector<MeshletDeformedPositionRef>& positionRefs,
    const Core::Assets::AssetVector<MeshletShadingAttributeRef>& attributeRefs,
    const Core::Assets::AssetVector<MeshletLocalVertexRef>& localVertexRefs,
    const Core::Assets::AssetVector<MeshletDesc>& meshlets,
    const Core::Assets::AssetVector<MeshletBounds>& meshletBounds,
    const Core::Assets::AssetVector<u8>& meshletPrimitiveIndices,
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

    usize expectedLocalVertexRefCount = 0u;
    usize expectedPositionRefCount = 0u;
    usize expectedAttributeRefCount = 0u;
    usize expectedPrimitiveIndexCount = 0u;
    for(usize meshletIndex = 0u; meshletIndex < meshlets.size(); ++meshletIndex){
        const MeshletDesc& meshlet = meshlets[meshletIndex];
        const u32 vertexCount = MeshletVertexCount(meshlet);
        const u32 primitiveCount = MeshletPrimitiveCount(meshlet);
        const u32 positionCount = MeshletPositionCount(meshlet);
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
        if(positionCount == 0u || positionCount > s_MeshMaxMeshletVertices){
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
            || meshlet.positionOffset != expectedPositionRefCount
            || meshlet.attributeOffset != expectedAttributeRefCount
            || meshlet.primitiveOffset != expectedPrimitiveIndexCount
        ){
            return FailMeshletPayloadValidation(
                contextText,
                meshPathText,
                meshletIndex,
                NWB_TEXT("has non-contiguous offsets")
            );
        }

        expectedLocalVertexRefCount += vertexCount;
        expectedPositionRefCount += positionCount;
        expectedAttributeRefCount += attributeCount;
        expectedPrimitiveIndexCount += static_cast<usize>(primitiveCount) * 3u;
        if(
            expectedLocalVertexRefCount > localVertexRefs.size()
            || expectedPositionRefCount > positionRefs.size()
            || expectedAttributeRefCount > attributeRefs.size()
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

        for(u32 localAttributeIndex = 0u; localAttributeIndex < attributeCount; ++localAttributeIndex){
            const MeshletShadingAttributeRef& ref = attributeRefs[meshlet.attributeOffset + localAttributeIndex];
            if(MeshletAttributeRefInRange(ref, normalCount, tangentCount, uv0Count, colorCount))
                continue;

            return FailMeshletPayloadValidation(
                contextText,
                meshPathText,
                meshletIndex,
                NWB_TEXT("has invalid local attribute")
            );
        }
        for(u32 localVertexIndex = 0u; localVertexIndex < vertexCount; ++localVertexIndex){
            const MeshletLocalVertexRef& ref = localVertexRefs[meshlet.localVertexOffset + localVertexIndex];
            if(ref.localDeformedPosition < positionCount && ref.localAttribute < attributeCount)
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
        || expectedPositionRefCount != positionRefs.size()
        || expectedAttributeRefCount != attributeRefs.size()
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
            positionRefs,
            attributeRefs,
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

