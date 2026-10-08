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

[[nodiscard]] Expected<SkeletonJointMatrix> ReadTransformField(
    const Path& nwbFilePath,
    const Value& object,
    const AStringView objectKind
){
    const Value* fieldValue = object.findField(s_TransformField);
    if(!fieldValue)
        return ::Float34Identity();
    return AssetsSkeletonCookDetail::ParseSkeletonJointMatrixValue(
        nwbFilePath, *fieldValue, objectKind, s_TransformField
    );
}

template<typename ObjectT, typename ParseObjectFn>
[[nodiscard]] Expected<Core::Assets::AssetVector<ObjectT>> ParseObjectMap(
    const Path& nwbFilePath,
    const Value& asset,
    const AStringView fieldName,
    const AStringView objectKind,
    Core::Assets::AssetArena& arena,
    ParseObjectFn&& parseObject
){
    Core::Assets::AssetVector<ObjectT> objects(arena);

    const Value* fieldValue = asset.findField(fieldName);
    if(!fieldValue)
        return objects;
    if(!fieldValue->isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("Model meta '{}': field '{}' must be a map")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return MakeUnexpected(Failure{});
    }

    const auto& map = fieldValue->asMap();
    objects.reserve(map.size());
    for(const auto& [objectName, objectValue] : map){
        const Name name(AStringView(objectName.data(), objectName.size()));
        if(!name){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': object name must not be empty")
                , StringConvert(objectKind)
                , PathToString<tchar>(nwbFilePath)
            );
            return MakeUnexpected(Failure{});
        }
        auto object = parseObject(objectValue);
        if(!object)
            return MakeUnexpected(Failure{});
        object->name = name;
        objects.push_back(*object);
    }

    return objects;
}

[[nodiscard]] Expected<ModelSkeletonObject> ParseSkeletonObject(const Path& nwbFilePath, const Value& objectValue){
    ModelSkeletonObject object{};
    static constexpr AStringView s_ObjectKind = "Model skeleton object";
    if(!objectValue.isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': value must be a map")
            , StringConvert(s_ObjectKind)
            , PathToString<tchar>(nwbFilePath)
        );
        return MakeUnexpected(Failure{});
    }
    if(!ValidateModelObjectFields(nwbFilePath, objectValue, s_ObjectKind, { s_SkeletonField, s_TransformField }))
        return MakeUnexpected(Failure{});

    auto skeletonResult = Core::Assets::ReadMetadataAssetRefField<Skeleton>(nwbFilePath, objectValue, s_ObjectKind, s_SkeletonField, true);
    if(!skeletonResult)
        return MakeUnexpected(Failure{});
    object.skeleton = *skeletonResult;
    auto transformResult = ReadTransformField(nwbFilePath, objectValue, s_ObjectKind);
    if(!transformResult)
        return MakeUnexpected(Failure{});
    object.transform = *transformResult;
    return object;
}

[[nodiscard]] Expected<ModelStaticMeshObject> ParseStaticMeshObject(const Path& nwbFilePath, const Value& objectValue){
    ModelStaticMeshObject object{};
    static constexpr AStringView s_ObjectKind = "Model static mesh object";
    if(!objectValue.isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': value must be a map")
            , StringConvert(s_ObjectKind)
            , PathToString<tchar>(nwbFilePath)
        );
        return MakeUnexpected(Failure{});
    }
    if(!ValidateModelObjectFields(
        nwbFilePath,
        objectValue,
        s_ObjectKind,
        { s_MeshField, s_MaterialField, s_ParentObjectField, s_ParentJointField, s_TransformField }
    ))
        return MakeUnexpected(Failure{});

    auto meshResult = Core::Assets::ReadMetadataAssetRefField<Mesh>(nwbFilePath, objectValue, s_ObjectKind, s_MeshField, true);
    if(!meshResult)
        return MakeUnexpected(Failure{});
    object.mesh = *meshResult;
    auto materialResult = Core::Assets::ReadMetadataAssetRefField<Material>(nwbFilePath, objectValue, s_ObjectKind, s_MaterialField, false);
    if(!materialResult)
        return MakeUnexpected(Failure{});
    object.material = *materialResult;
    auto parentObjectResult = Core::Assets::ReadMetadataNameField(nwbFilePath, objectValue, s_ObjectKind, s_ParentObjectField, false);
    if(!parentObjectResult)
        return MakeUnexpected(Failure{});
    object.parentObject = *parentObjectResult;
    auto parentJointResult = Core::Assets::ReadMetadataNameField(nwbFilePath, objectValue, s_ObjectKind, s_ParentJointField, false);
    if(!parentJointResult)
        return MakeUnexpected(Failure{});
    object.parentJoint = *parentJointResult;
    auto transformResult = ReadTransformField(nwbFilePath, objectValue, s_ObjectKind);
    if(!transformResult)
        return MakeUnexpected(Failure{});
    object.transform = *transformResult;
    return object;
}

