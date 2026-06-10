// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset.h"
#include "binary_payload.h"

#include <core/common/log.h>
#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_model_cook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ValueContainer>
[[nodiscard]] bool AppendVector(
    Core::Assets::AssetBytes& outBinary,
    const ValueContainer& values,
    const tchar* failureContext,
    const tchar* label
){
    const BinaryVectorPayloadFailure::Enum failure = AppendBinaryVectorPayload(outBinary, values);
    if(failure == BinaryVectorPayloadFailure::None)
        return true;

    if(failure == BinaryVectorPayloadFailure::CountOverflow){
        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: '{}' payload byte size overflows"), failureContext, label);
    }
    else{
        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: '{}' payload overflows output binary"), failureContext, label);
    }
    return false;
}

};


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

    return __hidden_model_cook::AppendVector(
        outBinary,
        skeletonObjectBinaries,
        NWB_TEXT("ModelAssetCodec::serialize"),
        NWB_TEXT("skeleton objects")
    )
        && __hidden_model_cook::AppendVector(
            outBinary,
            staticMeshObjectBinaries,
            NWB_TEXT("ModelAssetCodec::serialize"),
            NWB_TEXT("static mesh objects")
        )
        && __hidden_model_cook::AppendVector(
            outBinary,
            skinnedMeshObjectBinaries,
            NWB_TEXT("ModelAssetCodec::serialize"),
            NWB_TEXT("skinned mesh objects")
        )
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
