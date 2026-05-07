// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "geometry_asset_cook.h"

#include "deformable_geometry_validation.h"
#include "geometry_binary_payload.h"

#include <core/alloc/scratch.h>
#include <core/assets/asset_paths.h>
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
using ScratchVector = Vector<T, Core::Alloc::ScratchAllocator<T>>;

struct DiscoveredNwbFile{
    Path assetRoot;
    CompactString virtualRoot;
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
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry meta '{}': virtual root exceeds CompactString capacity")
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

static AString MakeIndexedLabel(const AStringView baseLabel, const usize index){
    char indexBuffer[32] = {};
    const AStringView indexText = FormatDecimal(index, indexBuffer);
    NWB_ASSERT(!indexText.empty());

    AString label;
    label.reserve(baseLabel.size() + indexText.size() + 2u);
    label.append(baseLabel.data(), baseLabel.size());
    label += '[';
    label.append(indexText.data(), indexText.size());
    label += ']';
    return label;
}

static constexpr const tchar* s_GeometryMetaKind = NWB_TEXT("Geometry");
static constexpr const tchar* s_DeformableGeometryMetaKind = NWB_TEXT("Deformable geometry");

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

static bool ParseMetadataFiniteF32Value(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const tchar* metaKind,
    const AStringView label,
    f32& outValue
){
    if(!value.isNumeric()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' must contain only numeric values")
            , metaKind
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
        );
        return false;
    }

    const f64 numericValue = value.toDouble();
    if(!IsFinite(numericValue)){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' must contain only finite numeric values")
            , metaKind
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
        );
        return false;
    }
    if(numericValue < static_cast<f64>(Limit<f32>::s_Min) || numericValue > static_cast<f64>(Limit<f32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' contains a value outside the f32 range")
            , metaKind
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
        );
        return false;
    }

    outValue = static_cast<f32>(numericValue);
    return true;
}

template<usize ComponentCount>
static bool ParseMetadataF32Tuple(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const tchar* metaKind,
    const AStringView label,
    f32 (&outValues)[ComponentCount]
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
        const AString componentLabel = MakeIndexedLabel(label, i);
        if(!ParseMetadataFiniteF32Value(nwbFilePath, list[i], metaKind, componentLabel, outValues[i]))
            return false;
    }
    return true;
}

