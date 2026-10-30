// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset.h"
#include "payload_validation.h"

#include <core/assets/auto_registration.h>
#include <core/assets/binary_payload_io.h>
#include <core/common/log.h>

#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skeleton_runtime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr TStringView s_SkeletonAssetKindLabel = NWB_TEXT("skeleton");
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
    if(foundJoint == m_jointIndices.end())
        return s_SkeletonInvalidJointIndex;
    return foundJoint.value();
}

b8 Skeleton::validatePayload(Core::Alloc::ScratchArena& scratchArena)const{
    if(!checkVirtualPath(NWB_TEXT("Skeleton::validatePayload")))
        return false;
    return AssetsSkeletonPayloadDetail::ValidateJoints<Core::Assets::AssetArena>(m_joints, m_jointIndices, scratchArena);
}

b8 Skeleton::loadBinary(const Span<const u8> binary){
    using namespace __hidden_skeleton_runtime;

    Core::Alloc::ScratchArena scratchArena(s_AssetsSkeletonLoadBinaryArena);
    usize cursor = 0u;
    const auto headerResult = Core::Assets::ReadMagicHeaderPayload<SkeletonHeaderBinary>(binary, s_SkeletonBinaryMagic, s_SkeletonLoadBinaryContext, s_SkeletonAssetKindLabel, cursor);
    if(!headerResult)
        return false;
    const SkeletonHeaderBinary& header = *headerResult;

    if(header.padding0 != 0u || header.jointCount > Limit<u32>::s_Max){
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
        if(jointBinary.padding0 != 0u || jointBinary.padding1[0u] != 0u || jointBinary.padding1[1u] != 0u){
            NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::loadBinary failed: reserved joint fields must be zero"));
            return false;
        }
        const Name jointName(jointBinary.nameHash);
        if(!candidate.m_jointIndices.emplace(jointName, static_cast<u32>(candidate.m_joints.size())).second){
            NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::loadBinary failed: duplicate joint '{}'"), ConvertString(jointName.resolvedText()));
            return false;
        }

        SkeletonJoint joint;
        joint.parentIndex = jointBinary.parentIndex;
        joint.localBindPose = jointBinary.localBindPose;
        candidate.m_joints.push_back(joint);
    }

    if(!Core::Assets::ReadCompletePayload(binary, cursor, s_SkeletonLoadBinaryContext) || !candidate.validatePayload(scratchArena))
        return false;
    *this = Move(candidate);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
