// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"
#include "binary_payload.h"

#include <core/assets/binary_payload_io.h>
#include <core/assets/paths.h>
#include <core/common/log.h>
#include <core/metascript/parser.h>
#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ModelAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(asset.assetType() != assetType()){
        NWB_LOGGER_ERROR(NWB_TEXT("ModelAssetCodec::serialize failed: invalid asset type '{}', expected '{}'")
            , StringConvert(asset.assetType().c_str())
            , StringConvert(Model::s_AssetTypeText)
        );
        return false;
    }

    const Model& model = static_cast<const Model&>(asset);
    if(!model.validatePayload())
        return false;

    Core::Assets::AssetVector<ModelBinaryPayload::ModelSkeletonObjectBinary> skeletonObjectBinaries(outBinary.get_allocator().arena());
    Core::Assets::AssetVector<ModelBinaryPayload::ModelStaticMeshObjectBinary> staticMeshObjectBinaries(outBinary.get_allocator().arena());
    Core::Assets::AssetVector<ModelBinaryPayload::ModelSkinnedMeshObjectBinary> skinnedMeshObjectBinaries(outBinary.get_allocator().arena());

    skeletonObjectBinaries.reserve(model.skeletonObjects().size());
    for(const ModelSkeletonObject& object : model.skeletonObjects()){
        ModelBinaryPayload::ModelSkeletonObjectBinary objectBinary;
        objectBinary.nameHash = object.name.hash();
        objectBinary.skeletonNameHash = object.skeleton.name().hash();
        objectBinary.transform = object.transform;
        skeletonObjectBinaries.push_back(objectBinary);
    }

    staticMeshObjectBinaries.reserve(model.staticMeshObjects().size());
    for(const ModelStaticMeshObject& object : model.staticMeshObjects()){
        ModelBinaryPayload::ModelStaticMeshObjectBinary objectBinary;
        objectBinary.nameHash = object.name.hash();
        objectBinary.meshNameHash = object.mesh.name().hash();
        objectBinary.materialNameHash = object.material.name().hash();
        objectBinary.parentObjectNameHash = object.parentObject.hash();
        objectBinary.parentJointNameHash = object.parentJoint.hash();
        objectBinary.transform = object.transform;
        staticMeshObjectBinaries.push_back(objectBinary);
    }

    skinnedMeshObjectBinaries.reserve(model.skinnedMeshObjects().size());
    for(const ModelSkinnedMeshObject& object : model.skinnedMeshObjects()){
        ModelBinaryPayload::ModelSkinnedMeshObjectBinary objectBinary;
        objectBinary.nameHash = object.name.hash();
        objectBinary.meshNameHash = object.mesh.name().hash();
        objectBinary.skinNameHash = object.skin.name().hash();
        objectBinary.materialNameHash = object.material.name().hash();
        objectBinary.skeletonObjectNameHash = object.skeletonObject.hash();
        objectBinary.transform = object.transform;
        skinnedMeshObjectBinaries.push_back(objectBinary);
    }

    usize reserveBytes = sizeof(ModelBinaryPayload::ModelHeaderBinary);
    const bool canReserve =
        AddBinaryVectorReserveBytes(reserveBytes, skeletonObjectBinaries)
        && AddBinaryVectorReserveBytes(reserveBytes, staticMeshObjectBinaries)
        && AddBinaryVectorReserveBytes(reserveBytes, skinnedMeshObjectBinaries)
    ;

    outBinary.clear();
    if(canReserve)
        outBinary.reserve(reserveBytes);

    ModelBinaryPayload::ModelHeaderBinary header;
    header.skeletonObjectCount = static_cast<u64>(skeletonObjectBinaries.size());
    header.staticMeshObjectCount = static_cast<u64>(staticMeshObjectBinaries.size());
    header.skinnedMeshObjectCount = static_cast<u64>(skinnedMeshObjectBinaries.size());
    AppendPOD(outBinary, header);

    return Core::Assets::AppendVectorPayload(
        outBinary,
        skeletonObjectBinaries,
        NWB_TEXT("ModelAssetCodec::serialize"),
        NWB_TEXT("skeleton objects")
    )
        && Core::Assets::AppendVectorPayload(
            outBinary,
            staticMeshObjectBinaries,
            NWB_TEXT("ModelAssetCodec::serialize"),
            NWB_TEXT("static mesh objects")
        )
        && Core::Assets::AppendVectorPayload(
            outBinary,
            skinnedMeshObjectBinaries,
            NWB_TEXT("ModelAssetCodec::serialize"),
            NWB_TEXT("skinned mesh objects")
        )
    ;
}

