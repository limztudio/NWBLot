// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr u32 s_MissingSourceStreamIndex = Limit<u32>::s_Max;

struct SourceGeometryVertexRef{
    u32 position = s_MissingSourceStreamIndex;
    u32 normal = s_MissingSourceStreamIndex;
    u32 tangent = s_MissingSourceStreamIndex;
    u32 uv0 = s_MissingSourceStreamIndex;
    u32 color = s_MissingSourceStreamIndex;
    u32 skin = s_MissingSourceStreamIndex;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ParseSourceVertexRefValue(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const tchar* metaKind,
    const AStringView label,
    u32& outValue
){
    const MetadataU32ValueFailure::Enum failure = ValidateMetadataU32Value(value, outValue);
    if(failure == MetadataU32ValueFailure::None)
        return true;

    LogMetadataU32ValueFailure(nwbFilePath, metaKind, label, failure);
    return false;
}

static bool ParseSourceVertexRefs(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const tchar* metaKind,
    const bool includeSkin,
    ScratchVector<SourceGeometryVertexRef>& outVertexRefs,
    Core::Alloc::ScratchArena& scratchArena
){
    outVertexRefs.clear();

    const Core::Metascript::Value* field = FindRequiredMetadataListField(nwbFilePath, asset, metaKind, "vertex_refs");
    if(!field)
        return false;

    const auto& list = field->asList();
    const usize expectedComponentCount = includeSkin ? 6u : 5u;
    outVertexRefs.reserve(list.size());
    for(usize vertexRefIndex = 0u; vertexRefIndex < list.size(); ++vertexRefIndex){
        const Core::Metascript::Value& value = list[vertexRefIndex];
        if(!value.isList() || value.asList().size() != expectedComponentCount){
            NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': 'vertex_refs[{}]' must contain {} integer stream indices")
                , metaKind
                , PathToString<tchar>(nwbFilePath)
                , vertexRefIndex
                , expectedComponentCount
            );
            return false;
        }

        SourceGeometryVertexRef ref;
        const auto& components = value.asList();
        const ScratchString label = MakeIndexedLabel(scratchArena, "vertex_refs", vertexRefIndex);
        const AStringView componentNames[] = {
            "position",
            "normal",
            "tangent",
            "uv0",
            "color",
            "skin",
        };
        u32* const componentValues[] = {
            &ref.position,
            &ref.normal,
            &ref.tangent,
            &ref.uv0,
            &ref.color,
            &ref.skin,
        };
        for(usize componentIndex = 0u; componentIndex < expectedComponentCount; ++componentIndex){
            ScratchString componentLabel{scratchArena};
            componentLabel.reserve(label.size() + componentNames[componentIndex].size() + 3u);
            componentLabel.append(label.data(), label.size());
            componentLabel += '.';
            componentLabel.append(componentNames[componentIndex].data(), componentNames[componentIndex].size());
            if(!ParseSourceVertexRefValue(nwbFilePath, components[componentIndex], metaKind, componentLabel, *componentValues[componentIndex]))
                return false;
        }
        outVertexRefs.push_back(ref);
    }

    if(outVertexRefs.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': 'vertex_refs' must not be empty"), metaKind, PathToString<tchar>(nwbFilePath));
        return false;
    }
    return true;
}

static bool ValidateSourceStreamIndex(
    const Path& nwbFilePath,
    const tchar* metaKind,
    const AStringView streamName,
    const u32 index,
    const usize streamCount
){
    if(index < streamCount)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': vertex_ref {} index is out of range")
        , metaKind
        , PathToString<tchar>(nwbFilePath)
        , StringConvert(streamName)
    );
    return false;
}

static bool ValidateOptionalSourceStreamIndex(
    const Path& nwbFilePath,
    const tchar* metaKind,
    const AStringView streamName,
    const u32 index,
    const usize streamCount
){
    if(index == s_MissingSourceStreamIndex)
        return true;
    return ValidateSourceStreamIndex(nwbFilePath, metaKind, streamName, index, streamCount);
}

