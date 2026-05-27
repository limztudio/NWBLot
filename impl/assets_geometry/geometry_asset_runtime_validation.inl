// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using GeometryPayloadValidation::CountFitsU32;
using GeometryPayloadValidation::FiniteVector;

[[nodiscard]] static bool ValidStreamIndex(const u32 index, const usize count){
    return index < count;
}

[[nodiscard]] static bool ValidHalfDirection(const Half4U& value){
    const SIMDVector direction = VectorSetW(LoadFloat(LoadHalf4U(value)), 0.0f);
    const f32 lengthSquared = VectorGetX(Vector3LengthSq(direction));
    return FiniteVector(direction, 0x7u) && IsFinite(lengthSquared) && Abs(lengthSquared - 1.0f) <= 0.01f;
}

[[nodiscard]] static bool ValidHalfTangent(const Half4U& value){
    const SIMDVector tangent = LoadFloat(LoadHalf4U(value));
    const SIMDVector direction = VectorSetW(tangent, 0.0f);
    const f32 lengthSquared = VectorGetX(Vector3LengthSq(direction));
    const f32 handedness = Abs(VectorGetW(tangent));
    return
        FiniteVector(tangent, 0xFu)
        && IsFinite(lengthSquared)
        && Abs(lengthSquared - 1.0f) <= 0.01f
        && Abs(handedness - 1.0f) <= 0.001f
    ;
}

[[nodiscard]] static bool ValidateGeometryStreams(
    const Core::Assets::AssetVector<Float3U>& positions,
    const Core::Assets::AssetVector<Half4U>& normals,
    const Core::Assets::AssetVector<Half4U>& tangents,
    const Core::Assets::AssetVector<Float2U>& uv0,
    const Core::Assets::AssetVector<Half4U>& colors,
    const tchar* contextText,
    const TStringView geometryPathText
){
    if(
        !CountFitsU32(positions.size())
        || !CountFitsU32(normals.size())
        || !CountFitsU32(tangents.size())
        || !CountFitsU32(uv0.size())
        || !CountFitsU32(colors.size())
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: geometry '{}' exceeds u32 stream count limits")
            , contextText
            , geometryPathText
        );
        return false;
    }

    for(usize i = 0u; i < positions.size(); ++i){
        if(FiniteVector(VectorSetW(LoadFloat(positions[i]), 0.0f), 0x7u))
            continue;

        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: geometry '{}' position {} contains non-finite data")
            , contextText
            , geometryPathText
            , i
        );
        return false;
    }
    for(usize i = 0u; i < normals.size(); ++i){
        if(ValidHalfDirection(normals[i]))
            continue;

        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: geometry '{}' normal {} is invalid")
            , contextText
            , geometryPathText
            , i
        );
        return false;
    }
    for(usize i = 0u; i < tangents.size(); ++i){
        if(ValidHalfTangent(tangents[i]))
            continue;

        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: geometry '{}' tangent {} is invalid")
            , contextText
            , geometryPathText
            , i
        );
        return false;
    }
    for(usize i = 0u; i < uv0.size(); ++i){
        if(FiniteVector(VectorSetW(VectorSetZ(LoadFloat(uv0[i]), 0.0f), 0.0f), 0x3u))
            continue;

        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: geometry '{}' uv0 {} contains non-finite data")
            , contextText
            , geometryPathText
            , i
        );
        return false;
    }
    for(usize i = 0u; i < colors.size(); ++i){
        if(FiniteVector(LoadFloat(LoadHalf4U(colors[i])), 0xFu))
            continue;

        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: geometry '{}' color {} contains non-finite data")
            , contextText
            , geometryPathText
            , i
        );
        return false;
    }

    return true;
}

[[nodiscard]] static bool ValidateGeometryVertexRefs(
    const Core::Assets::AssetVector<GeometryVertexRef>& vertexRefs,
    const usize positionCount,
    const usize normalCount,
    const usize tangentCount,
    const usize uv0Count,
    const usize colorCount,
    const usize skinCount,
    const bool skinRequired,
    const tchar* contextText,
    const TStringView geometryPathText
){
    if(!CountFitsU32(vertexRefs.size()) || (skinRequired && !CountFitsU32(skinCount))){
        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: geometry '{}' exceeds u32 stream count limits")
            , contextText
            , geometryPathText
        );
        return false;
    }

    for(usize vertexRefIndex = 0u; vertexRefIndex < vertexRefs.size(); ++vertexRefIndex){
        const GeometryVertexRef& ref = vertexRefs[vertexRefIndex];
        const bool valid =
            ValidStreamIndex(ref.position, positionCount)
            && ValidStreamIndex(ref.normal, normalCount)
            && ValidStreamIndex(ref.tangent, tangentCount)
            && ValidStreamIndex(ref.uv0, uv0Count)
            && ValidStreamIndex(ref.color, colorCount)
            && (skinRequired ? ValidStreamIndex(ref.skin, skinCount) : ref.skin == s_GeometryMissingStreamIndex)
        ;
        if(valid)
            continue;

        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: geometry '{}' vertex_ref {} is out of range")
            , contextText
            , geometryPathText
            , vertexRefIndex
        );
        return false;
    }

    return true;
}