namespace __hidden_model_cook{

using namespace Core::Metascript;

static constexpr AStringView s_SkeletonsField = "skeletons";
static constexpr AStringView s_StaticMeshesField = "static_meshes";
static constexpr AStringView s_SkinnedMeshesField = "skinned_meshes";
static constexpr AStringView s_SkeletonField = "skeleton";
static constexpr AStringView s_MeshField = "mesh";
static constexpr AStringView s_SkinField = "skin";
static constexpr AStringView s_MaterialField = "material";
static constexpr AStringView s_ParentObjectField = "parent_object";
static constexpr AStringView s_ParentJointField = "parent_joint";
static constexpr AStringView s_TransformField = "transform";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool ValidateModelAssetFields(const Path& nwbFilePath, const Value& asset){
    return Core::Assets::ValidateMetadataAssetFields(
        nwbFilePath,
        asset,
        "Model meta",
        { s_SkeletonsField, s_StaticMeshesField, s_SkinnedMeshesField }
    );
}

[[nodiscard]] bool ValidateModelObjectFields(
    const Path& nwbFilePath,
    const Value& object,
    const AStringView objectKind,
    const InitializerList<AStringView> allowedFields
){
    return Core::Assets::ValidateMetadataAssetFields(
        nwbFilePath,
        object,
        objectKind,
        allowedFields
    );
}

[[nodiscard]] bool ReadStringField(
    const Path& nwbFilePath,
    const Value& object,
    const AStringView objectKind,
    const AStringView fieldName,
    const bool required,
    Name& outName
){
    outName = NAME_NONE;

    const Value* fieldValue = object.findField(fieldName);
    if(!fieldValue){
        if(!required)
            return true;

        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' is required")
            , StringConvert(objectKind)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }
    if(!fieldValue->isString()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be a string")
            , StringConvert(objectKind)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }

    const MStringView text = fieldValue->asString();
    outName = Name(AStringView(text.data(), text.size()));
    if(required && !outName){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must not be empty")
            , StringConvert(objectKind)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }
    return true;
}

template<typename AssetT>
[[nodiscard]] bool ReadAssetRefField(
    const Path& nwbFilePath,
    const Value& object,
    const AStringView objectKind,
    const AStringView fieldName,
    const bool required,
    Core::Assets::AssetRef<AssetT>& outRef
){
    Name assetName = NAME_NONE;
    if(!ReadStringField(nwbFilePath, object, objectKind, fieldName, required, assetName))
        return false;

    outRef = {};
    outRef.virtualPath = assetName;
    return !required || outRef.valid();
}

[[nodiscard]] bool ReadFloatValue(
    const Path& nwbFilePath,
    const Value& value,
    const AStringView objectKind,
    const AStringView fieldName,
    f32& outValue
){
    outValue = 0.0f;
    if(!value.isNumeric()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must contain only numeric matrix values")
            , StringConvert(objectKind)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }

    const f64 numericValue = value.toDouble();
    if(!IsFinite(numericValue) || numericValue < static_cast<f64>(Limit<f32>::s_Min) || numericValue > static_cast<f64>(Limit<f32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' contains a non-finite or out-of-range matrix value")
            , StringConvert(objectKind)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }

    outValue = static_cast<f32>(numericValue);
    return true;
}

[[nodiscard]] bool ReadTransformField(
    const Path& nwbFilePath,
    const Value& object,
    const AStringView objectKind,
    SkeletonJointMatrix& outTransform
){
    outTransform = MakeIdentityModelMatrix();

    const Value* fieldValue = object.findField(s_TransformField);
    if(!fieldValue)
        return true;
    if(!fieldValue->isList()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be a 3x4 affine matrix")
            , StringConvert(objectKind)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(s_TransformField)
        );
        return false;
    }

    const auto& rows = fieldValue->asList();
    if(rows.size() != 3u && rows.size() != 4u){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must have 3 or 4 rows")
            , StringConvert(objectKind)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(s_TransformField)
        );
        return false;
    }

    for(usize rowIndex = 0u; rowIndex < 3u; ++rowIndex){
        const Value& row = rows[rowIndex];
        if(!row.isList() || row.asList().size() != 4u){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' row {} must have 4 numeric values")
                , StringConvert(objectKind)
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(s_TransformField)
                , rowIndex
            );
            return false;
        }

        f32 rowValues[4] = {};
        for(usize columnIndex = 0u; columnIndex < 4u; ++columnIndex){
            if(!ReadFloatValue(nwbFilePath, row.asList()[columnIndex], objectKind, s_TransformField, rowValues[columnIndex]))
                return false;
        }
        outTransform.rows[rowIndex] = Float4(rowValues[0], rowValues[1], rowValues[2], rowValues[3]);
    }

    return true;
}