template<typename ElementT, usize ComponentCount, typename ElementVectorT>
static bool ParseMetadataFloatListField(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const tchar* metaKind,
    const AStringView fieldName,
    ElementVectorT& outValues
){
    outValues.clear();

    const Core::Metascript::Value* field = FindRequiredMetadataListField(nwbFilePath, asset, metaKind, fieldName);
    if(!field)
        return false;

    const auto& list = field->asList();
    outValues.reserve(list.size());
    for(usize i = 0; i < list.size(); ++i){
        const AString label = MakeIndexedLabel(fieldName, i);
        alignas(16) f32 tuple[ComponentCount] = {};
        if(!ParseMetadataF32Tuple(nwbFilePath, list[i], metaKind, label, tuple)){
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

static bool ParseMetadataU32Value(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const tchar* metaKind,
    const AStringView label,
    u32& outValue
){
    if(!value.isNumeric()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' must contain only integer values")
            , metaKind
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
        );
        return false;
    }

    const f64 numericValue = value.toDouble();
    if(!IsFinite(numericValue) || numericValue < 0.0 || numericValue != Floor(numericValue)){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' contains a non-integer or negative value")
            , metaKind
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
        );
        return false;
    }
    if(numericValue > static_cast<f64>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' contains a value that exceeds u32")
            , metaKind
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
        );
        return false;
    }

    outValue = static_cast<u32>(numericValue);
    return true;
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

static bool ParseGeometryClassField(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const tchar* metaKind,
    u32& outGeometryClass
){
    const Core::Metascript::Value* geometryClass = FindField(asset, "geometry_class");
    if(!geometryClass){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': 'geometry_class' is required")
            , metaKind
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    if(!geometryClass->isString()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': 'geometry_class' must be a string")
            , metaKind
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    const Core::Metascript::MStringView text = geometryClass->asString();
    const AStringView classText(text.data(), text.size());
    if(ParseGeometryClassText(classText, outGeometryClass))
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': unsupported geometry_class '{}'")
        , metaKind
        , PathToString<tchar>(nwbFilePath)
        , StringConvert(classText)
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
    IndexVectorT& outIndices
){
    if(value.isList()){
        const auto& list = value.asList();
        for(usize i = 0; i < list.size(); ++i){
            const AString childLabel = MakeIndexedLabel(label, i);
            if(!FillMetadataIndexRecursive(nwbFilePath, list[i], metaKind, childLabel, use32BitIndices, outIndices))
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
    IndexVectorT& outIndices
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
    if(!FillMetadataIndexRecursive(nwbFilePath, *field, metaKind, "indices", use32BitIndices, outIndices)){
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

template<typename PositionVectorT, typename NormalVectorT, typename ColorVectorT>
static bool BuildGeometryVertices(
    const Path& nwbFilePath,
    const PositionVectorT& positions,
    const NormalVectorT& normals,
    const ColorVectorT& colors,
    Vector<GeometryVertex>& outVertices
){
    if(positions.size() != normals.size() || positions.size() != colors.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry meta '{}': vertex stream counts must match"), PathToString<tchar>(nwbFilePath));
        return false;
    }

    outVertices.clear();
    outVertices.reserve(positions.size());
    for(usize i = 0; i < positions.size(); ++i){
        GeometryVertex vertex;
        vertex.position = positions[i];
        vertex.normal = normals[i];
        vertex.color0 = colors[i];
        outVertices.push_back(vertex);
    }
    return true;
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

static bool ParseGeometryMeta(const DiscoveredNwbFile& discoveredFile, const Core::Metascript::Document& doc, GeometryCookEntry& outEntry){
    outEntry = {};

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
        outEntry.virtualPath
    ))
        return false;
    if(!RejectUnsupportedGeometryFields(discoveredFile, asset))
        return false;
    u32 geometryClass = GeometryClass::Invalid;
    if(!ParseGeometryClassField(
        discoveredFile.filePath,
        asset,
        s_GeometryMetaKind,
        geometryClass
    ))
        return false;
    if(geometryClass != GeometryClass::Static){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry meta '{}': geometry_class must be 'static' for geometry assets")
            , PathToString<tchar>(discoveredFile.filePath)
        );
        return false;
    }

    Core::Alloc::ScratchArena<> scratchArena;
    ScratchVector<Float3U> positions{Core::Alloc::ScratchAllocator<Float3U>(scratchArena)};
    ScratchVector<Float3U> normals{Core::Alloc::ScratchAllocator<Float3U>(scratchArena)};
    ScratchVector<Float4U> colors{Core::Alloc::ScratchAllocator<Float4U>(scratchArena)};
    if(!ParseMetadataIndexType(discoveredFile.filePath, asset, s_GeometryMetaKind, outEntry.use32BitIndices))
        return false;
    if(!ParseMetadataFloatListField<Float3U, 3u>(discoveredFile.filePath, asset, s_GeometryMetaKind, "positions", positions))
        return false;
    if(!ParseMetadataFloatListField<Float3U, 3u>(discoveredFile.filePath, asset, s_GeometryMetaKind, "normals", normals))
        return false;
    const Core::Metascript::Value* colorsField = FindField(asset, "colors");
    if(colorsField){
        if(!ParseMetadataFloatListField<Float4U, 4u>(discoveredFile.filePath, asset, s_GeometryMetaKind, "colors", colors))
            return false;
    }
    else
        BuildDefaultColors(positions.size(), colors);

    if(!BuildGeometryVertices(discoveredFile.filePath, positions, normals, colors, outEntry.vertices))
        return false;
    return ParseMetadataIndexField(discoveredFile.filePath, asset, s_GeometryMetaKind, outEntry.use32BitIndices, outEntry.indices);
}

static bool BuildGeometryAsset(GeometryCookEntry& geometryEntry, Geometry& outGeometry){
    outGeometry = Geometry(geometryEntry.virtualPath);
    outGeometry.setVertices(Move(geometryEntry.vertices));
    outGeometry.setIndices(Move(geometryEntry.indices));
    return outGeometry.validatePayload();
}

static bool ParseFiniteF32Value(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const AStringView label,
    f32& outValue
){
    return ParseMetadataFiniteF32Value(nwbFilePath, value, s_DeformableGeometryMetaKind, label, outValue);
}

static bool ParseU32Value(const Path& nwbFilePath, const Core::Metascript::Value& value, const AStringView label, u32& outValue){
    return ParseMetadataU32Value(nwbFilePath, value, s_DeformableGeometryMetaKind, label, outValue);
}

static bool ParseU16Value(const Path& nwbFilePath, const Core::Metascript::Value& value, const AStringView label, u16& outValue){
    u32 parsed = 0;
    if(!ParseU32Value(nwbFilePath, value, label, parsed))
        return false;
    if(parsed > Limit<u16>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("Deformable geometry meta '{}': '{}' contains a value that exceeds u16")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
        );
        return false;
    }

    outValue = static_cast<u16>(parsed);
    return true;
}

static const Core::Metascript::Value* FindRequiredListField(
    const Path& nwbFilePath,
    const Core::Metascript::Value& map,
    const AStringView fieldName){
    return FindRequiredMetadataListField(nwbFilePath, map, s_DeformableGeometryMetaKind, fieldName);
}

template<usize ComponentCount>
static bool ParseF32Tuple(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const AStringView label,
    f32 (&outValues)[ComponentCount]
){
    return ParseMetadataF32Tuple(nwbFilePath, value, s_DeformableGeometryMetaKind, label, outValues);
}

template<usize ComponentCount>
static bool ParseU16Tuple(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const AStringView label,
    u16 (&outValues)[ComponentCount]
){
    if(!value.isList() || value.asList().size() != ComponentCount){
        NWB_LOGGER_ERROR(NWB_TEXT("Deformable geometry meta '{}': '{}' must be a {}-component integer list")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
            , ComponentCount
        );
        return false;
    }

    const auto& list = value.asList();
    for(usize i = 0; i < ComponentCount; ++i){
        const AString componentLabel = MakeIndexedLabel(label, i);
        if(!ParseU16Value(nwbFilePath, list[i], componentLabel, outValues[i]))
            return false;
    }
    return true;
}

template<typename ElementT, usize ComponentCount, typename ElementVectorT>
static bool ParseFloatListField(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const AStringView fieldName,
    ElementVectorT& outValues
){
    return ParseMetadataFloatListField<ElementT, ComponentCount>(nwbFilePath, asset, s_DeformableGeometryMetaKind, fieldName, outValues);
}

template<typename ValueVectorT>
static bool ParseU32ListField(
    const Path& nwbFilePath,
    const Core::Metascript::Value& map,
    const AStringView fieldName,
    ValueVectorT& outValues
){
    outValues.clear();

    const Core::Metascript::Value* field = FindRequiredListField(nwbFilePath, map, fieldName);
    if(!field)
        return false;

    const auto& list = field->asList();
    outValues.reserve(list.size());
    for(usize i = 0; i < list.size(); ++i){
        u32 value = 0;
        const AString label = MakeIndexedLabel(fieldName, i);
        if(!ParseU32Value(nwbFilePath, list[i], label, value)){
            outValues.clear();
            return false;
        }
        outValues.push_back(value);
    }

    return true;
}

static bool IsExplicitEmptyOptionalField(const Core::Metascript::Value& value){
    return (value.isList() && value.asList().empty()) || (value.isMap() && value.asMap().empty());
}

template<typename ElementT, usize ComponentCount, typename ElementVectorT>
static bool ParseOptionalFloatListField(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const AStringView fieldName,
    ElementVectorT& outValues,
    bool& outProvided){
    outValues.clear();
    outProvided = false;

    const Core::Metascript::Value* field = FindField(asset, fieldName);
    if(!field || IsExplicitEmptyOptionalField(*field))
        return true;

    outProvided = true;
    return ParseFloatListField<ElementT, ComponentCount>(nwbFilePath, asset, fieldName, outValues);
}

static bool ParseDeformableIndexType(const Path& nwbFilePath, const Core::Metascript::Value& asset, bool& outUse32BitIndices){
    return ParseMetadataIndexType(nwbFilePath, asset, s_DeformableGeometryMetaKind, outUse32BitIndices);
}

static bool ParseDeformableIndexField(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const bool use32BitIndices,
    Vector<u32>& outIndices
){
    return ParseMetadataIndexField(nwbFilePath, asset, s_DeformableGeometryMetaKind, use32BitIndices, outIndices);
}

template<typename PositionVectorT, typename NormalVectorT, typename TangentVectorT, typename UvVectorT, typename ColorVectorT>
static bool BuildDeformableRestVertices(
    const Path& nwbFilePath,
    const PositionVectorT& positions,
    const NormalVectorT& normals,
    const TangentVectorT& tangents,
    const UvVectorT& uv0,
    const ColorVectorT& colors,
    Vector<DeformableVertexRest>& outVertices
){
    if(
        positions.size() != normals.size()
        || positions.size() != tangents.size()
        || positions.size() != uv0.size()
        || positions.size() != colors.size()
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Deformable geometry meta '{}': rest vertex stream counts must match")
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    outVertices.clear();
    outVertices.reserve(positions.size());
    for(usize i = 0; i < positions.size(); ++i){
        DeformableVertexRest vertex;
        vertex.position = positions[i];
        vertex.normal = normals[i];
        vertex.tangent = tangents[i];
        vertex.uv0 = uv0[i];
        vertex.color0 = colors[i];
        outVertices.push_back(vertex);
    }
    return true;
}

static bool GenerateMissingDeformableFrames(
    const Path& nwbFilePath,
    const bool normalsProvided,
    const bool tangentsProvided,
    Vector<DeformableVertexRest>& vertices,
    const Vector<u32>& indices){
    if(normalsProvided && tangentsProvided)
        return true;

    Core::Geometry::TangentFrameRebuildResult rebuildResult;
    if(!DeformableValidation::RebuildRestVertexTangentFrames(vertices, indices, &rebuildResult)){
        NWB_LOGGER_ERROR(NWB_TEXT("Deformable geometry meta '{}': failed to generate missing normal/tangent frames")
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
            NWB_LOGGER_ERROR(NWB_TEXT("Deformable geometry meta '{}': '{}' weights must be finite and non-negative")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(label)
            );
            return false;
        }
        weightSum += weight;
    }

    if(!IsFinite(weightSum) || weightSum <= DeformableValidation::s_Epsilon){
        NWB_LOGGER_ERROR(NWB_TEXT("Deformable geometry meta '{}': '{}' weights must contain a positive total")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
        );
        return false;
    }

    const f32 inverseWeightSum = 1.0f / weightSum;
    for(f32& weight : influence.weight)
        weight *= inverseWeightSum;

    if(!DeformableValidation::ValidSkinInfluence(influence)){
        NWB_LOGGER_ERROR(NWB_TEXT("Deformable geometry meta '{}': '{}' weights failed normalization")
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
    Vector<SkinInfluence4>& outSkin
){
    outSkin.clear();

    const Core::Metascript::Value* skin = FindField(asset, "skin");
    if(!skin)
        return true;
    if(!skin->isMap()){
        if(IsExplicitEmptyOptionalField(*skin))
            return true;

        NWB_LOGGER_ERROR(NWB_TEXT("Deformable geometry meta '{}': 'skin' must be a map"), PathToString<tchar>(nwbFilePath));
        return false;
    }
    if(skin->asMap().empty())
        return true;

    const Core::Metascript::Value* joints = FindField(*skin, "joints0");
    const Core::Metascript::Value* weights = FindField(*skin, "weights0");
    if(!joints || !joints->isList() || !weights || !weights->isList()){
        NWB_LOGGER_ERROR(NWB_TEXT("Deformable geometry meta '{}': 'skin' requires 'joints0' and 'weights0' lists")
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }
    const auto& jointList = joints->asList();
    const auto& weightList = weights->asList();
    if(jointList.size() != vertexCount || weightList.size() != vertexCount){
        NWB_LOGGER_ERROR(NWB_TEXT("Deformable geometry meta '{}': skin streams must match vertex count")
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    outSkin.reserve(vertexCount);
    for(usize i = 0; i < vertexCount; ++i){
        const AString jointLabel = MakeIndexedLabel("skin.joints0", i);
        const AString weightLabel = MakeIndexedLabel("skin.weights0", i);
        SkinInfluence4 influence;
        if(!ParseU16Tuple(nwbFilePath, jointList[i], jointLabel, influence.joint))
            return false;
        if(!ParseF32Tuple(nwbFilePath, weightList[i], weightLabel, influence.weight))
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

    return ParseU32Value(nwbFilePath, *jointCount, "skeleton_joint_count", outJointCount);
}

static bool ParseInverseBindMatrices(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const u32 skeletonJointCount,
    Vector<DeformableJointMatrix>& outMatrices){
    outMatrices.clear();

    const Core::Metascript::Value* matrices = FindField(asset, "inverse_bind_matrices");
    if(!matrices || IsExplicitEmptyOptionalField(*matrices))
        return true;
    if(!matrices->isList()){
        NWB_LOGGER_ERROR(NWB_TEXT("Deformable geometry meta '{}': 'inverse_bind_matrices' must be a list")
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }
    if(skeletonJointCount == 0u || matrices->asList().size() != skeletonJointCount){
        NWB_LOGGER_ERROR(NWB_TEXT("Deformable geometry meta '{}': inverse bind matrix count must match skeleton_joint_count")
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    const auto& matrixList = matrices->asList();
    outMatrices.reserve(matrixList.size());
    for(usize matrixIndex = 0u; matrixIndex < matrixList.size(); ++matrixIndex){
        const Core::Metascript::Value& matrixValue = matrixList[matrixIndex];
        if(!matrixValue.isList() || matrixValue.asList().size() != 4u){
            NWB_LOGGER_ERROR(NWB_TEXT("Deformable geometry meta '{}': inverse_bind_matrices[{}] must contain four columns")
                , PathToString<tchar>(nwbFilePath)
                , matrixIndex
            );
            return false;
        }

        DeformableJointMatrix matrix{};
        const auto& columns = matrixValue.asList();
        const AString label = MakeIndexedLabel("inverse_bind_matrices", matrixIndex);
        for(usize columnIndex = 0u; columnIndex < 4u; ++columnIndex){
            alignas(16) f32 column[4] = {};
            const AString columnLabel = MakeIndexedLabel(label, columnIndex);
            if(!ParseMetadataF32Tuple(nwbFilePath, columns[columnIndex], s_DeformableGeometryMetaKind, columnLabel, column)){
                return false;
            }
            matrix.rows[columnIndex] = Float4(column[0u], column[1u], column[2u], column[3u]);
        }

        if(!DeformableValidation::ValidAffineJointMatrix(matrix)){
            NWB_LOGGER_ERROR(NWB_TEXT("Deformable geometry meta '{}': inverse_bind_matrices[{}] is not a finite invertible affine matrix")
                , PathToString<tchar>(nwbFilePath)
                , matrixIndex
            );
            return false;
        }
        outMatrices.push_back(matrix);
    }
    return true;
}

static bool ParseSourceSamples(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const usize vertexCount,
    Vector<SourceSample>& outSourceSamples,
    bool& outSourceSamplesProvided
){
    outSourceSamples.clear();
    outSourceSamplesProvided = false;

    const Core::Metascript::Value* sourceSamples = FindField(asset, "source_samples");
    if(!sourceSamples)
        return true;
    outSourceSamplesProvided = true;
    if(!sourceSamples->isMap()){
        if(IsExplicitEmptyOptionalField(*sourceSamples))
            return true;

        NWB_LOGGER_ERROR(NWB_TEXT("Deformable geometry meta '{}': 'source_samples' must be a map")
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }
    if(sourceSamples->asMap().empty())
        return true;

    Core::Alloc::ScratchArena<> scratchArena;
    ScratchVector<u32> sourceTri{Core::Alloc::ScratchAllocator<u32>(scratchArena)};
    ScratchVector<Float3U> bary{Core::Alloc::ScratchAllocator<Float3U>(scratchArena)};
    if(!ParseU32ListField(nwbFilePath, *sourceSamples, "source_tri", sourceTri))
        return false;
    if(!ParseFloatListField<Float3U, 3u>(nwbFilePath, *sourceSamples, "bary", bary))
        return false;
    if(sourceTri.size() != vertexCount || bary.size() != vertexCount){
        NWB_LOGGER_ERROR(NWB_TEXT("Deformable geometry meta '{}': source samples must match vertex count")
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    outSourceSamples.clear();
    outSourceSamples.reserve(vertexCount);
    for(usize i = 0; i < vertexCount; ++i){
        SourceSample sample;
        sample.sourceTri = sourceTri[i];
        sample.bary[0] = bary[i].x;
        sample.bary[1] = bary[i].y;
        sample.bary[2] = bary[i].z;
        outSourceSamples.push_back(sample);
    }
    return true;
}

static bool GenerateIdentitySourceSamples(
    const Path& nwbFilePath,
    const Vector<u32>& indices,
    const usize vertexCount,
    Vector<SourceSample>& outSourceSamples){
    outSourceSamples.clear();
    if(vertexCount == 0u || vertexCount > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("Deformable geometry meta '{}': cannot generate source samples for invalid vertex count")
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }
    if(indices.empty() || (indices.size() % 3u) != 0u || (indices.size() / 3u) > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("Deformable geometry meta '{}': cannot generate source samples for invalid indices")
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    Core::Alloc::ScratchArena<> scratchArena;
    Vector<u8, Core::Alloc::ScratchAllocator<u8>> assigned{ Core::Alloc::ScratchAllocator<u8>(scratchArena) };
    assigned.resize(vertexCount, 0u);
    outSourceSamples.resize(vertexCount);

    for(usize triangleIndex = 0u; triangleIndex < indices.size() / 3u; ++triangleIndex){
        const usize indexBase = triangleIndex * 3u;
        for(u32 cornerIndex = 0u; cornerIndex < 3u; ++cornerIndex){
            const u32 vertexIndex = indices[indexBase + cornerIndex];
            if(vertexIndex >= vertexCount){
                NWB_LOGGER_ERROR(NWB_TEXT("Deformable geometry meta '{}': source sample generation found vertex index {} outside vertex count {}")
                    , PathToString<tchar>(nwbFilePath)
                    , vertexIndex
                    , vertexCount
                );
                return false;
            }
            if(assigned[vertexIndex] != 0u)
                continue;

            SourceSample sample;
            sample.sourceTri = static_cast<u32>(triangleIndex);
            sample.bary[cornerIndex] = 1.0f;
            outSourceSamples[vertexIndex] = sample;
            assigned[vertexIndex] = 1u;
        }
    }

    for(usize vertexIndex = 0u; vertexIndex < assigned.size(); ++vertexIndex){
        if(assigned[vertexIndex] != 0u)
            continue;

        NWB_LOGGER_ERROR(NWB_TEXT("Deformable geometry meta '{}': cannot generate source sample for unreferenced vertex {}")
            , PathToString<tchar>(nwbFilePath)
            , vertexIndex
        );
        return false;
    }

    return true;
}

static bool ParseEditMasks(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const usize triangleCount,
    Vector<DeformableEditMaskFlags>& outEditMaskPerTriangle){
    outEditMaskPerTriangle.clear();

    const Core::Metascript::Value* editMasks = FindField(asset, "edit_masks");
    if(!editMasks)
        return true;
    if(IsExplicitEmptyOptionalField(*editMasks))
        return true;

    Core::Alloc::ScratchArena<> scratchArena;
    ScratchVector<u32> parsedFlags{Core::Alloc::ScratchAllocator<u32>(scratchArena)};
    if(!ParseU32ListField(nwbFilePath, asset, "edit_masks", parsedFlags))
        return false;
    if(parsedFlags.size() != triangleCount){
        NWB_LOGGER_ERROR(NWB_TEXT("Deformable geometry meta '{}': edit mask count must match triangle count")
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    outEditMaskPerTriangle.reserve(parsedFlags.size());
    for(usize i = 0; i < parsedFlags.size(); ++i){
        if(parsedFlags[i] > Limit<DeformableEditMaskFlags>::s_Max){
            NWB_LOGGER_ERROR(NWB_TEXT("Deformable geometry meta '{}': edit_masks[{}] exceeds u8")
                , PathToString<tchar>(nwbFilePath)
                , i
            );
            return false;
        }

        const DeformableEditMaskFlags flags = static_cast<DeformableEditMaskFlags>(parsedFlags[i]);
        if(!ValidDeformableEditMaskFlags(flags)){
            NWB_LOGGER_ERROR(NWB_TEXT("Deformable geometry meta '{}': edit_masks[{}] is invalid")
                , PathToString<tchar>(nwbFilePath)
                , i
            );
            return false;
        }
        outEditMaskPerTriangle.push_back(flags);
    }
    return true;
}

static bool ParseRequiredStringField(
    const Path& nwbFilePath,
    const Core::Metascript::Value& map,
    const AStringView fieldName,
    AString& outText){
    outText.clear();

    const Core::Metascript::Value* field = FindField(map, fieldName);
    if(!field || !field->isString()){
        NWB_LOGGER_ERROR(NWB_TEXT("Deformable geometry meta '{}': '{}' must be a string")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }

    const Core::Metascript::MStringView text = field->asString();
    outText.assign(text.data(), text.size());
    CanonicalizeTextInPlace(outText);
    if(outText.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Deformable geometry meta '{}': '{}' must not be empty")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }
    return true;
}

static bool ParseOptionalFiniteF32Field(
    const Path& nwbFilePath,
    const Core::Metascript::Value& map,
    const AStringView fieldName,
    f32& outValue){
    const Core::Metascript::Value* field = FindField(map, fieldName);
    if(!field)
        return true;

    return ParseFiniteF32Value(nwbFilePath, *field, fieldName, outValue);
}

static bool ParseOptionalFloat2Field(
    const Path& nwbFilePath,
    const Core::Metascript::Value& map,
    const AStringView fieldName,
    Float2U& outValue){
    const Core::Metascript::Value* field = FindField(map, fieldName);
    if(!field)
        return true;

    f32 tuple[2] = {};
    if(!ParseF32Tuple(nwbFilePath, *field, fieldName, tuple))
        return false;

    outValue = Float2U(tuple[0], tuple[1]);
    return true;
}

static bool ParseDisplacement(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    DeformableDisplacement& outDisplacement,
    CompactString& outTexturePathText){
    outDisplacement = DeformableDisplacement{};
    outTexturePathText.clear();

    const Core::Metascript::Value* displacement = FindField(asset, "displacement");
    if(!displacement)
        return true;
    if(!displacement->isMap()){
        if(IsExplicitEmptyOptionalField(*displacement))
            return true;

        NWB_LOGGER_ERROR(NWB_TEXT("Deformable geometry meta '{}': 'displacement' must be a map")
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }
    if(displacement->asMap().empty())
        return true;

    AString space;
    AString mode;
    AString field;
    if(!ParseRequiredStringField(nwbFilePath, *displacement, "space", space))
        return false;
    if(!ParseRequiredStringField(nwbFilePath, *displacement, "mode", mode))
        return false;
    if(!ParseRequiredStringField(nwbFilePath, *displacement, "field", field))
        return false;

    const Core::Metascript::Value* amplitude = FindField(*displacement, "amplitude");
    if(!amplitude || !ParseFiniteF32Value(nwbFilePath, *amplitude, "displacement.amplitude", outDisplacement.amplitude))
        return false;

    if(
        !ParseOptionalFiniteF32Field(nwbFilePath, *displacement, "bias", outDisplacement.bias)
        || !ParseOptionalFloat2Field(nwbFilePath, *displacement, "uv_scale", outDisplacement.uvScale)
        || !ParseOptionalFloat2Field(nwbFilePath, *displacement, "uv_offset", outDisplacement.uvOffset)
    )
        return false;

    if(space == "tangent" && mode == "scalar" && field == "uv_ramp"){
        outDisplacement.mode = DeformableDisplacementMode::ScalarUvRamp;
        return ValidDeformableDisplacementDescriptor(outDisplacement);
    }

    if(field != "texture"){
        NWB_LOGGER_ERROR(NWB_TEXT("Deformable geometry meta '{}': displacement field must be 'uv_ramp' or 'texture'")
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    AString texturePath;
    if(!ParseRequiredStringField(nwbFilePath, *displacement, "texture", texturePath))
        return false;

    outDisplacement.texture.virtualPath = ToName(texturePath);
    if(!outTexturePathText.assign(texturePath)){
        NWB_LOGGER_ERROR(NWB_TEXT("Deformable geometry meta '{}': displacement texture path exceeds CompactString capacity")
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }
    if(space == "tangent" && mode == "scalar")
        outDisplacement.mode = DeformableDisplacementMode::ScalarTexture;
    else if(space == "tangent" && mode == "vector")
        outDisplacement.mode = DeformableDisplacementMode::VectorTangentTexture;
    else if(space == "object" && mode == "vector")
        outDisplacement.mode = DeformableDisplacementMode::VectorObjectTexture;
    else{
        NWB_LOGGER_ERROR(NWB_TEXT("Deformable geometry meta '{}': unsupported displacement texture space='{}' mode='{}'")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(space)
            , StringConvert(mode)
        );
        return false;
    }

    return ValidDeformableDisplacementDescriptor(outDisplacement);
}

static bool ParseMorphs(const Path& nwbFilePath, const Core::Metascript::Value& asset, Vector<DeformableMorph>& outMorphs){
    outMorphs.clear();

    const Core::Metascript::Value* morphs = FindField(asset, "morphs");
    if(!morphs)
        return true;
    if(!morphs->isMap()){
        if(IsExplicitEmptyOptionalField(*morphs))
            return true;

        NWB_LOGGER_ERROR(NWB_TEXT("Deformable geometry meta '{}': 'morphs' must be a map"), PathToString<tchar>(nwbFilePath));
        return false;
    }

    Vector<DeformableMorph> parsedMorphs;
    parsedMorphs.reserve(morphs->asMap().size());
    Core::Alloc::ScratchArena<> scratchArena;
    ScratchVector<u32> vertexIds{Core::Alloc::ScratchAllocator<u32>(scratchArena)};
    ScratchVector<Float3U> deltaPositions{Core::Alloc::ScratchAllocator<Float3U>(scratchArena)};
    ScratchVector<Float3U> deltaNormals{Core::Alloc::ScratchAllocator<Float3U>(scratchArena)};
    ScratchVector<Float4U> deltaTangents{Core::Alloc::ScratchAllocator<Float4U>(scratchArena)};
    for(const auto& [morphName, morphValue] : morphs->asMap()){
        const AStringView morphNameView(morphName.data(), morphName.size());
        const Name morphNameId = ToName(morphNameView);
        if(!morphNameId){
            NWB_LOGGER_ERROR(NWB_TEXT("Deformable geometry meta '{}': morph names must not be empty")
                , PathToString<tchar>(nwbFilePath)
            );
            return false;
        }
        if(!morphValue.isMap()){
            NWB_LOGGER_ERROR(NWB_TEXT("Deformable geometry meta '{}': morph '{}' must be a map")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(morphNameView)
            );
            return false;
        }

        if(!ParseU32ListField(nwbFilePath, morphValue, "vertex_ids", vertexIds))
            return false;
        if(!ParseFloatListField<Float3U, 3u>(nwbFilePath, morphValue, "delta_position", deltaPositions))
            return false;
        if(!ParseFloatListField<Float3U, 3u>(nwbFilePath, morphValue, "delta_normal", deltaNormals))
            return false;
        if(!FindField(morphValue, "delta_tangent")){
            NWB_LOGGER_ERROR(NWB_TEXT("Deformable geometry meta '{}': morph '{}' requires 'delta_tangent' list")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(morphNameView)
            );
            return false;
        }
        if(!ParseFloatListField<Float4U, 4u>(nwbFilePath, morphValue, "delta_tangent", deltaTangents))
            return false;
        if(
            vertexIds.empty()
            || vertexIds.size() != deltaPositions.size()
            || vertexIds.size() != deltaNormals.size()
            || vertexIds.size() != deltaTangents.size()
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("Deformable geometry meta '{}': morph '{}' stream counts must match and must not be empty")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(morphNameView)
            );
            return false;
        }

        DeformableMorph morph;
        morph.name = morphNameId;
        if(!morph.nameText.assign(morphNameView)){
            NWB_LOGGER_ERROR(NWB_TEXT("Deformable geometry meta '{}': morph '{}' exceeds CompactString capacity")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(morphNameView)
            );
            return false;
        }
        morph.deltas.reserve(vertexIds.size());
        for(usize i = 0; i < vertexIds.size(); ++i){
            DeformableMorphDelta delta;
            delta.vertexId = vertexIds[i];
            delta.deltaPosition = deltaPositions[i];
            delta.deltaNormal = deltaNormals[i];
            delta.deltaTangent = deltaTangents[i];
            morph.deltas.push_back(delta);
        }
        parsedMorphs.push_back(Move(morph));
    }

    NWB_ASSERT(parsedMorphs.size() == morphs->asMap().size());
    outMorphs = Move(parsedMorphs);
    return true;
}

static bool RejectDeformableGeometrySourceField(const DiscoveredNwbFile& discoveredFile, const Core::Metascript::Value& asset){
    if(!FindField(asset, "source"))
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("Deformable geometry meta '{}': external 'source' imports are not supported; use an offline converter to emit native .nwb streams")
        , PathToString<tchar>(discoveredFile.filePath)
    );
    return false;
}

static bool ParseDeformableGeometryMeta(
    const DiscoveredNwbFile& discoveredFile,
    const Core::Metascript::Document& doc,
    DeformableGeometryCookEntry& outEntry
){
    outEntry = {};

    const Core::Metascript::Value& asset = doc.asset();
    if(!asset.isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("Deformable geometry meta '{}': asset is not a map")
            , PathToString<tchar>(discoveredFile.filePath)
        );
        return false;
    }

    if(!Core::Assets::BuildMetadataDerivedAssetVirtualPath(
        discoveredFile.assetRoot,
        discoveredFile.virtualRoot,
        discoveredFile.filePath,
        asset,
        "DeformableGeometry",
        outEntry.virtualPath
    ))
        return false;

    if(!RejectDeformableGeometrySourceField(discoveredFile, asset))
        return false;

    Core::Alloc::ScratchArena<> scratchArena;
    ScratchVector<Float3U> positions{Core::Alloc::ScratchAllocator<Float3U>(scratchArena)};
    ScratchVector<Float3U> normals{Core::Alloc::ScratchAllocator<Float3U>(scratchArena)};
    ScratchVector<Float4U> tangents{Core::Alloc::ScratchAllocator<Float4U>(scratchArena)};
    ScratchVector<Float2U> uv0{Core::Alloc::ScratchAllocator<Float2U>(scratchArena)};
    ScratchVector<Float4U> colors{Core::Alloc::ScratchAllocator<Float4U>(scratchArena)};
    if(!ParseDeformableIndexType(discoveredFile.filePath, asset, outEntry.use32BitIndices))
        return false;
    if(!ParseFloatListField<Float3U, 3u>(discoveredFile.filePath, asset, "positions", positions))
        return false;
    bool normalsProvided = false;
    bool tangentsProvided = false;
    if(
        !ParseOptionalFloatListField<Float3U, 3u>(
            discoveredFile.filePath,
            asset,
            "normals",
            normals,
            normalsProvided
        )
    )
        return false;
    if(
        !ParseOptionalFloatListField<Float4U, 4u>(
            discoveredFile.filePath,
            asset,
            "tangents",
            tangents,
            tangentsProvided
        )
    )
        return false;
    if(!ParseFloatListField<Float2U, 2u>(discoveredFile.filePath, asset, "uv0", uv0))
        return false;
    if(!ParseDeformableIndexField(discoveredFile.filePath, asset, outEntry.use32BitIndices, outEntry.indices))
        return false;
    if(!normalsProvided)
        normals.assign(positions.size(), Float3U(0.0f, 0.0f, 1.0f));
    if(!tangentsProvided)
        tangents.assign(positions.size(), Float4U(1.0f, 0.0f, 0.0f, 1.0f));
    const Core::Metascript::Value* colorsField = FindField(asset, "colors");
    if(colorsField && !IsExplicitEmptyOptionalField(*colorsField)){
        if(!ParseFloatListField<Float4U, 4u>(discoveredFile.filePath, asset, "colors", colors))
            return false;
    }
    else
        BuildDefaultColors(positions.size(), colors);
    if(!BuildDeformableRestVertices(discoveredFile.filePath, positions, normals, tangents, uv0, colors, outEntry.restVertices))
        return false;
    if(!GenerateMissingDeformableFrames(
        discoveredFile.filePath,
        normalsProvided,
        tangentsProvided,
        outEntry.restVertices,
        outEntry.indices
    ))
        return false;
    if(!ParseSkinInfluences(discoveredFile.filePath, asset, outEntry.restVertices.size(), outEntry.skin))
        return false;
    if(!ParseSkeletonJointCount(discoveredFile.filePath, asset, outEntry.skeletonJointCount))
        return false;
    if(!ParseInverseBindMatrices(discoveredFile.filePath, asset, outEntry.skeletonJointCount, outEntry.inverseBindMatrices))
        return false;
    if(!ParseGeometryClassField(
        discoveredFile.filePath,
        asset,
        s_DeformableGeometryMetaKind,
        outEntry.geometryClass
    ))
        return false;
    if(!GeometryClassUsesDeformableRuntime(outEntry.geometryClass)){
        NWB_LOGGER_ERROR(NWB_TEXT("Deformable geometry meta '{}': geometry_class must be static_deform, skinned, or skinned_deform")
            , PathToString<tchar>(discoveredFile.filePath)
        );
        return false;
    }
    if(GeometryClassUsesSkinning(outEntry.geometryClass) != !outEntry.skin.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Deformable geometry meta '{}': geometry_class '{}' does not match skin payload")
            , PathToString<tchar>(discoveredFile.filePath)
            , StringConvert(GeometryClassText(outEntry.geometryClass))
        );
        return false;
    }
    bool sourceSamplesProvided = false;
    if(!ParseSourceSamples(
        discoveredFile.filePath,
        asset,
        outEntry.restVertices.size(),
        outEntry.sourceSamples,
        sourceSamplesProvided
    ))
        return false;
    if(
        !sourceSamplesProvided
        && GeometryClassAllowsRuntimeDeform(outEntry.geometryClass)
        && !GenerateIdentitySourceSamples(
                discoveredFile.filePath,
                outEntry.indices,
                outEntry.restVertices.size(),
                outEntry.sourceSamples
            )
    )
        return false;
    if(!ParseEditMasks(discoveredFile.filePath, asset, outEntry.indices.size() / 3u, outEntry.editMaskPerTriangle))
        return false;
    if(!ParseDisplacement(
        discoveredFile.filePath,
        asset,
        outEntry.displacement,
        outEntry.displacementTextureVirtualPathText
    ))
        return false;
    if(!ParseMorphs(discoveredFile.filePath, asset, outEntry.morphs))
        return false;
    if(!GeometryClassAllowsRuntimeDeform(outEntry.geometryClass)){
        if(!outEntry.sourceSamples.empty() || !outEntry.editMaskPerTriangle.empty() || !outEntry.morphs.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Deformable geometry meta '{}': geometry_class '{}' cannot define surface edit or morph payload")
                , PathToString<tchar>(discoveredFile.filePath)
                , StringConvert(GeometryClassText(outEntry.geometryClass))
            );
            return false;
        }
        if(outEntry.displacement.mode != DeformableDisplacementMode::None){
            NWB_LOGGER_ERROR(NWB_TEXT("Deformable geometry meta '{}': geometry_class '{}' cannot define displacement")
                , PathToString<tchar>(discoveredFile.filePath)
                , StringConvert(GeometryClassText(outEntry.geometryClass))
            );
            return false;
        }
    }

    return true;
}

static bool ParseDeformableDisplacementTextureMeta(
    const DiscoveredNwbFile& discoveredFile,
    const Core::Metascript::Document& doc,
    DeformableDisplacementTextureCookEntry& outEntry
){
    outEntry = {};

    const Core::Metascript::Value& asset = doc.asset();
    if(!asset.isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("Deformable displacement texture meta '{}': asset is not a map")
            , PathToString<tchar>(discoveredFile.filePath)
        );
        return false;
    }

    if(!Core::Assets::BuildMetadataDerivedAssetVirtualPath(
        discoveredFile.assetRoot,
        discoveredFile.virtualRoot,
        discoveredFile.filePath,
        asset,
        "DeformableDisplacementTexture",
        outEntry.virtualPath
    ))
        return false;

    const Core::Metascript::Value* width = FindField(asset, "width");
    const Core::Metascript::Value* height = FindField(asset, "height");
    if(!width || !ParseU32Value(discoveredFile.filePath, *width, "width", outEntry.width))
        return false;
    if(!height || !ParseU32Value(discoveredFile.filePath, *height, "height", outEntry.height))
        return false;
    if(!ParseFloatListField<Float4U, 4u>(discoveredFile.filePath, asset, "texels", outEntry.texels))
        return false;
    if(outEntry.width == 0u || outEntry.height == 0u || outEntry.width > Limit<u32>::s_Max / outEntry.height){
        NWB_LOGGER_ERROR(NWB_TEXT("Deformable displacement texture meta '{}': dimensions are invalid")
            , PathToString<tchar>(discoveredFile.filePath)
        );
        return false;
    }
    if(outEntry.texels.size() != static_cast<usize>(outEntry.width) * static_cast<usize>(outEntry.height)){
        NWB_LOGGER_ERROR(NWB_TEXT("Deformable displacement texture meta '{}': texel count must match width * height")
            , PathToString<tchar>(discoveredFile.filePath)
        );
        return false;
    }

    return true;
}

static bool BuildDeformableGeometryAsset(DeformableGeometryCookEntry& geometryEntry, DeformableGeometry& outGeometry){
    outGeometry = DeformableGeometry(geometryEntry.virtualPath);

    outGeometry.setGeometryClass(geometryEntry.geometryClass);
    outGeometry.setRestVertices(Move(geometryEntry.restVertices));
    outGeometry.setIndices(Move(geometryEntry.indices));
    outGeometry.setSkin(Move(geometryEntry.skin));
    outGeometry.setSkeletonJointCount(geometryEntry.skeletonJointCount);
    outGeometry.setInverseBindMatrices(Move(geometryEntry.inverseBindMatrices));
    outGeometry.setSourceSamples(Move(geometryEntry.sourceSamples));
    outGeometry.setEditMaskPerTriangle(Move(geometryEntry.editMaskPerTriangle));
    outGeometry.setDisplacement(geometryEntry.displacement);
    outGeometry.setDisplacementTextureVirtualPathText(Move(geometryEntry.displacementTextureVirtualPathText));
    outGeometry.setMorphs(Move(geometryEntry.morphs));
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
    GeometryCookEntry& outEntry
){
    __hidden_geometry_asset_cook::DiscoveredNwbFile discoveredFile;
    if(!__hidden_geometry_asset_cook::BuildDiscoveredNwbFile(assetRoot, virtualRoot, nwbFilePath, discoveredFile))
        return false;
    return __hidden_geometry_asset_cook::ParseGeometryMeta(discoveredFile, doc, outEntry);
}

bool ParseDeformableGeometryCookMetadata(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    DeformableGeometryCookEntry& outEntry
){
    __hidden_geometry_asset_cook::DiscoveredNwbFile discoveredFile;
    if(!__hidden_geometry_asset_cook::BuildDiscoveredNwbFile(assetRoot, virtualRoot, nwbFilePath, discoveredFile))
        return false;
    return __hidden_geometry_asset_cook::ParseDeformableGeometryMeta(discoveredFile, doc, outEntry);
}

bool ParseDeformableDisplacementTextureCookMetadata(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    DeformableDisplacementTextureCookEntry& outEntry
){
    __hidden_geometry_asset_cook::DiscoveredNwbFile discoveredFile;
    if(!__hidden_geometry_asset_cook::BuildDiscoveredNwbFile(assetRoot, virtualRoot, nwbFilePath, discoveredFile))
        return false;
    return __hidden_geometry_asset_cook::ParseDeformableDisplacementTextureMeta(discoveredFile, doc, outEntry);
}

bool BuildGeometryAsset(GeometryCookEntry& geometryEntry, Geometry& outGeometry){
    return __hidden_geometry_asset_cook::BuildGeometryAsset(geometryEntry, outGeometry);
}

bool BuildDeformableGeometryAsset(DeformableGeometryCookEntry& geometryEntry, DeformableGeometry& outGeometry){
    return __hidden_geometry_asset_cook::BuildDeformableGeometryAsset(geometryEntry, outGeometry);
}

bool BuildDeformableDisplacementTextureAsset(
    DeformableDisplacementTextureCookEntry& textureEntry,
    DeformableDisplacementTexture& outTexture
){
    outTexture = DeformableDisplacementTexture(textureEntry.virtualPath);
    outTexture.setSize(textureEntry.width, textureEntry.height);
    outTexture.setTexels(Move(textureEntry.texels));
    return outTexture.validatePayload();
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
    const bool canReserve = AddBinaryVectorReserveBytes(reserveBytes, geometry.vertices())
        && AddBinaryVectorReserveBytes(reserveBytes, geometry.indices())
    ;

    outBinary.clear();
    if(canReserve)
        outBinary.reserve(reserveBytes);

    GeometryBinaryPayload::GeometryHeaderBinary header;
    header.vertexCount = static_cast<u64>(geometry.vertices().size());
    header.indexCount = static_cast<u64>(geometry.indices().size());
    AppendPOD(outBinary, header);
    const tchar* const serializeFailureContext = NWB_TEXT("GeometryAssetCodec::serialize");
    auto appendVector = [&](const auto& values, const tchar* label){
        return GeometryBinaryPayload::AppendVector(outBinary, values, serializeFailureContext, label);
    };
    if(!appendVector(geometry.vertices(), NWB_TEXT("vertices")))
        return false;

    return appendVector(geometry.indices(), NWB_TEXT("indices"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

