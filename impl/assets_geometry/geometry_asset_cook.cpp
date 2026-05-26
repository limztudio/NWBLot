// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "geometry_asset_cook.h"

#include "skinned_geometry_validation.h"
#include "skinned_geometry_tangent_frame_rebuild.h"
#include "geometry_asset_binary_payload.h"
#include "geometry_binary_payload.h"

#include <core/alloc/scratch.h>
#include <core/assets/asset_paths.h>
#include <core/geometry/frame_math.h>
#include <core/metascript/parser.h>
#include <global/binary.h>
#include <global/text_utils.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_geometry_asset_cook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
using ScratchVector = Vector<T, Core::Alloc::ScratchArena>;
using ScratchString = AString<Core::Alloc::ScratchArena>;

struct DiscoveredNwbFile{
    Path assetRoot;
    ACompactString virtualRoot;
    Path filePath;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool BuildDiscoveredNwbFile(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    DiscoveredNwbFile& outFile
){
    outFile.assetRoot = assetRoot;
    outFile.virtualRoot.clear();
    outFile.filePath = nwbFilePath;
    if(!outFile.virtualRoot.assign(virtualRoot)){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry meta '{}': virtual root exceeds ACompactString capacity")
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }
    return true;
}

static bool AccumulateFlattenedValueLeafCount(const Core::Metascript::Value& value, usize& inOutCount){
    if(value.isList()){
        for(const Core::Metascript::Value& child : value.asList()){
            if(!AccumulateFlattenedValueLeafCount(child, inOutCount))
                return false;
        }
        return true;
    }

    if(inOutCount == Limit<usize>::s_Max)
        return false;

    ++inOutCount;
    return true;
}

static bool CountFlattenedValueLeaves(const Core::Metascript::Value& value, usize& outCount){
    outCount = 0u;
    return AccumulateFlattenedValueLeafCount(value, outCount);
}

static const Core::Metascript::Value* FindField(const Core::Metascript::Value& map, const AStringView fieldName){
    return map.findField(Core::Metascript::MStringView(fieldName.data(), fieldName.size()));
}

static ScratchString MakeIndexedLabel(Core::Alloc::ScratchArena& arena, const AStringView baseLabel, const usize index){
    char indexBuffer[32] = {};
    const AStringView indexText = FormatDecimal(index, indexBuffer);
    NWB_ASSERT(!indexText.empty());

    ScratchString label{arena};
    label.reserve(baseLabel.size() + indexText.size() + 2u);
    label.append(baseLabel.data(), baseLabel.size());
    label += '[';
    label.append(indexText.data(), indexText.size());
    label += ']';
    return label;
}

namespace MetadataF32ValueFailure{
    enum Enum : u8{
        None,
        NotNumeric,
        NonFinite,
        OutOfRange
    };
};

namespace MetadataU32ValueFailure{
    enum Enum : u8{
        None,
        NotNumeric,
        NonIntegerOrNegative,
        OutOfRange
    };
};

static constexpr const tchar* s_GeometryMetaKind = NWB_TEXT("Geometry");
static constexpr const tchar* s_SkinnedGeometryMetaKind = NWB_TEXT("SkinnedGeometry geometry");

static const Core::Metascript::Value* FindRequiredMetadataListField(
    const Path& nwbFilePath,
    const Core::Metascript::Value& map,
    const tchar* metaKind,
    const AStringView fieldName){
    const Core::Metascript::Value* field = FindField(map, fieldName);
    if(field && field->isList())
        return field;

    NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' must be a list")
        , metaKind
        , PathToString<tchar>(nwbFilePath)
        , StringConvert(fieldName)
    );
    return nullptr;
}

