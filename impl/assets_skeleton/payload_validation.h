// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "asset.h"

#include <core/common/log.h>

#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsSkeletonPayloadDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline bool ValidateJoints(const Span<const SkeletonJoint> joints, const HashMap<Name, u32, ArenaT, Hasher<Name>, EqualTo<Name>>& jointIndices, Core::Alloc::ScratchArena& scratchArena){
    if(joints.empty() || joints.size() > Limit<u32>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::validatePayload failed: skeleton joint count must fit nonzero u32"));
        return false;
    }

    if(jointIndices.size() != joints.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::validatePayload failed: joint lookup count does not match joint count"));
        return false;
    }

    Vector<u8, Core::Alloc::ScratchArena> namedJoints(scratchArena);
    namedJoints.resize(joints.size(), 0u);
    for(const auto& jointLookup : jointIndices){
        const Name jointName = jointLookup.first;
        const u32 jointIndex = jointLookup.second;
        if(!jointName){
            NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::validatePayload failed: joint lookup contains an empty name"));
            return false;
        }
        if(jointIndex >= joints.size()){
            NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::validatePayload failed: joint lookup '{}' has invalid index {}"), StringConvert(jointName.resolvedText()), jointIndex);
            return false;
        }
        if(namedJoints[jointIndex] != 0u){
            NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::validatePayload failed: joint {} has multiple names"), jointIndex);
            return false;
        }
        namedJoints[jointIndex] = 1u;
    }
    for(usize i = 0u; i < joints.size(); ++i){
        const SkeletonJoint& joint = joints[i];
        if(!MatrixIsAffine(LoadFloat(joint.localBindPose), 0.0f)){
            NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::validatePayload failed: joint {} has non-finite local bind pose"), i);
            return false;
        }
        if(joint.parentIndex != s_SkeletonInvalidJointIndex && joint.parentIndex >= i){
            NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::validatePayload failed: joint {} has invalid parent {}"), i, joint.parentIndex);
            return false;
        }
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

