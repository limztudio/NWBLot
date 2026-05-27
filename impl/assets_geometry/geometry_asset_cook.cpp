// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "geometry_asset_cook.h"

#include "skinned_geometry_validation.h"
#include "geometry_asset_binary_payload.h"
#include "geometry_binary_payload.h"

#include <core/alloc/scratch.h>
#include <core/assets/asset_paths.h>
#include <core/geometry/frame_math.h>
#include <core/geometry/tangent_frame_rebuild.h>
#include <core/metascript/parser.h>
#include <global/binary.h>
#include <global/text_utils.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_geometry_asset_cook{


#include "geometry_asset_cook_metadata.inl"

static bool ParseSourceGeometryMeta(
    const DiscoveredNwbFile& discoveredFile,
    const Core::Metascript::Value& asset,
    GeometryCookEntry& outEntry,
    Core::Alloc::ScratchArena& scratchArena
);
static bool ParseSourceSkinnedGeometryMeta(
    const DiscoveredNwbFile& discoveredFile,
    const Core::Metascript::Value& asset,
    SkinnedGeometryCookEntry& outEntry,
    Core::Alloc::ScratchArena& scratchArena
);

static bool ValidateGeometryAssetFields(
    const DiscoveredNwbFile& discoveredFile,
    const Core::Metascript::Value& asset
){
    return Core::Assets::ValidateMetadataAssetFields(
        discoveredFile.filePath,
        asset,
        "Geometry meta",
        { "positions", "normals", "tangents", "uv0", "colors", "vertex_refs", "indices" }
    );
}

static bool ValidateSkinnedGeometryAssetFields(
    const DiscoveredNwbFile& discoveredFile,
    const Core::Metascript::Value& asset
){
    return Core::Assets::ValidateMetadataAssetFields(
        discoveredFile.filePath,
        asset,
        "SkinnedGeometry geometry meta",
        {
            "positions",
            "normals",
            "tangents",
            "uv0",
            "colors",
            "vertex_refs",
            "indices",
            "skin",
            "skeleton_joint_count",
            "inverse_bind_matrices",
        }
    );
}

static bool ParseGeometryMeta(
    const DiscoveredNwbFile& discoveredFile,
    const Core::Metascript::Document& doc,
    GeometryCookEntry& outEntry,
    Core::Alloc::ScratchArena& scratchArena
){
    outEntry = GeometryCookEntry(outEntry.positions.get_allocator().arena());

    const Core::Metascript::Value& asset = doc.asset();
    if(!asset.isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry meta '{}': asset is not a map"), PathToString<tchar>(discoveredFile.filePath));
        return false;
    }

    if(!Core::Assets::BuildMetadataDerivedAssetVirtualPath(
        discoveredFile.assetRoot,
        discoveredFile.virtualRoot,
        discoveredFile.filePath,
        asset,
        "Geometry",
        outEntry.virtualPath,
        scratchArena
    ))
        return false;
    if(!ValidateGeometryAssetFields(discoveredFile, asset))
        return false;
    return ParseSourceGeometryMeta(discoveredFile, asset, outEntry, scratchArena);
}

static bool BuildGeometryAsset(GeometryCookEntry& geometryEntry, Geometry& outGeometry){
    outGeometry = Geometry(geometryEntry.positions.get_allocator().arena(), geometryEntry.virtualPath);
    outGeometry.setPayload(
        Move(geometryEntry.positions),
        Move(geometryEntry.normals),
        Move(geometryEntry.tangents),
        Move(geometryEntry.uv0),
        Move(geometryEntry.colors),
        Move(geometryEntry.vertexRefs),
        Move(geometryEntry.meshlets),
        Move(geometryEntry.meshletBounds),
        Move(geometryEntry.meshletVertexRefs),
        Move(geometryEntry.meshletPrimitiveIndices)
    );
    return outGeometry.validatePayload();
}

template<usize ComponentCount>
static bool ParseU16Tuple(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const AStringView label,
    u16 (&outValues)[ComponentCount],
    Core::Alloc::ScratchArena& scratchArena
){
    if(!value.isList() || value.asList().size() != ComponentCount){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometry geometry meta '{}': '{}' must be a {}-component integer list")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
            , ComponentCount
        );
        return false;
    }

    const auto& list = value.asList();
    for(usize i = 0; i < ComponentCount; ++i){
        u32 parsed = 0u;
        const MetadataU32ValueFailure::Enum failure = ValidateMetadataU32Value(list[i], parsed);
        if(failure != MetadataU32ValueFailure::None){
            const ScratchString componentLabel = MakeIndexedLabel(scratchArena, label, i);
            LogMetadataU32ValueFailure(nwbFilePath, s_SkinnedGeometryMetaKind, componentLabel, failure);
            return false;
        }
        if(parsed > Limit<u16>::s_Max){
            const ScratchString componentLabel = MakeIndexedLabel(scratchArena, label, i);
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometry geometry meta '{}': '{}' contains a value that exceeds u16")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(componentLabel)
            );
            return false;
        }
        outValues[i] = static_cast<u16>(parsed);
    }
    return true;
}