static MetadataF32ValueFailure::Enum ValidateMetadataFiniteF32Value(const Core::Metascript::Value& value, f32& outValue){
    if(!value.isNumeric())
        return MetadataF32ValueFailure::NotNumeric;

    const f64 numericValue = value.toDouble();
    if(!IsFinite(numericValue))
        return MetadataF32ValueFailure::NonFinite;
    if(numericValue < static_cast<f64>(Limit<f32>::s_Min) || numericValue > static_cast<f64>(Limit<f32>::s_Max))
        return MetadataF32ValueFailure::OutOfRange;

    outValue = static_cast<f32>(numericValue);
    return MetadataF32ValueFailure::None;
}

static void LogMetadataFiniteF32ValueFailure(
    const Path& nwbFilePath,
    const tchar* metaKind,
    const AStringView label,
    const MetadataF32ValueFailure::Enum failure
){
    switch(failure){
    case MetadataF32ValueFailure::NotNumeric:
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' must contain only numeric values")
            , metaKind
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
        );
        return;

    case MetadataF32ValueFailure::NonFinite:
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' must contain only finite numeric values")
            , metaKind
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
        );
        return;

    case MetadataF32ValueFailure::OutOfRange:
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' contains a value outside the f32 range")
            , metaKind
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
        );
        return;

    default:
        return;
    }
}

template<usize ComponentCount>
static bool ParseMetadataF32TupleWithLabel(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const tchar* metaKind,
    const AStringView label,
    f32 (&outValues)[ComponentCount],
    Core::Alloc::ScratchArena& scratchArena
){
    if(!value.isList() || value.asList().size() != ComponentCount){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' must be a {}-component list")
            , metaKind
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
            , ComponentCount
        );
        return false;
    }

    const auto& list = value.asList();
    for(usize i = 0; i < ComponentCount; ++i){
        const MetadataF32ValueFailure::Enum failure = ValidateMetadataFiniteF32Value(list[i], outValues[i]);
        if(failure == MetadataF32ValueFailure::None)
            continue;

        const ScratchString componentLabel = MakeIndexedLabel(scratchArena, label, i);
        LogMetadataFiniteF32ValueFailure(nwbFilePath, metaKind, componentLabel, failure);
        return false;
    }
    return true;
}

template<usize ComponentCount>
static bool ParseMetadataF32Tuple(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const tchar* metaKind,
    const AStringView label,
    f32 (&outValues)[ComponentCount],
    Core::Alloc::ScratchArena& scratchArena
){
    return ParseMetadataF32TupleWithLabel(nwbFilePath, value, metaKind, label, outValues, scratchArena);
}

template<usize ComponentCount>
static bool ParseMetadataF32TupleListElement(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const tchar* metaKind,
    const AStringView fieldName,
    const usize elementIndex,
    f32 (&outValues)[ComponentCount],
    Core::Alloc::ScratchArena& scratchArena
){
    const ScratchString label = MakeIndexedLabel(scratchArena, fieldName, elementIndex);
    return ParseMetadataF32TupleWithLabel(nwbFilePath, value, metaKind, label, outValues, scratchArena);
}

template<typename ElementT, usize ComponentCount, typename ElementVectorT>
static bool ParseMetadataFloatListField(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const tchar* metaKind,
    const AStringView fieldName,
    ElementVectorT& outValues,
    Core::Alloc::ScratchArena& scratchArena
){
    outValues.clear();

    const Core::Metascript::Value* field = FindRequiredMetadataListField(nwbFilePath, asset, metaKind, fieldName);
    if(!field)
        return false;

    const auto& list = field->asList();
    outValues.reserve(list.size());
    for(usize i = 0; i < list.size(); ++i){
        alignas(16) f32 tuple[ComponentCount] = {};
        if(!ParseMetadataF32TupleListElement(nwbFilePath, list[i], metaKind, fieldName, i, tuple, scratchArena)){
            outValues.clear();
            return false;
        }

        ElementT element;
        element.x = tuple[0];
        element.y = tuple[1];
        if constexpr(ComponentCount >= 3u)
            element.z = tuple[2];
        if constexpr(ComponentCount >= 4u)
            element.w = tuple[3];
        outValues.push_back(element);
    }

    if(outValues.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' must not be empty")
            , metaKind
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }

    return true;
}