[[nodiscard]] Expected<ModelSkinnedMeshObject> ParseSkinnedMeshObject(const Path& nwbFilePath, const Value& objectValue){
    ModelSkinnedMeshObject object{};
    static constexpr AStringView s_ObjectKind = "Model skinned mesh object";
    if(!objectValue.isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': value must be a map")
            , StringConvert(s_ObjectKind)
            , PathToString<tchar>(nwbFilePath)
        );
        return MakeUnexpected(Failure{});
    }
    if(!ValidateModelObjectFields(
        nwbFilePath,
        objectValue,
        s_ObjectKind,
        { s_SkinField, s_MaterialField, s_SkeletonField, s_TransformField }
    ))
        return MakeUnexpected(Failure{});

    auto skinResult = Core::Assets::ReadMetadataAssetRefField<Skin>(nwbFilePath, objectValue, s_ObjectKind, s_SkinField, true);
    if(!skinResult)
        return MakeUnexpected(Failure{});
    object.skin = *skinResult;
    auto materialResult = Core::Assets::ReadMetadataAssetRefField<Material>(nwbFilePath, objectValue, s_ObjectKind, s_MaterialField, false);
    if(!materialResult)
        return MakeUnexpected(Failure{});
    object.material = *materialResult;
    auto skeletonObjectResult = Core::Assets::ReadMetadataNameField(nwbFilePath, objectValue, s_ObjectKind, s_SkeletonField, true);
    if(!skeletonObjectResult)
        return MakeUnexpected(Failure{});
    object.skeletonObject = *skeletonObjectResult;
    auto transformResult = ReadTransformField(nwbFilePath, objectValue, s_ObjectKind);
    if(!transformResult)
        return MakeUnexpected(Failure{});
    object.transform = *transformResult;
    return object;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<ModelCookEntry> ParseModelCookMetadata(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
){
    using namespace __hidden_model_cook;

    ModelCookEntry entry(arena);

    if(!Core::Assets::CheckMetadataAssetMap(nwbFilePath, asset, "Model meta"))
        return MakeUnexpected(Failure{});

    if(!Core::Assets::AssignCookEntryVirtualPath(entry, virtualPath, nwbFilePath, "Model meta"))
        return MakeUnexpected(Failure{});
    if(!ValidateModelAssetFields(nwbFilePath, asset))
        return MakeUnexpected(Failure{});

    auto skeletonObjectsResult = ParseObjectMap<ModelSkeletonObject>(
        nwbFilePath,
        asset,
        s_SkeletonsField,
        AStringView("Model skeleton object"),
        arena,
        [&](const Core::Metascript::Value& objectValue){
            return ParseSkeletonObject(nwbFilePath, objectValue);
        }
    );
    if(!skeletonObjectsResult)
        return MakeUnexpected(Failure{});
    entry.skeletonObjects = Move(*skeletonObjectsResult);

    auto staticMeshObjectsResult = ParseObjectMap<ModelStaticMeshObject>(
        nwbFilePath,
        asset,
        s_StaticMeshesField,
        AStringView("Model static mesh object"),
        arena,
        [&](const Core::Metascript::Value& objectValue){
            return ParseStaticMeshObject(nwbFilePath, objectValue);
        }
    );
    if(!staticMeshObjectsResult)
        return MakeUnexpected(Failure{});
    entry.staticMeshObjects = Move(*staticMeshObjectsResult);

    auto skinnedMeshObjectsResult = ParseObjectMap<ModelSkinnedMeshObject>(
        nwbFilePath,
        asset,
        s_SkinnedMeshesField,
        AStringView("Model skinned mesh object"),
        arena,
        [&](const Core::Metascript::Value& objectValue){
            return ParseSkinnedMeshObject(nwbFilePath, objectValue);
        }
    );
    if(!skinnedMeshObjectsResult)
        return MakeUnexpected(Failure{});
    entry.skinnedMeshObjects = Move(*skinnedMeshObjectsResult);

    Model testModel(entry.skeletonObjects.get_allocator().arena(), entry.virtualPath);
    testModel.setObjects(
        Model::SkeletonObjectVector(entry.skeletonObjects),
        Model::StaticMeshObjectVector(entry.staticMeshObjects),
        Model::SkinnedMeshObjectVector(entry.skinnedMeshObjects)
    );
    if(!testModel.validatePayload(scratchArena))
        return MakeUnexpected(Failure{});
    return entry;
}

Expected<ModelCookEntry> ParseModelCookMetadata(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
){
    Name virtualPath = s_NameNone;
    auto virtualPathResult = Core::Assets::BuildMetadataDerivedAssetVirtualPath(assetRoot, virtualRoot, nwbFilePath, scratchArena);
    if(!virtualPathResult)
        return MakeUnexpected(Failure{});
    virtualPath = *virtualPathResult;
    return ParseModelCookMetadata(virtualPath, nwbFilePath, doc.asset(), arena, scratchArena);
}

Expected<Model> BuildModelAsset(ModelCookEntry& modelEntry, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena){
    Model asset(arena, modelEntry.virtualPath);
    asset.setObjects(
        Move(modelEntry.skeletonObjects),
        Move(modelEntry.staticMeshObjects),
        Move(modelEntry.skinnedMeshObjects)
    );
    if(!asset.validatePayload(scratchArena))
        return MakeUnexpected(Failure{});
    return asset;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