template<typename ObjectVectorT, typename ParseObjectFn>
[[nodiscard]] bool ParseObjectMap(
    const Path& nwbFilePath,
    const Value& asset,
    const AStringView fieldName,
    const AStringView objectKind,
    ObjectVectorT& outObjects,
    ParseObjectFn&& parseObject
){
    outObjects.clear();

    const Value* fieldValue = asset.findField(fieldName);
    if(!fieldValue)
        return true;
    if(!fieldValue->isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("Model meta '{}': field '{}' must be a map")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }

    const auto& map = fieldValue->asMap();
    outObjects.reserve(map.size());
    for(const auto& [objectName, objectValue] : map){
        typename ObjectVectorT::value_type object{};
        object.name = Name(AStringView(objectName.data(), objectName.size()));
        if(!object.name){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': object name must not be empty")
                , StringConvert(objectKind)
                , PathToString<tchar>(nwbFilePath)
            );
            return false;
        }
        if(!parseObject(objectValue, object))
            return false;
        outObjects.push_back(object);
    }

    return true;
}

[[nodiscard]] bool ParseSkeletonObject(const Path& nwbFilePath, const Value& objectValue, ModelSkeletonObject& outObject){
    static constexpr AStringView s_ObjectKind = "Model skeleton object";
    if(!objectValue.isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': value must be a map")
            , StringConvert(s_ObjectKind)
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }
    if(!ValidateModelObjectFields(nwbFilePath, objectValue, s_ObjectKind, { s_SkeletonField, s_TransformField }))
        return false;

    return ReadAssetRefField(nwbFilePath, objectValue, s_ObjectKind, s_SkeletonField, true, outObject.skeleton)
        && ReadTransformField(nwbFilePath, objectValue, s_ObjectKind, outObject.transform)
    ;
}

[[nodiscard]] bool ParseStaticMeshObject(const Path& nwbFilePath, const Value& objectValue, ModelStaticMeshObject& outObject){
    static constexpr AStringView s_ObjectKind = "Model static mesh object";
    if(!objectValue.isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': value must be a map")
            , StringConvert(s_ObjectKind)
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }
    if(!ValidateModelObjectFields(
        nwbFilePath,
        objectValue,
        s_ObjectKind,
        { s_MeshField, s_MaterialField, s_ParentObjectField, s_ParentJointField, s_TransformField }
    ))
        return false;

    return ReadAssetRefField(nwbFilePath, objectValue, s_ObjectKind, s_MeshField, true, outObject.mesh)
        && ReadAssetRefField(nwbFilePath, objectValue, s_ObjectKind, s_MaterialField, false, outObject.material)
        && ReadStringField(nwbFilePath, objectValue, s_ObjectKind, s_ParentObjectField, false, outObject.parentObject)
        && ReadStringField(nwbFilePath, objectValue, s_ObjectKind, s_ParentJointField, false, outObject.parentJoint)
        && ReadTransformField(nwbFilePath, objectValue, s_ObjectKind, outObject.transform)
    ;
}

[[nodiscard]] bool ParseSkinnedMeshObject(const Path& nwbFilePath, const Value& objectValue, ModelSkinnedMeshObject& outObject){
    static constexpr AStringView s_ObjectKind = "Model skinned mesh object";
    if(!objectValue.isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': value must be a map")
            , StringConvert(s_ObjectKind)
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }
    if(!ValidateModelObjectFields(
        nwbFilePath,
        objectValue,
        s_ObjectKind,
        { s_MeshField, s_SkinField, s_MaterialField, s_SkeletonField, s_TransformField }
    ))
        return false;

    return ReadAssetRefField(nwbFilePath, objectValue, s_ObjectKind, s_MeshField, true, outObject.mesh)
        && ReadAssetRefField(nwbFilePath, objectValue, s_ObjectKind, s_SkinField, true, outObject.skin)
        && ReadAssetRefField(nwbFilePath, objectValue, s_ObjectKind, s_MaterialField, false, outObject.material)
        && ReadStringField(nwbFilePath, objectValue, s_ObjectKind, s_SkeletonField, true, outObject.skeletonObject)
        && ReadTransformField(nwbFilePath, objectValue, s_ObjectKind, outObject.transform)
    ;
}

[[nodiscard]] bool SkeletonObjectNameExists(const ModelCookEntry& entry, const Name skeletonObjectName){
    for(const ModelSkeletonObject& skeletonObject : entry.skeletonObjects){
        if(skeletonObject.name == skeletonObjectName)
            return true;
    }
    return false;
}