static MetadataU32ValueFailure::Enum ValidateMetadataU32Value(const Core::Metascript::Value& value, u32& outValue){
    if(!value.isNumeric())
        return MetadataU32ValueFailure::NotNumeric;

    const f64 numericValue = value.toDouble();
    if(!IsFinite(numericValue) || numericValue < 0.0 || numericValue != Floor(numericValue))
        return MetadataU32ValueFailure::NonIntegerOrNegative;
    if(numericValue > static_cast<f64>(Limit<u32>::s_Max))
        return MetadataU32ValueFailure::OutOfRange;

    outValue = static_cast<u32>(numericValue);
    return MetadataU32ValueFailure::None;
}

static void LogMetadataU32ValueFailure(
    const Path& nwbFilePath,
    const tchar* metaKind,
    const AStringView label,
    const MetadataU32ValueFailure::Enum failure
){
    switch(failure){
    case MetadataU32ValueFailure::NotNumeric:
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' must contain only integer values")
            , metaKind
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
        );
        return;

    case MetadataU32ValueFailure::NonIntegerOrNegative:
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' contains a non-integer or negative value")
            , metaKind
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
        );
        return;

    case MetadataU32ValueFailure::OutOfRange:
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' contains a value that exceeds u32")
            , metaKind
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
        );
        return;

    default:
        return;
    }
}

static bool ParseMetadataU32Value(
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

static bool ParseMetadataIndexType(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const tchar* metaKind,
    bool& outUse32BitIndices
){
    outUse32BitIndices = true;

    const Core::Metascript::Value* indexType = FindField(asset, "index_type");
    if(!indexType || !indexType->isString()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': 'index_type' must be 'u16' or 'u32'")
            , metaKind
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    const AStringView indexTypeText(indexType->asString().data(), indexType->asString().size());
    if(indexTypeText == "u16"){
        outUse32BitIndices = false;
        return true;
    }
    if(indexTypeText == "u32"){
        outUse32BitIndices = true;
        return true;
    }

    NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': unsupported index_type '{}'")
        , metaKind
        , PathToString<tchar>(nwbFilePath)
        , StringConvert(indexTypeText)
    );
    return false;
}

template<typename IndexVectorT>
static bool FillMetadataIndexRecursive(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const tchar* metaKind,
    const AStringView label,
    const bool use32BitIndices,
    IndexVectorT& outIndices,
    Core::Alloc::ScratchArena& scratchArena
){
    if(value.isList()){
        const auto& list = value.asList();
        for(usize i = 0; i < list.size(); ++i){
            const ScratchString childLabel = MakeIndexedLabel(scratchArena, label, i);
            if(!FillMetadataIndexRecursive(nwbFilePath, list[i], metaKind, childLabel, use32BitIndices, outIndices, scratchArena))
                return false;
        }
        return true;
    }

    u32 index = 0;
    if(!ParseMetadataU32Value(nwbFilePath, value, metaKind, label, index))
        return false;
    if(!use32BitIndices && index > Limit<u16>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' contains a value that exceeds u16 index_type")
            , metaKind
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
        );
        return false;
    }

    using IndexValue = typename IndexVectorT::value_type;
    outIndices.push_back(static_cast<IndexValue>(index));
    return true;
}

