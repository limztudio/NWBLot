// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset.h"
#include "binary_payload.h"

#include <core/assets/auto_registration.h>
#include <core/assets/binary_payload_io.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skeleton_runtime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_ASSET_CODEC_REGISTRAR(s_SkeletonAssetCodecAutoRegistrar, SkeletonAssetCodec);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void Skeleton::setJoints(JointVector&& joints, JointIndexMap&& jointIndices){
    m_joints = Move(joints);
    m_jointIndices = Move(jointIndices);
}

u32 Skeleton::findJointIndex(const Name jointName)const{
    const auto foundJoint = m_jointIndices.find(jointName);
    if(foundJoint == m_jointIndices.end())
        return s_SkeletonInvalidJointIndex;
    return foundJoint.value();
}

bool Skeleton::validatePayload()const{
    if(!checkVirtualPath(NWB_TEXT("Skeleton::validatePayload")))
        return false;
    if(m_joints.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::validatePayload failed: skeleton has no joints"));
        return false;
    }

    if(m_jointIndices.size() != m_joints.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::validatePayload failed: joint lookup count does not match joint count"));
        return false;
    }

    Core::Assets::AssetVector<u8> namedJoints(m_joints.get_allocator().arena());
    namedJoints.resize(m_joints.size(), 0u);
    for(const auto& jointLookup : m_jointIndices){
        const Name jointName = jointLookup.first;
        const u32 jointIndex = jointLookup.second;
        if(!jointName){
            NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::validatePayload failed: joint lookup contains an empty name"));
            return false;
        }
        if(jointIndex >= m_joints.size()){
            NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::validatePayload failed: joint lookup '{}' has invalid index {}")
                , StringConvert(jointName.resolvedText())
                , jointIndex
            );
            return false;
        }
        if(namedJoints[jointIndex] != 0u){
            NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::validatePayload failed: joint {} has multiple names"), jointIndex);
            return false;
        }
        namedJoints[jointIndex] = 1u;
    }
    for(usize jointIndex = 0u; jointIndex < namedJoints.size(); ++jointIndex){
        if(namedJoints[jointIndex] != 0u)
            continue;

        NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::validatePayload failed: joint {} has no name"), jointIndex);
        return false;
    }

    for(usize i = 0u; i < m_joints.size(); ++i){
        const SkeletonJoint& joint = m_joints[i];
        if(joint.parentIndex != s_SkeletonInvalidJointIndex && joint.parentIndex >= i){
            NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::validatePayload failed: joint {} has invalid parent {}")
                , i
                , joint.parentIndex
            );
            return false;
        }
    }

    return true;
}

bool Skeleton::loadBinary(const Core::Assets::AssetBytes& binary){
    m_joints.clear();
    m_jointIndices.clear();

    usize cursor = 0u;
    const auto headerResult = Core::Assets::ReadMagicHeaderPayload<SkeletonBinaryPayload::HeaderBinary>(
        binary,
        cursor,
        SkeletonBinaryPayload::s_SkeletonMagic,
        NWB_TEXT("Skeleton::loadBinary"),
        NWB_TEXT("skeleton")
    );
    if(!headerResult)
        return false;
    const SkeletonBinaryPayload::HeaderBinary& header = *headerResult;

    Core::Assets::AssetVector<SkeletonBinaryPayload::JointBinary> jointBinaries(m_joints.get_allocator().arena());
    if(!Core::Assets::ReadVectorPayload(
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
        const Name jointName(jointBinary.nameHash);
        if(!m_jointIndices.emplace(jointName, static_cast<u32>(m_joints.size())).second){
            NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::loadBinary failed: duplicate joint '{}'"), StringConvert(jointName.resolvedText()));
            return false;
        }

        SkeletonJoint joint;
        joint.parentIndex = jointBinary.parentIndex;
        joint.localBindPose = jointBinary.localBindPose;
        m_joints.push_back(joint);
    }

    return Core::Assets::ReadCompletePayload(binary, cursor, NWB_TEXT("Skeleton::loadBinary"))
        && validatePayload()
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

