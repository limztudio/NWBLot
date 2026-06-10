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


namespace __hidden_skeleton_cook{


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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool SkeletonAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(asset.assetType() != assetType()){
        NWB_LOGGER_ERROR(NWB_TEXT("SkeletonAssetCodec::serialize failed: invalid asset type '{}', expected '{}'")
            , StringConvert(asset.assetType().c_str())
            , StringConvert(Skeleton::s_AssetTypeText)
        );
        return false;
    }

    const Skeleton& skeleton = static_cast<const Skeleton&>(asset);
    if(!skeleton.validatePayload())
        return false;

    Core::Assets::AssetVector<SkeletonBinaryPayload::JointBinary> jointBinaries(outBinary.get_allocator().arena());
    jointBinaries.reserve(skeleton.joints().size());
    for(const SkeletonJoint& joint : skeleton.joints()){
        SkeletonBinaryPayload::JointBinary jointBinary;
        jointBinary.nameHash = joint.name.hash();
        jointBinary.parentIndex = joint.parentIndex;
        jointBinary.localBindPose = joint.localBindPose;
        jointBinaries.push_back(jointBinary);
    }

    outBinary.clear();
    outBinary.reserve(sizeof(SkeletonBinaryPayload::HeaderBinary) + jointBinaries.size() * sizeof(SkeletonBinaryPayload::JointBinary));

    SkeletonBinaryPayload::HeaderBinary header;
    header.jointCount = static_cast<u64>(jointBinaries.size());
    AppendPOD(outBinary, header);
    return __hidden_skeleton_cook::AppendVector(
        outBinary,
        jointBinaries,
        NWB_TEXT("SkeletonAssetCodec::serialize"),
        NWB_TEXT("joints")
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