template<typename IndexVectorT>
static bool ParseMetadataIndexField(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const tchar* metaKind,
    const bool use32BitIndices,
    IndexVectorT& outIndices,
    Core::Alloc::ScratchArena& scratchArena
){
    outIndices.clear();

    const Core::Metascript::Value* field = FindRequiredMetadataListField(nwbFilePath, asset, metaKind, "indices");
    if(!field)
        return false;

    usize indexCount = 0u;
    if(!CountFlattenedValueLeaves(*field, indexCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': 'indices' scalar count overflows")
            , metaKind
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    outIndices.reserve(indexCount);
    if(!FillMetadataIndexRecursive(nwbFilePath, *field, metaKind, "indices", use32BitIndices, outIndices, scratchArena)){
        outIndices.clear();
        return false;
    }
    NWB_ASSERT(outIndices.size() == indexCount);
    if(outIndices.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': 'indices' must not be empty"), metaKind, PathToString<tchar>(nwbFilePath));
        return false;
    }
    return true;
}

template<typename ColorVectorT>
static void BuildDefaultColors(const usize vertexCount, ColorVectorT& outColors){
    outColors.assign(vertexCount, Float4U(1.f, 1.f, 1.f, 1.f));
}

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

template<typename PositionVectorT, typename NormalVectorT, typename ColorVectorT>
static bool BuildGeometryStreams(
    const Path& nwbFilePath,
    const PositionVectorT& positions,
    const NormalVectorT& normals,
    const ColorVectorT& colors,
    Core::Assets::AssetVector<Float3U>& outPositions,
    Core::Assets::AssetVector<Half4U>& outNormals,
    Core::Assets::AssetVector<Half4U>& outColors
){
    if(positions.size() != normals.size() || positions.size() != colors.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry meta '{}': vertex stream counts must match"), PathToString<tchar>(nwbFilePath));
        return false;
    }

    outPositions.clear();
    outNormals.clear();
    outColors.clear();
    outPositions.reserve(positions.size());
    outNormals.reserve(positions.size());
    outColors.reserve(positions.size());
    for(usize i = 0; i < positions.size(); ++i){
        outPositions.push_back(positions[i]);
        outNormals.push_back(MakeGeometryNormalStreamValue(normals[i]));
        outColors.push_back(MakeGeometryColorStreamValue(colors[i]));
    }
    return true;
}

static bool RejectDeprecatedGeometryClassField(
    const DiscoveredNwbFile& discoveredFile,
    const Core::Metascript::Value& asset,
    const tchar* metaKind
){
    if(!FindField(asset, "geometry_class"))
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': 'geometry_class' is no longer supported; use 'geometry asset' or 'skinned_geometry asset'")
        , metaKind
        , PathToString<tchar>(discoveredFile.filePath)
    );
    return false;
}

static bool RejectSkinnedFieldsOnStaticGeometry(const DiscoveredNwbFile& discoveredFile, const Core::Metascript::Value& asset){
    if(!FindField(asset, "skin") && !FindField(asset, "skeleton_joint_count") && !FindField(asset, "inverse_bind_matrices"))
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("Geometry meta '{}': skinned fields require a 'skinned_geometry asset' header")
        , PathToString<tchar>(discoveredFile.filePath)
    );
    return false;
}

