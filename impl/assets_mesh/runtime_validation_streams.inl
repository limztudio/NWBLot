// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ValidDirectionVector(const SIMDVector direction){
    return
        VectorIsFinite(direction, 0x7u)
        && Vector3NearEqual(Vector3LengthSq(direction), s_SIMDOne, VectorReplicate(0.01f))
    ;
}

[[nodiscard]] static bool ValidTangentVector(const SIMDVector tangent){
    const SIMDVector direction = VectorSetW(tangent, 0.0f);
    return
        VectorIsFinite(tangent, 0xFu)
        && Vector3NearEqual(Vector3LengthSq(direction), s_SIMDOne, VectorReplicate(0.01f))
        && Vector4NearEqual(VectorAbs(VectorSplatW(tangent)), s_SIMDOne, VectorReplicate(0.001f))
    ;
}

[[nodiscard]] static bool ValidateMeshStreams(
    const Core::Assets::AssetVector<Float3U>& positions,
    const Core::Assets::AssetVector<Half4U>& normals,
    const Core::Assets::AssetVector<Half4U>& tangents,
    const Core::Assets::AssetVector<Float2U>& uv0,
    const Core::Assets::AssetVector<Half4U>& colors,
    const tchar* contextText,
    const TStringView meshPathText
){
    if(
        !FitsU32(positions.size())
        || !FitsU32(normals.size())
        || !FitsU32(tangents.size())
        || !FitsU32(uv0.size())
        || !FitsU32(colors.size())
    ){
        return FailMeshPayloadValidation(
            contextText,
            meshPathText,
            NWB_TEXT("exceeds u32 stream count limits")
        );
    }

    for(usize i = 0u; i < positions.size(); ++i){
        if(VectorIsFinite(VectorSetW(LoadFloat(positions[i]), 0.0f), 0x7u))
            continue;

        return FailMeshPayloadIndexedValidation(
            contextText,
            meshPathText,
            NWB_TEXT("position"),
            i,
            NWB_TEXT("contains non-finite data")
        );
    }
    for(usize i = 0u; i < normals.size(); ++i){
        if(ValidDirectionVector(VectorSetW(LoadFloat(LoadHalf4U(normals[i])), 0.0f)))
            continue;

        return FailMeshPayloadIndexedValidation(
            contextText,
            meshPathText,
            NWB_TEXT("normal"),
            i,
            NWB_TEXT("is invalid")
        );
    }
    for(usize i = 0u; i < tangents.size(); ++i){
        if(ValidTangentVector(LoadFloat(LoadHalf4U(tangents[i]))))
            continue;

        return FailMeshPayloadIndexedValidation(
            contextText,
            meshPathText,
            NWB_TEXT("tangent"),
            i,
            NWB_TEXT("is invalid")
        );
    }
    for(usize i = 0u; i < uv0.size(); ++i){
        if(VectorIsFinite(VectorSetW(VectorSetZ(LoadFloat(uv0[i]), 0.0f), 0.0f), 0x3u))
            continue;

        return FailMeshPayloadIndexedValidation(
            contextText,
            meshPathText,
            NWB_TEXT("uv0"),
            i,
            NWB_TEXT("contains non-finite data")
        );
    }
    for(usize i = 0u; i < colors.size(); ++i){
        if(VectorIsFinite(LoadFloat(LoadHalf4U(colors[i])), 0xFu))
            continue;

        return FailMeshPayloadIndexedValidation(
            contextText,
            meshPathText,
            NWB_TEXT("color"),
            i,
            NWB_TEXT("contains non-finite data")
        );
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