[[nodiscard]] bool NormalizeSkinnedMeshSkeletonObject(
    const Path& nwbFilePath,
    const ModelCookEntry& entry,
    ModelSkinnedMeshObject& object
){
    if(SkeletonObjectNameExists(entry, object.skeletonObject))
        return true;

    ModelSkeletonObject const* matchedSkeletonObject = nullptr;
    for(const ModelSkeletonObject& skeletonObject : entry.skeletonObjects){
        if(!skeletonObject.skeleton.valid() || skeletonObject.skeleton.name() != object.skeletonObject)
            continue;
        if(matchedSkeletonObject){
            NWB_LOGGER_ERROR(NWB_TEXT("Model meta '{}': skinned mesh '{}' skeleton '{}' matches multiple skeleton objects")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(object.name.c_str())
                , StringConvert(object.skeletonObject.c_str())
            );
            return false;
        }
        matchedSkeletonObject = &skeletonObject;
    }

    if(!matchedSkeletonObject)
        return true;

    object.skeletonObject = matchedSkeletonObject->name;
    return true;
}

[[nodiscard]] bool NormalizeSkinnedMeshSkeletonObjects(const Path& nwbFilePath, ModelCookEntry& entry){
    for(ModelSkinnedMeshObject& object : entry.skinnedMeshObjects){
        if(!NormalizeSkinnedMeshSkeletonObject(nwbFilePath, entry, object))
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


bool ParseModelCookMetadata(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    ModelCookEntry& outEntry
){
    using namespace __hidden_model_cook;

    outEntry = ModelCookEntry(outEntry.skeletonObjects.get_allocator().arena());

    if(!asset.isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("Model meta '{}': asset is not a map"), PathToString<tchar>(nwbFilePath));
        return false;
    }

    outEntry.virtualPath = virtualPath;
    if(!outEntry.virtualPath){
        NWB_LOGGER_ERROR(NWB_TEXT("Model meta '{}': virtual path must not be empty"), PathToString<tchar>(nwbFilePath));
        return false;
    }
    if(!ValidateModelAssetFields(nwbFilePath, asset))
        return false;

    if(
        !ParseObjectMap(
            nwbFilePath,
            asset,
            s_SkeletonsField,
            AStringView("Model skeleton object"),
            outEntry.skeletonObjects,
            [&](const Core::Metascript::Value& objectValue, ModelSkeletonObject& outObject){
                return ParseSkeletonObject(nwbFilePath, objectValue, outObject);
            }
        )
        || !ParseObjectMap(
            nwbFilePath,
            asset,
            s_StaticMeshesField,
            AStringView("Model static mesh object"),
            outEntry.staticMeshObjects,
            [&](const Core::Metascript::Value& objectValue, ModelStaticMeshObject& outObject){
                return ParseStaticMeshObject(nwbFilePath, objectValue, outObject);
            }
        )
        || !ParseObjectMap(
            nwbFilePath,
            asset,
            s_SkinnedMeshesField,
            AStringView("Model skinned mesh object"),
            outEntry.skinnedMeshObjects,
            [&](const Core::Metascript::Value& objectValue, ModelSkinnedMeshObject& outObject){
                return ParseSkinnedMeshObject(nwbFilePath, objectValue, outObject);
            }
        )
    )
        return false;

    if(!NormalizeSkinnedMeshSkeletonObjects(nwbFilePath, outEntry))
        return false;

    Model testModel(outEntry.skeletonObjects.get_allocator().arena(), outEntry.virtualPath);
    testModel.setObjects(
        Model::SkeletonObjectVector(outEntry.skeletonObjects),
        Model::StaticMeshObjectVector(outEntry.staticMeshObjects),
        Model::SkinnedMeshObjectVector(outEntry.skinnedMeshObjects)
    );
    return testModel.validatePayload();
}

bool ParseModelCookMetadata(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    ModelCookEntry& outEntry,
    Core::Alloc::ScratchArena& scratchArena
){
    Name virtualPath = NAME_NONE;
    if(!Core::Assets::BuildMetadataDerivedAssetVirtualPath(assetRoot, virtualRoot, nwbFilePath, virtualPath, scratchArena))
        return false;
    return ParseModelCookMetadata(virtualPath, nwbFilePath, doc.asset(), outEntry);
}

bool BuildModelAsset(ModelCookEntry& modelEntry, Model& outModel){
    outModel = Model(modelEntry.skeletonObjects.get_allocator().arena(), modelEntry.virtualPath);
    outModel.setObjects(
        Move(modelEntry.skeletonObjects),
        Move(modelEntry.staticMeshObjects),
        Move(modelEntry.skinnedMeshObjects)
    );
    return outModel.validatePayload();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