static bool RejectUnsupportedGeometryFields(const DiscoveredNwbFile& discoveredFile, const Core::Metascript::Value& asset){
    if(
        !asset.findField("primitive")
        && !asset.findField("vertex_stride")
        && !asset.findField("vertex_data")
        && !asset.findField("index_data")
    )
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("Geometry meta '{}': unsupported geometry fields are present; define positions, normals, optional colors, index_type, and indices")
        , PathToString<tchar>(discoveredFile.filePath)
    );
    return false;
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
    if(!RejectUnsupportedGeometryFields(discoveredFile, asset))
        return false;
    if(!RejectDeprecatedGeometryClassField(discoveredFile, asset, s_GeometryMetaKind))
        return false;
    if(!RejectSkinnedFieldsOnStaticGeometry(discoveredFile, asset))
        return false;
    if(FindField(asset, "vertex_refs"))
        return ParseSourceGeometryMeta(discoveredFile, asset, outEntry, scratchArena);

    ScratchVector<Float3U> positions{scratchArena};
    ScratchVector<Float3U> normals{scratchArena};
    ScratchVector<Float4U> colors{scratchArena};
    if(!ParseMetadataIndexType(discoveredFile.filePath, asset, s_GeometryMetaKind, outEntry.use32BitIndices))
        return false;
    if(!ParseMetadataFloatListField<Float3U, 3u>(discoveredFile.filePath, asset, s_GeometryMetaKind, "positions", positions, scratchArena))
        return false;
    if(!ParseMetadataFloatListField<Float3U, 3u>(discoveredFile.filePath, asset, s_GeometryMetaKind, "normals", normals, scratchArena))
        return false;
    const Core::Metascript::Value* colorsField = FindField(asset, "colors");
    if(colorsField){
        if(!ParseMetadataFloatListField<Float4U, 4u>(discoveredFile.filePath, asset, s_GeometryMetaKind, "colors", colors, scratchArena))
            return false;
    }
    else
        BuildDefaultColors(positions.size(), colors);

    if(!BuildGeometryStreams(
        discoveredFile.filePath,
        positions,
        normals,
        colors,
        outEntry.positions,
        outEntry.normals,
        outEntry.colors
    ))
        return false;
    return ParseMetadataIndexField(discoveredFile.filePath, asset, s_GeometryMetaKind, outEntry.use32BitIndices, outEntry.indices, scratchArena);
}

static bool BuildGeometryAsset(GeometryCookEntry& geometryEntry, Geometry& outGeometry){
    outGeometry = Geometry(geometryEntry.positions.get_allocator().arena(), geometryEntry.virtualPath);
    outGeometry.setStreams(Move(geometryEntry.positions), Move(geometryEntry.normals), Move(geometryEntry.colors));
    outGeometry.setIndices(Move(geometryEntry.indices));
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

template<typename ElementT, usize ComponentCount, typename ElementVectorT>
static bool ParseOptionalFloatListField(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
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
        s_SkinnedGeometryMetaKind,
        fieldName,
        outValues,
        scratchArena
    );
}

