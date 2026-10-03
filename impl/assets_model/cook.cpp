// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"
#include "arena_names.h"
#include "binary_payload.h"

#include <impl/assets_skeleton/cook_matrix.h>

#include <core/assets/binary_payload_io.h>
#include <core/assets/paths.h>
#include <core/common/log.h>
#include <core/metascript/parser.h>

#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ModelAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(!checkSerializeAssetType(asset, NWB_TEXT("ModelAssetCodec::serialize")))
        return false;

    const Model& model = static_cast<const Model&>(asset);
    Core::Alloc::ScratchArena scratchArena(AssetsModelArenaScope::s_SerializeArena);
    if(!model.validatePayload(scratchArena))
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_model_cook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

[[nodiscard]] bool ReadTransformField(
    const Path& nwbFilePath,
    const Value& object,
    const AStringView objectKind,
    SkeletonJointMatrix& outTransform
){
    outTransform = ::Float34Identity();

    const Value* fieldValue = object.findField(s_TransformField);
    if(!fieldValue)
        return true;
    return AssetsSkeletonCookDetail::ParseSkeletonJointMatrixValue(
        nwbFilePath, *fieldValue, objectKind, s_TransformField, outTransform
    );
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

    return Core::Assets::ReadMetadataAssetRefField(nwbFilePath, objectValue, s_ObjectKind, s_SkeletonField, true, outObject.skeleton)
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

    return Core::Assets::ReadMetadataAssetRefField(nwbFilePath, objectValue, s_ObjectKind, s_MeshField, true, outObject.mesh)
        && Core::Assets::ReadMetadataAssetRefField(nwbFilePath, objectValue, s_ObjectKind, s_MaterialField, false, outObject.material)
        && Core::Assets::ReadMetadataNameField(nwbFilePath, objectValue, s_ObjectKind, s_ParentObjectField, false, outObject.parentObject)
        && Core::Assets::ReadMetadataNameField(nwbFilePath, objectValue, s_ObjectKind, s_ParentJointField, false, outObject.parentJoint)
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

    return Core::Assets::ReadMetadataAssetRefField(nwbFilePath, objectValue, s_ObjectKind, s_MeshField, true, outObject.mesh)
        && Core::Assets::ReadMetadataAssetRefField(nwbFilePath, objectValue, s_ObjectKind, s_SkinField, true, outObject.skin)
        && Core::Assets::ReadMetadataAssetRefField(nwbFilePath, objectValue, s_ObjectKind, s_MaterialField, false, outObject.material)
        && Core::Assets::ReadMetadataNameField(nwbFilePath, objectValue, s_ObjectKind, s_SkeletonField, true, outObject.skeletonObject)
        && ReadTransformField(nwbFilePath, objectValue, s_ObjectKind, outObject.transform)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ParseModelCookMetadata(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    ModelCookEntry& outEntry,
    Core::Alloc::ScratchArena& scratchArena
){
    using namespace __hidden_model_cook;

    outEntry = ModelCookEntry(outEntry.skeletonObjects.get_allocator().arena());

    if(!Core::Assets::CheckMetadataAssetMap(nwbFilePath, asset, "Model meta"))
        return false;

    if(!Core::Assets::AssignCookEntryVirtualPath(outEntry, virtualPath, nwbFilePath, "Model meta"))
        return false;
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

    Model testModel(outEntry.skeletonObjects.get_allocator().arena(), outEntry.virtualPath);
    testModel.setObjects(
        Model::SkeletonObjectVector(outEntry.skeletonObjects),
        Model::StaticMeshObjectVector(outEntry.staticMeshObjects),
        Model::SkinnedMeshObjectVector(outEntry.skinnedMeshObjects)
    );
    return testModel.validatePayload(scratchArena);
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
    return ParseModelCookMetadata(virtualPath, nwbFilePath, doc.asset(), outEntry, scratchArena);
}

bool BuildModelAsset(ModelCookEntry& modelEntry, Model& outModel, Core::Alloc::ScratchArena& scratchArena){
    outModel = Model(modelEntry.skeletonObjects.get_allocator().arena(), modelEntry.virtualPath);
    outModel.setObjects(
        Move(modelEntry.skeletonObjects),
        Move(modelEntry.staticMeshObjects),
        Move(modelEntry.skinnedMeshObjects)
    );
    return outModel.validatePayload(scratchArena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

