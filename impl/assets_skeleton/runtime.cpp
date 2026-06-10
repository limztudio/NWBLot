// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset.h"
#include "binary_payload.h"

#include <core/assets/auto_registration.h>
#include <core/common/log.h>
#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skeleton_runtime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UniquePtr<Core::Assets::IAssetCodec> CreateSkeletonAssetCodec(){
    return MakeUnique<SkeletonAssetCodec>();
}
Core::Assets::AssetCodecAutoRegistrar s_SkeletonAssetCodecAutoRegistrar(&CreateSkeletonAssetCodec);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ValueContainer>
[[nodiscard]] bool ReadVector(
    const Core::Assets::AssetBytes& binary,
    usize& inOutCursor,
    const u64 count,
    ValueContainer& outValues,
    const tchar* failureContext,
    const tchar* label
){
    const BinaryVectorPayloadFailure::Enum failure = ReadBinaryVectorPayload(binary, inOutCursor, count, outValues);
    if(failure == BinaryVectorPayloadFailure::None)
        return true;

    if(failure == BinaryVectorPayloadFailure::CountOverflow){
        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: '{}' payload byte size overflows"), failureContext, label);
    }
    else{
        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: malformed '{}' payload"), failureContext, label);
    }
    return false;
}

[[nodiscard]] bool ReadComplete(const Core::Assets::AssetBytes& binary, const usize cursor, const tchar* failureContext){
    if(cursor == binary.size())
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("{} failed: trailing bytes detected"), failureContext);
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Skeleton::validatePayload()const{
    if(!virtualPath()){
        NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::validatePayload failed: virtual path is empty"));
        return false;
    }
    if(m_joints.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::validatePayload failed: skeleton has no joints"));
        return false;
    }

    for(usize i = 0u; i < m_joints.size(); ++i){
        const SkeletonJoint& joint = m_joints[i];
        if(!joint.name){
            NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::validatePayload failed: joint {} has empty name"), i);
            return false;
        }
        if(joint.parentIndex != s_SkeletonInvalidJointIndex && joint.parentIndex >= i){
            NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::validatePayload failed: joint {} has invalid parent {}")
                , i
                , joint.parentIndex
            );
            return false;
        }
        for(usize other = 0u; other < i; ++other){
            if(m_joints[other].name == joint.name){
                NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::validatePayload failed: duplicate joint name at joint {}"), i);
                return false;
            }
        }
    }
    return true;
}

bool Skeleton::loadBinary(const Core::Assets::AssetBytes& binary){
    m_joints.clear();

    usize cursor = 0u;
    SkeletonBinaryPayload::HeaderBinary header;
    if(!ReadPOD(binary, cursor, header)){
        NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::loadBinary failed: malformed header"));
        return false;
    }
    if(header.magic != SkeletonBinaryPayload::s_SkeletonMagic){
        NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::loadBinary failed: invalid skeleton asset format; recook required"));
        return false;
    }

    Core::Assets::AssetVector<SkeletonBinaryPayload::JointBinary> jointBinaries(m_joints.get_allocator().arena());
    if(!__hidden_skeleton_runtime::ReadVector(
        binary,
        cursor,
        header.jointCount,
        jointBinaries,
        NWB_TEXT("Skeleton::loadBinary"),
        NWB_TEXT("joints")
    ))
        return false;

    m_joints.reserve(jointBinaries.size());
    for(const SkeletonBinaryPayload::JointBinary& jointBinary : jointBinaries){
        SkeletonJoint joint;
        joint.name = Name(jointBinary.nameHash);
        joint.parentIndex = jointBinary.parentIndex;
        joint.localBindPose = jointBinary.localBindPose;
        m_joints.push_back(joint);
    }

    return __hidden_skeleton_runtime::ReadComplete(binary, cursor, NWB_TEXT("Skeleton::loadBinary"))
        && validatePayload()
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
