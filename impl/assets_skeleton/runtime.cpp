// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset.h"
#include "arena_names.h"
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

bool Skeleton::validatePayload(Core::Alloc::ScratchArena& scratchArena)const{
    if(!checkVirtualPath(NWB_TEXT("Skeleton::validatePayload")))
        return false;
    if(m_joints.empty() || m_joints.size() > Limit<u32>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::validatePayload failed: skeleton joint count must fit nonzero u32"));
        return false;
    }

    if(m_jointIndices.size() != m_joints.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::validatePayload failed: joint lookup count does not match joint count"));
        return false;
    }

    Vector<u8, Core::Alloc::ScratchArena> namedJoints(scratchArena);
    namedJoints.resize(m_joints.size(), 0u);
    for(const auto& jointLookup : m_jointIndices){
        const Name jointName = jointLookup.first;
        const u32 jointIndex = jointLookup.second;
        if(!jointName){
            NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::validatePayload failed: joint lookup contains an empty name"));
            return false;
        }
        if(jointIndex >= m_joints.size()){
            NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::validatePayload failed: joint lookup '{}' has invalid index {}"), StringConvert(jointName.resolvedText()), jointIndex);
            return false;
        }
        if(namedJoints[jointIndex] != 0u){
            NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::validatePayload failed: joint {} has multiple names"), jointIndex);
            return false;
        }
        namedJoints[jointIndex] = 1u;
    }
    for(usize i = 0u; i < m_joints.size(); ++i){
        const SkeletonJoint& joint = m_joints[i];
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

bool Skeleton::loadBinary(const Core::Assets::AssetBytes& binary){
    Core::Alloc::ScratchArena scratchArena(AssetsSkeletonArenaScope::s_LoadBinaryArena);
    usize cursor = 0u;
    const auto headerResult = Core::Assets::ReadMagicHeaderPayload<SkeletonBinaryPayload::HeaderBinary>(binary, cursor, SkeletonBinaryPayload::s_SkeletonMagic, NWB_TEXT("Skeleton::loadBinary"), NWB_TEXT("skeleton"));
    if(!headerResult)
        return false;
    const SkeletonBinaryPayload::HeaderBinary& header = *headerResult;

    if(header.padding0 != 0u || header.jointCount > Limit<u32>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::loadBinary failed: reserved header fields or joint count are invalid"));
        return false;
    }
    const auto jointByteCount = ComputeBinaryVectorPayloadBytes<SkeletonBinaryPayload::JointBinary>(header.jointCount);
    if(!jointByteCount){
        NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::loadBinary failed: joint payload byte size overflows"));
        return false;
    }
    if(!BinaryDetail::CanReadBytes(binary, cursor, *jointByteCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::loadBinary failed: malformed joints payload"));
        return false;
    }

    Skeleton candidate(m_joints.get_allocator().arena(), virtualPath());
    const usize jointCount = static_cast<usize>(header.jointCount);
    if(!BinaryDetail::CanStoreValueCount(candidate.m_joints, jointCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::loadBinary failed: joint count exceeds runtime container limits"));
        return false;
    }
    candidate.m_joints.reserve(jointCount);
    candidate.m_jointIndices.reserve(jointCount);
    for(usize jointIndex = 0u; jointIndex < jointCount; ++jointIndex){
        const auto jointBinaryResult = ReadPOD<SkeletonBinaryPayload::JointBinary>(binary, cursor);
        if(!jointBinaryResult){
            NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::loadBinary failed: malformed joint {}"), jointIndex);
            return false;
        }
        const SkeletonBinaryPayload::JointBinary& jointBinary = *jointBinaryResult;
        if(jointBinary.padding0 != 0u || jointBinary.padding1[0u] != 0u || jointBinary.padding1[1u] != 0u){
            NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::loadBinary failed: reserved joint fields must be zero"));
            return false;
        }
        const Name jointName(jointBinary.nameHash);
        if(!candidate.m_jointIndices.emplace(jointName, static_cast<u32>(candidate.m_joints.size())).second){
            NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::loadBinary failed: duplicate joint '{}'"), StringConvert(jointName.resolvedText()));
            return false;
        }

        SkeletonJoint joint;
        joint.parentIndex = jointBinary.parentIndex;
        joint.localBindPose = jointBinary.localBindPose;
        candidate.m_joints.push_back(joint);
    }

    if(!Core::Assets::ReadCompletePayload(binary, cursor, NWB_TEXT("Skeleton::loadBinary")) || !candidate.validatePayload(scratchArena))
        return false;
    *this = Move(candidate);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

