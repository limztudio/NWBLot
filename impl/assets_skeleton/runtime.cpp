// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset.h"

#include <core/assets/auto_registration.h>
#include <core/assets/binary_payload_io.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skeleton_runtime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr TStringView s_SkeletonLoadBinaryContext = NWB_TEXT("Skeleton::loadBinary");

NWB_DEFINE_ASSET_CODEC_REGISTRAR(s_SkeletonAssetCodecAutoRegistrar, SkeletonAssetCodec);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void Skeleton::setJoints(JointVector&& joints, JointIndexMap&& jointIndices)noexcept{
    m_joints = Move(joints);
    m_jointIndices = Move(jointIndices);
}

u32 Skeleton::findJointIndex(const Name jointName)const noexcept{
    const auto foundJoint = m_jointIndices.find(jointName);
    return foundJoint != m_jointIndices.end() ? foundJoint.value() : s_SkeletonInvalidJointIndex;
}

b8 Skeleton::validatePayload(Core::Alloc::ScratchArena& scratchArena)const{
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
            NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::validatePayload failed: joint lookup '{}' has invalid index {}"), ConvertString(jointName.resolvedText()), jointIndex);
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

b8 Skeleton::loadBinary(const Span<const u8> binary){
    using namespace __hidden_skeleton_runtime;

    Core::Alloc::ScratchArena scratchArena(s_AssetsSkeletonLoadBinaryArena);
    usize cursor = 0u;
    const auto headerResult = Core::Assets::ReadMagicHeaderPayload<SkeletonHeaderBinary>(binary, s_SkeletonBinaryMagic, s_SkeletonLoadBinaryContext, s_SkeletonAssetKindLabel, cursor);
    if(!headerResult)
        return false;
    const SkeletonHeaderBinary& header = *headerResult;

    if(!IsValidSkeletonHeaderBinary(header)){
        NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::loadBinary failed: reserved header fields or joint count are invalid"));
        return false;
    }
    const auto jointByteCount = ComputeBinaryVectorPayloadBytes<SkeletonJointBinary>(header.jointCount);
    if(!jointByteCount){
        NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::loadBinary failed: joint payload byte size overflows"));
        return false;
    }
    if(!CanReadBytes(binary, cursor, *jointByteCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::loadBinary failed: malformed joints payload"));
        return false;
    }

    Skeleton candidate(m_joints.get_allocator().arena(), virtualPath());
    const usize jointCount = static_cast<usize>(header.jointCount);
    if(!CanStoreValueCount(candidate.m_joints, jointCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::loadBinary failed: joint count exceeds runtime container limits"));
        return false;
    }
    candidate.m_joints.reserve(jointCount);
    candidate.m_jointIndices.reserve(jointCount);
    for(usize jointIndex = 0u; jointIndex < jointCount; ++jointIndex){
        const auto jointBinaryResult = ReadPOD<SkeletonJointBinary>(binary, cursor);
        if(!jointBinaryResult){
            NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::loadBinary failed: malformed joint {}"), jointIndex);
            return false;
        }
        const SkeletonJointBinary& jointBinary = *jointBinaryResult;
        if(!IsValidSkeletonJointBinaryPadding(jointBinary)){
            NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::loadBinary failed: reserved joint fields must be zero"));
            return false;
        }
        const Name jointName(jointBinary.nameHash);
        if(!candidate.m_jointIndices.emplace(jointName, static_cast<u32>(candidate.m_joints.size())).second){
            NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::loadBinary failed: duplicate joint '{}'"), ConvertString(jointName.resolvedText()));
            return false;
        }

        candidate.m_joints.push_back(UnpackSkeletonJointBinary(jointBinary));
    }

    if(!Core::Assets::ReadCompletePayload(binary, cursor, s_SkeletonLoadBinaryContext) || !candidate.validatePayload(scratchArena))
        return false;
    *this = Move(candidate);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