[[nodiscard]] static bool ValidateMeshletPayload(
    const Core::Assets::AssetVector<GeometryVertexRef>& vertexRefs,
    const Core::Assets::AssetVector<GeometryMeshletDesc>& meshlets,
    const Core::Assets::AssetVector<GeometryMeshletBounds>& meshletBounds,
    const Core::Assets::AssetVector<u32>& meshletVertexRefs,
    const Core::Assets::AssetVector<u8>& meshletPrimitiveIndices,
    const tchar* contextText,
    const TStringView geometryPathText
){
    if(meshlets.empty() || meshletBounds.size() != meshlets.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: geometry '{}' has incomplete meshlet payload")
            , contextText
            , geometryPathText
        );
        return false;
    }

    usize expectedVertexRefCount = 0u;
    usize expectedPrimitiveIndexCount = 0u;
    for(usize meshletIndex = 0u; meshletIndex < meshlets.size(); ++meshletIndex){
        const GeometryMeshletDesc& meshlet = meshlets[meshletIndex];
        if(meshlet.vertexCount == 0u || meshlet.vertexCount > s_GeometryMaxMeshletVertices){
            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: geometry '{}' meshlet {} has invalid vertex count")
                , contextText
                , geometryPathText
                , meshletIndex
            );
            return false;
        }
        if(meshlet.primitiveCount == 0u || meshlet.primitiveCount > s_GeometryMaxMeshletTriangles){
            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: geometry '{}' meshlet {} has invalid primitive count")
                , contextText
                , geometryPathText
                , meshletIndex
            );
            return false;
        }
        if(meshlet.vertexOffset != expectedVertexRefCount || meshlet.primitiveOffset != expectedPrimitiveIndexCount){
            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: geometry '{}' meshlet {} has non-contiguous offsets")
                , contextText
                , geometryPathText
                , meshletIndex
            );
            return false;
        }

        expectedVertexRefCount += meshlet.vertexCount;
        expectedPrimitiveIndexCount += static_cast<usize>(meshlet.primitiveCount) * 3u;
        if(expectedVertexRefCount > meshletVertexRefs.size() || expectedPrimitiveIndexCount > meshletPrimitiveIndices.size()){
            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: geometry '{}' meshlet {} exceeds meshlet stream bounds")
                , contextText
                , geometryPathText
                , meshletIndex
            );
            return false;
        }

        for(u32 localVertexIndex = 0u; localVertexIndex < meshlet.vertexCount; ++localVertexIndex){
            const u32 vertexRefIndex = meshletVertexRefs[meshlet.vertexOffset + localVertexIndex];
            if(vertexRefIndex < vertexRefs.size())
                continue;

            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: geometry '{}' meshlet {} references an out-of-range vertex_ref")
                , contextText
                , geometryPathText
                , meshletIndex
            );
            return false;
        }
        for(u32 primitiveIndex = 0u; primitiveIndex < meshlet.primitiveCount; ++primitiveIndex){
            const usize primitiveOffset = meshlet.primitiveOffset + static_cast<usize>(primitiveIndex) * 3u;
            for(usize corner = 0u; corner < 3u; ++corner){
                if(meshletPrimitiveIndices[primitiveOffset + corner] < meshlet.vertexCount)
                    continue;

                NWB_LOGGER_ERROR(NWB_TEXT("{} failed: geometry '{}' meshlet {} primitive {} has an out-of-range local vertex")
                    , contextText
                    , geometryPathText
                    , meshletIndex
                    , primitiveIndex
                );
                return false;
            }
        }
    }

    if(expectedVertexRefCount != meshletVertexRefs.size() || expectedPrimitiveIndexCount != meshletPrimitiveIndices.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: geometry '{}' meshlet streams contain trailing data")
            , contextText
            , geometryPathText
        );
        return false;
    }

    return true;
}

[[nodiscard]] static bool ValidateSharedGeometryPayload(
    const Core::Assets::AssetVector<Float3U>& positions,
    const Core::Assets::AssetVector<Half4U>& normals,
    const Core::Assets::AssetVector<Half4U>& tangents,
    const Core::Assets::AssetVector<Float2U>& uv0,
    const Core::Assets::AssetVector<Half4U>& colors,
    const Core::Assets::AssetVector<GeometryVertexRef>& vertexRefs,
    const Core::Assets::AssetVector<GeometryMeshletDesc>& meshlets,
    const Core::Assets::AssetVector<GeometryMeshletBounds>& meshletBounds,
    const Core::Assets::AssetVector<u32>& meshletVertexRefs,
    const Core::Assets::AssetVector<u8>& meshletPrimitiveIndices,
    const usize skinCount,
    const bool skinRequired,
    const tchar* contextText,
    const TStringView geometryPathText
){
    if(!ValidateGeometryStreams(positions, normals, tangents, uv0, colors, contextText, geometryPathText))
        return false;

    if(!ValidateGeometryVertexRefs(
        vertexRefs,
        positions.size(),
        normals.size(),
        tangents.size(),
        uv0.size(),
        colors.size(),
        skinCount,
        skinRequired,
        contextText,
        geometryPathText
    ))
        return false;

    return ValidateMeshletPayload(
        vertexRefs,
        meshlets,
        meshletBounds,
        meshletVertexRefs,
        meshletPrimitiveIndices,
        contextText,
        geometryPathText
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