static bool NormalizeSkinInfluenceWeights(
    const Path& nwbFilePath,
    const AStringView label,
    SkinInfluence4& influence
){
    f32 weightSum = 0.0f;
    for(u32 influenceIndex = 0u; influenceIndex < 4u; ++influenceIndex){
        const f32 weight = influence.weight[influenceIndex];
        if(!IsFinite(weight) || weight < 0.0f){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometry geometry meta '{}': '{}' weights must be finite and non-negative")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(label)
            );
            return false;
        }
        weightSum += weight;
    }

    if(!IsFinite(weightSum) || weightSum <= SkinnedGeometryValidation::s_Epsilon){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometry geometry meta '{}': '{}' weights must contain a positive total")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
        );
        return false;
    }

    const f32 inverseWeightSum = 1.0f / weightSum;
    for(f32& weight : influence.weight)
        weight *= inverseWeightSum;

    if(!SkinnedGeometryValidation::ValidSkinInfluence(influence)){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometry geometry meta '{}': '{}' weights failed normalization")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
        );
        return false;
    }
    return true;
}

static bool ParseSkeletonJointCount(const Path& nwbFilePath, const Core::Metascript::Value& asset, u32& outJointCount){
    outJointCount = 0u;

    const Core::Metascript::Value* jointCount = FindField(asset, "skeleton_joint_count");
    if(!jointCount)
        return true;

    return ParseMetadataU32Value(nwbFilePath, *jointCount, s_SkinnedGeometryMetaKind, "skeleton_joint_count", outJointCount);
}

static bool ParseInverseBindMatrices(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const u32 skeletonJointCount,
    Core::Assets::AssetVector<SkinnedGeometryJointMatrix>& outMatrices,
    Core::Alloc::ScratchArena& scratchArena
){
    outMatrices.clear();

    const Core::Metascript::Value* matrices = FindField(asset, "inverse_bind_matrices");
    if(!matrices || (matrices->isList() && matrices->asList().empty()) || (matrices->isMap() && matrices->asMap().empty()))
        return true;
    if(!matrices->isList()){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometry geometry meta '{}': 'inverse_bind_matrices' must be a list")
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }
    if(skeletonJointCount == 0u || matrices->asList().size() != skeletonJointCount){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometry geometry meta '{}': inverse bind matrix count must match skeleton_joint_count")
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    const auto& matrixList = matrices->asList();
    outMatrices.reserve(matrixList.size());
    for(usize matrixIndex = 0u; matrixIndex < matrixList.size(); ++matrixIndex){
        const Core::Metascript::Value& matrixValue = matrixList[matrixIndex];
        if(!matrixValue.isList() || matrixValue.asList().size() != 4u){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometry geometry meta '{}': inverse_bind_matrices[{}] must contain four columns")
                , PathToString<tchar>(nwbFilePath)
                , matrixIndex
            );
            return false;
        }

        SkinnedGeometryJointMatrix matrix{};
        const auto& columns = matrixValue.asList();
        const ScratchString label = MakeIndexedLabel(scratchArena, "inverse_bind_matrices", matrixIndex);
        for(usize columnIndex = 0u; columnIndex < 4u; ++columnIndex){
            alignas(16) f32 column[4] = {};
            const ScratchString columnLabel = MakeIndexedLabel(scratchArena, label, columnIndex);
            if(!ParseMetadataF32Tuple(nwbFilePath, columns[columnIndex], s_SkinnedGeometryMetaKind, columnLabel, column, scratchArena))
                return false;
            matrix.rows[columnIndex] = Float4(column[0u], column[1u], column[2u], column[3u]);
        }

        if(!SkinnedGeometryValidation::ValidAffineJointMatrix(matrix)){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometry geometry meta '{}': inverse_bind_matrices[{}] is not a finite invertible affine matrix")
                , PathToString<tchar>(nwbFilePath)
                , matrixIndex
            );
            return false;
        }
        outMatrices.push_back(matrix);
    }
    return true;
}

#include "geometry_asset_cook_source.inl"