static bool ValidateSourceIndexStream(
    const Path& nwbFilePath,
    const tchar* metaKind,
    const Core::Assets::AssetVector<u32>& indices,
    const usize vertexRefCount
){
    if((indices.size() % 3u) != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': 'indices' must contain whole triangles")
            , metaKind
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }
    for(const u32 index : indices){
        if(index < vertexRefCount)
            continue;

        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': 'indices' references an out-of-range vertex_ref")
            , metaKind
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ParseSourceGeometryMeta(
    const DiscoveredNwbFile& discoveredFile,
    const Core::Metascript::Value& asset,
    GeometryCookEntry& outEntry,
    Core::Alloc::ScratchArena& scratchArena
){
    ScratchVector<Float3U> positions{scratchArena};
    ScratchVector<Float3U> normals{scratchArena};
    ScratchVector<Float4U> tangents{scratchArena};
    ScratchVector<Float2U> uv0{scratchArena};
    ScratchVector<Float4U> colors{scratchArena};
    ScratchVector<SourceGeometryVertexRef> vertexRefs{scratchArena};

    if(!ParseMetadataFloatListField<Float3U, 3u>(discoveredFile.filePath, asset, s_GeometryMetaKind, "positions", positions, scratchArena))
        return false;
    if(!ParseMetadataFloatListField<Float3U, 3u>(discoveredFile.filePath, asset, s_GeometryMetaKind, "normals", normals, scratchArena))
        return false;

    const Core::Metascript::Value* tangentsField = FindField(asset, "tangents");
    if(tangentsField && !ParseMetadataFloatListField<Float4U, 4u>(discoveredFile.filePath, asset, s_GeometryMetaKind, "tangents", tangents, scratchArena))
        return false;
    if(!ParseMetadataFloatListField<Float2U, 2u>(discoveredFile.filePath, asset, s_GeometryMetaKind, "uv0", uv0, scratchArena))
        return false;

    const Core::Metascript::Value* colorsField = FindField(asset, "colors");
    if(colorsField){
        if(!ParseMetadataFloatListField<Float4U, 4u>(discoveredFile.filePath, asset, s_GeometryMetaKind, "colors", colors, scratchArena))
            return false;
    }
    else
        BuildDefaultColors(1u, colors);

    if(!ParseSourceVertexRefs(discoveredFile.filePath, asset, s_GeometryMetaKind, false, vertexRefs, scratchArena))
        return false;

    outEntry.use32BitIndices = vertexRefs.size() > static_cast<usize>(Limit<u16>::s_Max);
    if(!ParseMetadataIndexField(discoveredFile.filePath, asset, s_GeometryMetaKind, true, outEntry.indices, scratchArena))
        return false;
    if(!ValidateSourceIndexStream(discoveredFile.filePath, s_GeometryMetaKind, outEntry.indices, vertexRefs.size()))
        return false;

    outEntry.positions.clear();
    outEntry.normals.clear();
    outEntry.colors.clear();
    outEntry.positions.reserve(vertexRefs.size());
    outEntry.normals.reserve(vertexRefs.size());
    outEntry.colors.reserve(vertexRefs.size());
    for(const SourceGeometryVertexRef& ref : vertexRefs){
        if(!ValidateSourceStreamIndex(discoveredFile.filePath, s_GeometryMetaKind, "position", ref.position, positions.size()))
            return false;
        if(!ValidateSourceStreamIndex(discoveredFile.filePath, s_GeometryMetaKind, "normal", ref.normal, normals.size()))
            return false;
        if(!ValidateOptionalSourceStreamIndex(discoveredFile.filePath, s_GeometryMetaKind, "tangent", ref.tangent, tangents.size()))
            return false;
        if(!ValidateSourceStreamIndex(discoveredFile.filePath, s_GeometryMetaKind, "uv0", ref.uv0, uv0.size()))
            return false;
        if(!ValidateSourceStreamIndex(discoveredFile.filePath, s_GeometryMetaKind, "color", ref.color, colors.size()))
            return false;
        if(ref.skin != s_MissingSourceStreamIndex){
            NWB_LOGGER_ERROR(NWB_TEXT("Geometry meta '{}': static vertex_ref cannot contain a skin index")
                , PathToString<tchar>(discoveredFile.filePath)
            );
            return false;
        }

        outEntry.positions.push_back(positions[ref.position]);
        outEntry.normals.push_back(MakeGeometryNormalStreamValue(normals[ref.normal]));
        outEntry.colors.push_back(MakeGeometryColorStreamValue(colors[ref.color]));
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ParseSkinInfluenceStream(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::AssetVector<SkinInfluence4>& outSkin,
    Core::Alloc::ScratchArena& scratchArena
){
    outSkin.clear();

    const Core::Metascript::Value* skin = FindField(asset, "skin");
    if(!skin)
        return true;
    if(!skin->isMap() || skin->asMap().empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometry geometry meta '{}': 'skin' must be a non-empty map")
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    const Core::Metascript::Value* joints = FindField(*skin, "joints0");
    const Core::Metascript::Value* weights = FindField(*skin, "weights0");
    if(!joints || !joints->isList() || !weights || !weights->isList()){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometry geometry meta '{}': 'skin' requires 'joints0' and 'weights0' lists")
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }
    const auto& jointList = joints->asList();
    const auto& weightList = weights->asList();
    if(jointList.empty() || jointList.size() != weightList.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometry geometry meta '{}': skin streams must be non-empty and match")
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    outSkin.reserve(jointList.size());
    for(usize i = 0; i < jointList.size(); ++i){
        const ScratchString jointLabel = MakeIndexedLabel(scratchArena, "skin.joints0", i);
        const ScratchString weightLabel = MakeIndexedLabel(scratchArena, "skin.weights0", i);
        SkinInfluence4 influence;
        if(!ParseU16Tuple(nwbFilePath, jointList[i], jointLabel, influence.joint, scratchArena))
            return false;
        if(!ParseMetadataF32Tuple(nwbFilePath, weightList[i], s_SkinnedGeometryMetaKind, weightLabel, influence.weight, scratchArena))
            return false;
        if(!NormalizeSkinInfluenceWeights(nwbFilePath, weightLabel, influence))
            return false;
        outSkin.push_back(influence);
    }
    return true;
}

static bool ParseSourceSkinnedGeometryMeta(
    const DiscoveredNwbFile& discoveredFile,
    const Core::Metascript::Value& asset,
    SkinnedGeometryCookEntry& outEntry,
    Core::Alloc::ScratchArena& scratchArena
){
    ScratchVector<Float3U> positions{scratchArena};
    ScratchVector<Float3U> normals{scratchArena};
    ScratchVector<Float4U> tangents{scratchArena};
    ScratchVector<Float2U> uv0{scratchArena};
    ScratchVector<Float4U> colors{scratchArena};
    ScratchVector<SourceGeometryVertexRef> vertexRefs{scratchArena};
    Core::Assets::AssetVector<SkinInfluence4> sourceSkin(outEntry.skin.get_allocator().arena());

    if(!ParseMetadataFloatListField<Float3U, 3u>(
        discoveredFile.filePath,
        asset,
        s_SkinnedGeometryMetaKind,
        "positions",
        positions,
        scratchArena
    ))
        return false;
    if(!ParseMetadataFloatListField<Float3U, 3u>(
        discoveredFile.filePath,
        asset,
        s_SkinnedGeometryMetaKind,
        "normals",
        normals,
        scratchArena
    ))
        return false;

    const Core::Metascript::Value* tangentsField = FindField(asset, "tangents");
    if(tangentsField && !ParseMetadataFloatListField<Float4U, 4u>(
        discoveredFile.filePath,
        asset,
        s_SkinnedGeometryMetaKind,
        "tangents",
        tangents,
        scratchArena
    ))
        return false;
    if(!ParseMetadataFloatListField<Float2U, 2u>(
        discoveredFile.filePath,
        asset,
        s_SkinnedGeometryMetaKind,
        "uv0",
        uv0,
        scratchArena
    ))
        return false;

    const Core::Metascript::Value* colorsField = FindField(asset, "colors");
    if(colorsField){
        if(!ParseMetadataFloatListField<Float4U, 4u>(
            discoveredFile.filePath,
            asset,
            s_SkinnedGeometryMetaKind,
            "colors",
            colors,
            scratchArena
        ))
            return false;
    }
    else
        BuildDefaultColors(1u, colors);

    if(!ParseSkinInfluenceStream(discoveredFile.filePath, asset, sourceSkin, scratchArena))
        return false;
    if(sourceSkin.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometry geometry meta '{}': 'skin' is required for skinned geometry")
            , PathToString<tchar>(discoveredFile.filePath)
        );
        return false;
    }
    if(!ParseSourceVertexRefs(discoveredFile.filePath, asset, s_SkinnedGeometryMetaKind, true, vertexRefs, scratchArena))
        return false;

    outEntry.use32BitIndices = vertexRefs.size() > static_cast<usize>(Limit<u16>::s_Max);
    if(!ParseMetadataIndexField(discoveredFile.filePath, asset, s_SkinnedGeometryMetaKind, true, outEntry.indices, scratchArena))
        return false;
    if(!ValidateSourceIndexStream(discoveredFile.filePath, s_SkinnedGeometryMetaKind, outEntry.indices, vertexRefs.size()))
        return false;
    if(!ParseSkeletonJointCount(discoveredFile.filePath, asset, outEntry.skeletonJointCount))
        return false;
    if(!ParseInverseBindMatrices(
        discoveredFile.filePath,
        asset,
        outEntry.skeletonJointCount,
        outEntry.inverseBindMatrices,
        scratchArena
    ))
        return false;

    outEntry.restVertices.clear();
    outEntry.skin.clear();
    outEntry.restVertices.reserve(vertexRefs.size());
    outEntry.skin.reserve(vertexRefs.size());

    bool tangentsProvided = !tangents.empty();
    for(const SourceGeometryVertexRef& ref : vertexRefs){
        if(!ValidateSourceStreamIndex(discoveredFile.filePath, s_SkinnedGeometryMetaKind, "position", ref.position, positions.size()))
            return false;
        if(!ValidateSourceStreamIndex(discoveredFile.filePath, s_SkinnedGeometryMetaKind, "normal", ref.normal, normals.size()))
            return false;
        if(!ValidateOptionalSourceStreamIndex(discoveredFile.filePath, s_SkinnedGeometryMetaKind, "tangent", ref.tangent, tangents.size()))
            return false;
        if(!ValidateSourceStreamIndex(discoveredFile.filePath, s_SkinnedGeometryMetaKind, "uv0", ref.uv0, uv0.size()))
            return false;
        if(!ValidateSourceStreamIndex(discoveredFile.filePath, s_SkinnedGeometryMetaKind, "color", ref.color, colors.size()))
            return false;
        if(!ValidateSourceStreamIndex(discoveredFile.filePath, s_SkinnedGeometryMetaKind, "skin", ref.skin, sourceSkin.size()))
            return false;

        Float4U tangent(1.0f, 0.0f, 0.0f, 1.0f);
        if(ref.tangent == s_MissingSourceStreamIndex)
            tangentsProvided = false;
        else
            tangent = tangents[ref.tangent];

        outEntry.restVertices.push_back(MakeSkinnedGeometryVertex(
            positions[ref.position],
            normals[ref.normal],
            tangent,
            uv0[ref.uv0],
            colors[ref.color]
        ));
        outEntry.skin.push_back(sourceSkin[ref.skin]);
    }

    return GenerateMissingSkinnedGeometryFrames(
        discoveredFile.filePath,
        true,
        tangentsProvided,
        outEntry.restVertices,
        outEntry.indices,
        scratchArena
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

