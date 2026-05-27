// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SourceGeometryStreams{
    ScratchVector<Float3U> positions;
    ScratchVector<Float3U> normals;
    ScratchVector<Float4U> tangents;
    ScratchVector<Float2U> uv0;
    ScratchVector<Float4U> colors;
    ScratchVector<GeometryVertexRef> vertexRefs;
    Core::Assets::AssetVector<u32> indices;
    bool tangentsProvided = false;

    SourceGeometryStreams(Core::Assets::AssetArena& assetArena, Core::Alloc::ScratchArena& scratchArena)
        : positions(scratchArena)
        , normals(scratchArena)
        , tangents(scratchArena)
        , uv0(scratchArena)
        , colors(scratchArena)
        , vertexRefs(scratchArena)
        , indices(assetArena)
    {}
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
    ScratchVector<GeometryVertexRef>& outVertexRefs,
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

        GeometryVertexRef ref;
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
            componentLabel.reserve(label.size() + componentNames[componentIndex].size() + 2u);
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

template<typename ElementT, usize ComponentCount, typename ElementVectorT>
static bool ParseOptionalSourceFloatListField(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const tchar* metaKind,
    const AStringView fieldName,
    ElementVectorT& outValues,
    bool& outProvided,
    Core::Alloc::ScratchArena& scratchArena
){
    outValues.clear();
    outProvided = false;

    const Core::Metascript::Value* field = FindField(asset, fieldName);
    if(!field || (field->isList() && field->asList().empty()) || (field->isMap() && field->asMap().empty()))
        return true;

    outProvided = true;
    return ParseMetadataFloatListField<ElementT, ComponentCount>(
        nwbFilePath,
        asset,
        metaKind,
        fieldName,
        outValues,
        scratchArena
    );
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

static bool ValidateSourceIndexStream(
    const Path& nwbFilePath,
    const tchar* metaKind,
    const Core::Assets::AssetVector<u32>& indices,
    const usize vertexRefCount
){
    if(indices.empty() || (indices.size() % 3u) != 0u){
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

static bool ValidateSourceVertexRefs(
    const Path& nwbFilePath,
    const tchar* metaKind,
    const bool includeSkin,
    const SourceGeometryStreams& streams,
    const usize skinCount
){
    for(const GeometryVertexRef& ref : streams.vertexRefs){
        if(!ValidateSourceStreamIndex(nwbFilePath, metaKind, "position", ref.position, streams.positions.size()))
            return false;
        if(!ValidateSourceStreamIndex(nwbFilePath, metaKind, "normal", ref.normal, streams.normals.size()))
            return false;
        if(streams.tangentsProvided && !ValidateSourceStreamIndex(nwbFilePath, metaKind, "tangent", ref.tangent, streams.tangents.size()))
            return false;
        if(!streams.tangentsProvided && ref.tangent != s_GeometryMissingStreamIndex){
            NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': vertex_ref tangent must be UINT32_MAX when 'tangents' is omitted")
                , metaKind
                , PathToString<tchar>(nwbFilePath)
            );
            return false;
        }
        if(!ValidateSourceStreamIndex(nwbFilePath, metaKind, "uv0", ref.uv0, streams.uv0.size()))
            return false;
        if(!ValidateSourceStreamIndex(nwbFilePath, metaKind, "color", ref.color, streams.colors.size()))
            return false;
        if(includeSkin){
            if(!ValidateSourceStreamIndex(nwbFilePath, metaKind, "skin", ref.skin, skinCount))
                return false;
        }
        else if(ref.skin != s_GeometryMissingStreamIndex){
            NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': static vertex_ref cannot contain a skin index")
                , metaKind
                , PathToString<tchar>(nwbFilePath)
            );
            return false;
        }
    }
    return true;
}

template<typename CookEntryT>
static void CopySourceStreams(SourceGeometryStreams& streams, CookEntryT& outEntry){
    outEntry.positions.clear();
    outEntry.normals.clear();
    outEntry.tangents.clear();
    outEntry.uv0.clear();
    outEntry.colors.clear();
    outEntry.vertexRefs.clear();

    outEntry.positions.reserve(streams.positions.size());
    outEntry.normals.reserve(streams.normals.size());
    outEntry.tangents.reserve(streams.tangents.size());
    outEntry.uv0.reserve(streams.uv0.size());
    outEntry.colors.reserve(streams.colors.size());
    outEntry.vertexRefs.reserve(streams.vertexRefs.size());

    outEntry.positions.insert(outEntry.positions.end(), streams.positions.begin(), streams.positions.end());
    for(const Float3U& normal : streams.normals)
        outEntry.normals.push_back(MakeGeometryNormalStreamValue(normal));
    for(const Float4U& tangent : streams.tangents)
        outEntry.tangents.push_back(MakeHalf4U(tangent.x, tangent.y, tangent.z, tangent.w));
    outEntry.uv0.insert(outEntry.uv0.end(), streams.uv0.begin(), streams.uv0.end());
    for(const Float4U& color : streams.colors)
        outEntry.colors.push_back(MakeGeometryColorStreamValue(color));
    outEntry.vertexRefs.insert(outEntry.vertexRefs.end(), streams.vertexRefs.begin(), streams.vertexRefs.end());
}

template<typename CookEntryT>
static u32 FindOrAppendTangent(CookEntryT& entry, const Float4U& tangent){
    const Half4U packedTangent = MakeHalf4U(tangent.x, tangent.y, tangent.z, tangent.w);
    for(usize tangentIndex = 0u; tangentIndex < entry.tangents.size(); ++tangentIndex){
        if(NWB_MEMCMP(&entry.tangents[tangentIndex], &packedTangent, sizeof(Half4U)) == 0)
            return static_cast<u32>(tangentIndex);
    }

    const u32 tangentIndex = static_cast<u32>(entry.tangents.size());
    entry.tangents.push_back(packedTangent);
    return tangentIndex;
}

template<typename CookEntryT>
static bool GenerateMissingTangents(
    const Path& nwbFilePath,
    const tchar* metaKind,
    const Core::Assets::AssetVector<u32>& indices,
    CookEntryT& entry,
    Core::Alloc::ScratchArena& scratchArena
){
    if(!entry.tangents.empty())
        return true;

    using RebuildVertex = Core::Geometry::TangentFrameRebuildVertex;
    ScratchVector<RebuildVertex> rebuildVertices{scratchArena};
    rebuildVertices.reserve(entry.vertexRefs.size());
    for(const GeometryVertexRef& ref : entry.vertexRefs){
        rebuildVertices.push_back(RebuildVertex{
            Float4(entry.positions[ref.position].x, entry.positions[ref.position].y, entry.positions[ref.position].z, 0.0f),
            Float4(LoadHalf4U(entry.normals[ref.normal]).x, LoadHalf4U(entry.normals[ref.normal]).y, LoadHalf4U(entry.normals[ref.normal]).z, 0.0f),
            Float4(1.0f, 0.0f, 0.0f, 1.0f),
            entry.uv0[ref.uv0],
        });
    }

    Core::Geometry::TangentFrameRebuildResult rebuildResult;
    if(!Core::Geometry::RebuildTangentFrames(rebuildVertices, indices, &rebuildResult)){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': failed to generate missing tangent stream")
            , metaKind
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    for(usize vertexRefIndex = 0u; vertexRefIndex < entry.vertexRefs.size(); ++vertexRefIndex){
        const GeometryVertexRef& ref = entry.vertexRefs[vertexRefIndex];
        const SIMDVector normal = Core::Geometry::FrameNormalizeDirection(
            VectorSetW(LoadFloat(LoadHalf4U(entry.normals[ref.normal])), 0.0f),
            VectorSet(0.0f, 0.0f, 1.0f, 0.0f)
        );
        const SIMDVector tangent = Core::Geometry::FrameResolveTangent(
            normal,
            VectorSetW(LoadFloat(rebuildVertices[vertexRefIndex].tangent), 0.0f),
            Core::Geometry::FrameFallbackTangent(normal)
        );
        if(!Core::Geometry::FrameValidDirection(tangent)){
            NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': failed to resolve generated tangent")
                , metaKind
                , PathToString<tchar>(nwbFilePath)
            );
            return false;
        }

        const f32 handedness = Core::Geometry::FrameTangentHandedness(rebuildVertices[vertexRefIndex].tangent.w, 1.0f);
        Float4U generatedTangent;
        StoreFloat(VectorSetW(tangent, handedness), &generatedTangent);

        const u32 tangentIndex = FindOrAppendTangent(entry, generatedTangent);
        entry.vertexRefs[vertexRefIndex].tangent = tangentIndex;
    }
    return true;
}

#include "geometry_asset_cook_meshlet.inl"

static bool ParseCommonSourceGeometryStreams(
    const DiscoveredNwbFile& discoveredFile,
    const Core::Metascript::Value& asset,
    const tchar* metaKind,
    const bool includeSkin,
    SourceGeometryStreams& streams,
    const usize skinCount,
    Core::Alloc::ScratchArena& scratchArena
){
    if(!ParseMetadataFloatListField<Float3U, 3u>(discoveredFile.filePath, asset, metaKind, "positions", streams.positions, scratchArena))
        return false;
    if(!ParseMetadataFloatListField<Float3U, 3u>(discoveredFile.filePath, asset, metaKind, "normals", streams.normals, scratchArena))
        return false;
    if(!ParseOptionalSourceFloatListField<Float4U, 4u>(
        discoveredFile.filePath,
        asset,
        metaKind,
        "tangents",
        streams.tangents,
        streams.tangentsProvided,
        scratchArena
    ))
        return false;
    if(!ParseMetadataFloatListField<Float2U, 2u>(discoveredFile.filePath, asset, metaKind, "uv0", streams.uv0, scratchArena))
        return false;

    if(!ParseMetadataFloatListField<Float4U, 4u>(discoveredFile.filePath, asset, metaKind, "colors", streams.colors, scratchArena))
        return false;

    if(!ParseSourceVertexRefs(discoveredFile.filePath, asset, metaKind, includeSkin, streams.vertexRefs, scratchArena))
        return false;
    if(!ParseMetadataIndexField(discoveredFile.filePath, asset, metaKind, streams.indices, scratchArena))
        return false;
    if(!ValidateSourceIndexStream(discoveredFile.filePath, metaKind, streams.indices, streams.vertexRefs.size()))
        return false;
    return ValidateSourceVertexRefs(discoveredFile.filePath, metaKind, includeSkin, streams, skinCount);
}

static bool ParseSourceGeometryMeta(
    const DiscoveredNwbFile& discoveredFile,
    const Core::Metascript::Value& asset,
    GeometryCookEntry& outEntry,
    Core::Alloc::ScratchArena& scratchArena
){
    SourceGeometryStreams streams(outEntry.positions.get_allocator().arena(), scratchArena);
    if(!ParseCommonSourceGeometryStreams(discoveredFile, asset, s_GeometryMetaKind, false, streams, 0u, scratchArena))
        return false;

    CopySourceStreams(streams, outEntry);
    if(!GenerateMissingTangents(discoveredFile.filePath, s_GeometryMetaKind, streams.indices, outEntry, scratchArena))
        return false;
    return BuildMeshlets(discoveredFile.filePath, s_GeometryMetaKind, streams.indices, outEntry);
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
    if(!skin || !skin->isMap() || skin->asMap().empty()){
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
    for(usize i = 0u; i < jointList.size(); ++i){
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
    if(!ParseSkinInfluenceStream(discoveredFile.filePath, asset, outEntry.skin, scratchArena))
        return false;

    SourceGeometryStreams streams(outEntry.positions.get_allocator().arena(), scratchArena);
    if(!ParseCommonSourceGeometryStreams(
        discoveredFile,
        asset,
        s_SkinnedGeometryMetaKind,
        true,
        streams,
        outEntry.skin.size(),
        scratchArena
    ))
        return false;
    if(!ParseSkeletonJointCount(discoveredFile.filePath, asset, outEntry.skeletonJointCount))
        return false;
    if(outEntry.skeletonJointCount == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometry geometry meta '{}': 'skeleton_joint_count' is required for skinned geometry")
            , PathToString<tchar>(discoveredFile.filePath)
        );
        return false;
    }
    if(!ParseInverseBindMatrices(
        discoveredFile.filePath,
        asset,
        outEntry.skeletonJointCount,
        outEntry.inverseBindMatrices,
        scratchArena
    ))
        return false;
    if(outEntry.inverseBindMatrices.size() != outEntry.skeletonJointCount){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometry geometry meta '{}': 'inverse_bind_matrices' is required and must match skeleton_joint_count")
            , PathToString<tchar>(discoveredFile.filePath)
        );
        return false;
    }

    CopySourceStreams(streams, outEntry);
    if(!GenerateMissingTangents(discoveredFile.filePath, s_SkinnedGeometryMetaKind, streams.indices, outEntry, scratchArena))
        return false;
    return BuildMeshlets(discoveredFile.filePath, s_SkinnedGeometryMetaKind, streams.indices, outEntry);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