static bool ParseSkinnedGeometryMeta(
    const DiscoveredNwbFile& discoveredFile,
    const Core::Metascript::Document& doc,
    SkinnedGeometryCookEntry& outEntry,
    Core::Alloc::ScratchArena& scratchArena
){
    outEntry = SkinnedGeometryCookEntry(outEntry.positions.get_allocator().arena());

    const Core::Metascript::Value& asset = doc.asset();
    if(!asset.isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometry geometry meta '{}': asset is not a map")
            , PathToString<tchar>(discoveredFile.filePath)
        );
        return false;
    }

    if(!Core::Assets::BuildMetadataDerivedAssetVirtualPath(
        discoveredFile.assetRoot,
        discoveredFile.virtualRoot,
        discoveredFile.filePath,
        asset,
        "SkinnedGeometry",
        outEntry.virtualPath,
        scratchArena
    ))
        return false;

    if(!ValidateSkinnedGeometryAssetFields(discoveredFile, asset))
        return false;
    outEntry.geometryClass = Core::Geometry::GeometryClass::Skinned;
    return ParseSourceSkinnedGeometryMeta(discoveredFile, asset, outEntry, scratchArena);
}

static bool BuildSkinnedGeometryAsset(SkinnedGeometryCookEntry& geometryEntry, SkinnedGeometry& outGeometry){
    outGeometry = SkinnedGeometry(geometryEntry.positions.get_allocator().arena(), geometryEntry.virtualPath);

    outGeometry.setGeometryClass(geometryEntry.geometryClass);
    outGeometry.setSkeletonJointCount(geometryEntry.skeletonJointCount);
    outGeometry.setPayload(
        Move(geometryEntry.positions),
        Move(geometryEntry.normals),
        Move(geometryEntry.tangents),
        Move(geometryEntry.uv0),
        Move(geometryEntry.colors),
        Move(geometryEntry.skin),
        Move(geometryEntry.inverseBindMatrices),
        Move(geometryEntry.vertexRefs),
        Move(geometryEntry.meshlets),
        Move(geometryEntry.meshletBounds),
        Move(geometryEntry.meshletVertexRefs),
        Move(geometryEntry.meshletPrimitiveIndices)
    );
    return outGeometry.validatePayload();
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ParseGeometryCookMetadata(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    GeometryCookEntry& outEntry,
    Core::Alloc::ScratchArena& scratchArena
){
    __hidden_geometry_asset_cook::DiscoveredNwbFile discoveredFile;
    if(!__hidden_geometry_asset_cook::BuildDiscoveredNwbFile(assetRoot, virtualRoot, nwbFilePath, discoveredFile))
        return false;
    return __hidden_geometry_asset_cook::ParseGeometryMeta(discoveredFile, doc, outEntry, scratchArena);
}

bool ParseSkinnedGeometryCookMetadata(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    SkinnedGeometryCookEntry& outEntry,
    Core::Alloc::ScratchArena& scratchArena
){
    __hidden_geometry_asset_cook::DiscoveredNwbFile discoveredFile;
    if(!__hidden_geometry_asset_cook::BuildDiscoveredNwbFile(assetRoot, virtualRoot, nwbFilePath, discoveredFile))
        return false;
    return __hidden_geometry_asset_cook::ParseSkinnedGeometryMeta(discoveredFile, doc, outEntry, scratchArena);
}

bool BuildGeometryAsset(GeometryCookEntry& geometryEntry, Geometry& outGeometry){
    return __hidden_geometry_asset_cook::BuildGeometryAsset(geometryEntry, outGeometry);
}

bool BuildSkinnedGeometryAsset(SkinnedGeometryCookEntry& geometryEntry, SkinnedGeometry& outGeometry){
    return __hidden_geometry_asset_cook::BuildSkinnedGeometryAsset(geometryEntry, outGeometry);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GeometryAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(asset.assetType() != assetType()){
        NWB_LOGGER_ERROR(NWB_TEXT("GeometryAssetCodec::serialize failed: invalid asset type '{}', expected '{}'")
            , StringConvert(asset.assetType().c_str())
            , StringConvert(Geometry::s_AssetTypeText)
        );
        return false;
    }

    const Geometry& geometry = static_cast<const Geometry&>(asset);
    if(!geometry.validatePayload())
        return false;

    usize reserveBytes = sizeof(GeometryBinaryPayload::GeometryHeaderBinary);
    const bool canReserve = GeometryAssetBinaryPayload::AddGeometryBaseReserveBytes(reserveBytes, geometry);

    outBinary.clear();
    if(canReserve)
        outBinary.reserve(reserveBytes);

    GeometryBinaryPayload::GeometryHeaderBinary header;
    GeometryAssetBinaryPayload::FillGeometryBaseHeader(header, geometry);
    AppendPOD(outBinary, header);

    const tchar* const serializeFailureContext = NWB_TEXT("GeometryAssetCodec::serialize");
    if(!GeometryAssetBinaryPayload::AppendGeometryAttributeStreams(outBinary, geometry, serializeFailureContext))
        return false;
    return GeometryAssetBinaryPayload::AppendGeometryMeshletStreams(outBinary, geometry, serializeFailureContext);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