template<typename PositionVectorT, typename NormalVectorT, typename TangentVectorT, typename UvVectorT, typename ColorVectorT>
static bool BuildGeometryRestVertices(
    const Path& nwbFilePath,
    const PositionVectorT& positions,
    const NormalVectorT& normals,
    const TangentVectorT& tangents,
    const UvVectorT& uv0,
    const ColorVectorT& colors,
    Core::Assets::AssetVector<SkinnedGeometryVertex>& outVertices
){
    if(
        positions.size() != normals.size()
        || positions.size() != tangents.size()
        || positions.size() != uv0.size()
        || positions.size() != colors.size()
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometry geometry meta '{}': rest vertex stream counts must match")
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    outVertices.clear();
    outVertices.reserve(positions.size());
    for(usize i = 0; i < positions.size(); ++i){
        outVertices.push_back(MakeSkinnedGeometryVertex(
            positions[i],
            normals[i],
            tangents[i],
            uv0[i],
            colors[i]
        ));
    }
    return true;
}

static bool GenerateFallbackSkinnedGeometryTangents(Core::Assets::AssetVector<SkinnedGeometryVertex>& vertices){
    for(SkinnedGeometryVertex& vertex : vertices){
        const SIMDVector normal = Core::Geometry::FrameNormalizeDirection(
            LoadSkinnedGeometryVertexNormal(vertex),
            VectorSet(0.0f, 0.0f, 1.0f, 0.0f)
        );
        if(!Core::Geometry::FrameValidDirection(normal))
            return false;

        Float4U tangent;
        StoreFloat(VectorSetW(Core::Geometry::FrameFallbackTangent(normal), 1.0f), &tangent);
        StoreSkinnedGeometryVertexTangent(vertex, tangent);
        if(SkinnedGeometryValidation::FindRestVertexPayloadFailure(vertex) != SkinnedGeometryValidation::RestVertexPayloadFailure::None)
            return false;
    }
    return true;
}

static bool GenerateMissingSkinnedGeometryFrames(
    const Path& nwbFilePath,
    const bool normalsProvided,
    const bool tangentsProvided,
    Core::Assets::AssetVector<SkinnedGeometryVertex>& vertices,
    const Core::Assets::AssetVector<u32>& indices,
    Core::Alloc::ScratchArena& scratchArena
){
    if(normalsProvided && tangentsProvided)
        return true;
    if(normalsProvided && !tangentsProvided){
        if(GenerateFallbackSkinnedGeometryTangents(vertices))
            return true;

        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometry geometry meta '{}': failed to generate fallback tangents")
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    Core::Geometry::TangentFrameRebuildResult rebuildResult;
    if(!SkinnedGeometryTangentFrameRebuild::Rebuild(vertices, indices, &rebuildResult, scratchArena)){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometry geometry meta '{}': failed to generate missing normal/tangent frames")
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
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

static bool ParseSkinInfluences(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const usize vertexCount,
    Core::Assets::AssetVector<SkinInfluence4>& outSkin,
    Core::Alloc::ScratchArena& scratchArena
){
    outSkin.clear();

    const Core::Metascript::Value* skin = FindField(asset, "skin");
    if(!skin)
        return true;
    if(!skin->isMap()){
        if((skin->isList() && skin->asList().empty()) || (skin->isMap() && skin->asMap().empty()))
            return true;

        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometry geometry meta '{}': 'skin' must be a map"), PathToString<tchar>(nwbFilePath));
        return false;
    }
    if(skin->asMap().empty())
        return true;

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
    if(jointList.size() != vertexCount || weightList.size() != vertexCount){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometry geometry meta '{}': skin streams must match vertex count")
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    outSkin.reserve(vertexCount);
    for(usize i = 0; i < vertexCount; ++i){
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

static bool RejectSkinnedGeometrySourceField(const DiscoveredNwbFile& discoveredFile, const Core::Metascript::Value& asset){
    if(!FindField(asset, "source"))
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometry geometry meta '{}': external 'source' imports are not supported; use an offline converter to emit native .nwb streams")
        , PathToString<tchar>(discoveredFile.filePath)
    );
    return false;
}

#include "geometry_asset_cook_source.inl"

static bool ParseSkinnedGeometryMeta(
    const DiscoveredNwbFile& discoveredFile,
    const Core::Metascript::Document& doc,
    SkinnedGeometryCookEntry& outEntry,
    Core::Alloc::ScratchArena& scratchArena
){
    outEntry = SkinnedGeometryCookEntry(outEntry.restVertices.get_allocator().arena());

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

    if(!RejectDeprecatedGeometryClassField(discoveredFile, asset, s_SkinnedGeometryMetaKind))
        return false;
    if(!RejectSkinnedGeometrySourceField(discoveredFile, asset))
        return false;
    outEntry.geometryClass = Core::Geometry::GeometryClass::Skinned;
    if(FindField(asset, "vertex_refs"))
        return ParseSourceSkinnedGeometryMeta(discoveredFile, asset, outEntry, scratchArena);

    ScratchVector<Float3U> positions{scratchArena};
    ScratchVector<Float3U> normals{scratchArena};
    ScratchVector<Float4U> tangents{scratchArena};
    ScratchVector<Float2U> uv0{scratchArena};
    ScratchVector<Float4U> colors{scratchArena};
    if(!ParseMetadataIndexType(discoveredFile.filePath, asset, s_SkinnedGeometryMetaKind, outEntry.use32BitIndices))
        return false;
    if(!ParseMetadataFloatListField<Float3U, 3u>(
        discoveredFile.filePath,
        asset,
        s_SkinnedGeometryMetaKind,
        "positions",
        positions,
        scratchArena
    ))
        return false;
    bool normalsProvided = false;
    bool tangentsProvided = false;
    if(!ParseOptionalFloatListField<Float3U, 3u>(
        discoveredFile.filePath,
        asset,
        "normals",
        normals,
        normalsProvided,
        scratchArena
    ))
        return false;
    if(!ParseOptionalFloatListField<Float4U, 4u>(
        discoveredFile.filePath,
        asset,
        "tangents",
        tangents,
        tangentsProvided,
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
    if(!ParseMetadataIndexField(
        discoveredFile.filePath,
        asset,
        s_SkinnedGeometryMetaKind,
        outEntry.use32BitIndices,
        outEntry.indices,
        scratchArena
    ))
        return false;
    if(!normalsProvided)
        normals.assign(positions.size(), Float3U(0.0f, 0.0f, 1.0f));
    if(!tangentsProvided)
        tangents.assign(positions.size(), Float4U(1.0f, 0.0f, 0.0f, 1.0f));
    const Core::Metascript::Value* colorsField = FindField(asset, "colors");
    if(colorsField
        && !(colorsField->isList() && colorsField->asList().empty())
        && !(colorsField->isMap() && colorsField->asMap().empty())
    ){
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
        BuildDefaultColors(positions.size(), colors);
    if(!BuildGeometryRestVertices(discoveredFile.filePath, positions, normals, tangents, uv0, colors, outEntry.restVertices))
        return false;
    if(!GenerateMissingSkinnedGeometryFrames(
        discoveredFile.filePath,
        normalsProvided,
        tangentsProvided,
        outEntry.restVertices,
        outEntry.indices,
        scratchArena
    ))
        return false;
    if(!ParseSkinInfluences(discoveredFile.filePath, asset, outEntry.restVertices.size(), outEntry.skin, scratchArena))
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
    if(outEntry.skin.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometry geometry meta '{}': 'skin' is required for skinned geometry")
            , PathToString<tchar>(discoveredFile.filePath)
        );
        return false;
    }
    return true;
}

static bool BuildSkinnedGeometryAsset(SkinnedGeometryCookEntry& geometryEntry, SkinnedGeometry& outGeometry){
    outGeometry = SkinnedGeometry(geometryEntry.restVertices.get_allocator().arena(), geometryEntry.virtualPath);

    outGeometry.setGeometryClass(geometryEntry.geometryClass);
    outGeometry.setRestVertices(Move(geometryEntry.restVertices));
    outGeometry.setIndices(Move(geometryEntry.indices));
    outGeometry.setSkin(Move(geometryEntry.skin));
    outGeometry.setSkeletonJointCount(geometryEntry.skeletonJointCount);
    outGeometry.setInverseBindMatrices(Move(geometryEntry.inverseBindMatrices));
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
    const bool canReserve = AddBinaryVectorReserveBytes(reserveBytes, geometry.positions())
        && AddBinaryVectorReserveBytes(reserveBytes, geometry.normals())
        && AddBinaryVectorReserveBytes(reserveBytes, geometry.colors())
        && AddBinaryVectorReserveBytes(reserveBytes, geometry.indices())
    ;

    outBinary.clear();
    if(canReserve)
        outBinary.reserve(reserveBytes);

    GeometryBinaryPayload::GeometryHeaderBinary header;
    header.vertexCount = static_cast<u64>(geometry.vertexCount());
    header.indexCount = static_cast<u64>(geometry.indices().size());
    AppendPOD(outBinary, header);
    const tchar* const serializeFailureContext = NWB_TEXT("GeometryAssetCodec::serialize");
    auto appendVector = [&](const auto& values, const tchar* label){
        return GeometryAssetBinaryPayload::AppendVector(outBinary, values, serializeFailureContext, label);
    };
    if(!appendVector(geometry.positions(), NWB_TEXT("positions")))
        return false;
    if(!appendVector(geometry.normals(), NWB_TEXT("normals")))
        return false;
    if(!appendVector(geometry.colors(), NWB_TEXT("colors")))
        return false;

    return appendVector(geometry.indices(), NWB_TEXT("indices"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

