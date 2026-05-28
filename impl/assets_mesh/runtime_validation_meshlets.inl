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
        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mesh '{}' exceeds u32 stream count limits")
            , contextText
            , meshPathText
        );
        return false;
    }

    for(usize positionRefIndex = 0u; positionRefIndex < positionRefs.size(); ++positionRefIndex){
        const MeshletDeformedPositionRef& ref = positionRefs[positionRefIndex];
        if(MeshletPositionRefInRange(ref, positionCount, skinCount, skinRequired))
            continue;

        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mesh '{}' meshlet position ref {} is out of range")
            , contextText
            , meshPathText
            , positionRefIndex
        );
        return false;
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
            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mesh '{}' meshlet {} has attribute ref {} shared across skin identities")
                , contextText
                , meshPathText
                , meshletIndex
                , attributeIndex
            );
            return false;
        },
        [&](const usize attributeIndex){
            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mesh '{}' meshlet attribute ref {} is unreferenced")
                , contextText
                , meshPathText
                , attributeIndex
            );
            return false;
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
        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mesh '{}' has incomplete meshlet payload")
            , contextText
            , meshPathText
        );
        return false;
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
            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mesh '{}' meshlet {} has invalid vertex count")
                , contextText
                , meshPathText
                , meshletIndex
            );
            return false;
        }
        if(primitiveCount == 0u || primitiveCount > s_MeshMaxMeshletTriangles){
            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mesh '{}' meshlet {} has invalid primitive count")
                , contextText
                , meshPathText
                , meshletIndex
            );
            return false;
        }
        if(positionCount == 0u || positionCount > s_MeshMaxMeshletVertices){
            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mesh '{}' meshlet {} has invalid deformed position count")
                , contextText
                , meshPathText
                , meshletIndex
            );
            return false;
        }
        if(attributeCount == 0u || attributeCount > s_MeshMaxMeshletVertices){
            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mesh '{}' meshlet {} has invalid attribute count")
                , contextText
                , meshPathText
                , meshletIndex
            );
            return false;
        }
        if(
            meshlet.localVertexOffset != expectedLocalVertexRefCount
            || meshlet.positionOffset != expectedPositionRefCount
            || meshlet.attributeOffset != expectedAttributeRefCount
            || meshlet.primitiveOffset != expectedPrimitiveIndexCount
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mesh '{}' meshlet {} has non-contiguous offsets")
                , contextText
                , meshPathText
                , meshletIndex
            );
            return false;
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
            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mesh '{}' meshlet {} exceeds meshlet stream bounds")
                , contextText
                , meshPathText
                , meshletIndex
            );
            return false;
        }

        const MeshletBounds& bounds = meshletBounds[meshletIndex];
        const SIMDVector sphere = LoadFloat(bounds.sphere);
        if(!FiniteVector(sphere, 0xFu) || VectorGetW(sphere) < 0.0f){
            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mesh '{}' meshlet {} has invalid bounds")
                , contextText
                , meshPathText
                , meshletIndex
            );
            return false;
        }
        if(MeshletConeFlags(bounds) & ~s_MeshletConeFlagEnabled){
            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mesh '{}' meshlet {} has invalid cone flags")
                , contextText
                , meshPathText
                , meshletIndex
            );
            return false;
        }
        if(MeshletConeEnabled(bounds) && MeshletConePackedCutoff(bounds) == 0u){
            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mesh '{}' meshlet {} has invalid cone cutoff")
                , contextText
                , meshPathText
                , meshletIndex
            );
            return false;
        }

        for(u32 localAttributeIndex = 0u; localAttributeIndex < attributeCount; ++localAttributeIndex){
            const MeshletShadingAttributeRef& ref = attributeRefs[meshlet.attributeOffset + localAttributeIndex];
            if(MeshletAttributeRefInRange(ref, normalCount, tangentCount, uv0Count, colorCount))
                continue;

            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mesh '{}' meshlet {} has invalid local attribute")
                , contextText
                , meshPathText
                , meshletIndex
            );
            return false;
        }
        for(u32 localVertexIndex = 0u; localVertexIndex < vertexCount; ++localVertexIndex){
            const MeshletLocalVertexRef& ref = localVertexRefs[meshlet.localVertexOffset + localVertexIndex];
            if(ref.localDeformedPosition < positionCount && ref.localAttribute < attributeCount)
                continue;

            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mesh '{}' meshlet {} local vertex ref is out of range")
                , contextText
                , meshPathText
                , meshletIndex
            );
            return false;
        }
        for(u32 primitiveIndex = 0u; primitiveIndex < primitiveCount; ++primitiveIndex){
            const usize primitiveOffset = meshlet.primitiveOffset + static_cast<usize>(primitiveIndex) * 3u;
            for(usize corner = 0u; corner < 3u; ++corner){
                if(meshletPrimitiveIndices[primitiveOffset + corner] < vertexCount)
                    continue;

                NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mesh '{}' meshlet {} primitive {} has an out-of-range local vertex")
                    , contextText
                    , meshPathText
                    , meshletIndex
                    , primitiveIndex
                );
                return false;
            }
        }
    }

    if(
        expectedLocalVertexRefCount != localVertexRefs.size()
        || expectedPositionRefCount != positionRefs.size()
        || expectedAttributeRefCount != attributeRefs.size()
        || expectedPrimitiveIndexCount != meshletPrimitiveIndices.size()
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mesh '{}' meshlet streams contain trailing data")
            , contextText
            , meshPathText
        );
        return false;
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

