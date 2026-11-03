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


b8 Skeleton::validatePayload(Core::Alloc::ScratchArena& scratchArena)const{
    if(!checkVirtualPath(NWB_TEXT("Skeleton::validatePayload")))
        return false;
    if(!ValidateSkeletonJointCounts(m_joints.size(), m_jointIndices.size()))
        return false;

    Vector<u8, Core::Alloc::ScratchArena> namedJoints(scratchArena);
    namedJoints.resize(m_joints.size(), 0u);
    for(const auto& jointLookup : m_jointIndices){
        const Name jointName = jointLookup.first;
        const u32 jointIndex = jointLookup.second;
        u8 dummyFlag = 0u;
        u8& flagRef = jointIndex < m_joints.size() ? namedJoints[jointIndex] : dummyFlag;
        if(!ValidateSkeletonJointLookupEntry(jointName, jointIndex, m_joints.size(), flagRef))
            return false;
    }
    return ValidateSkeletonJointsArray(m_joints);
}

b8 Skeleton::loadBinary(const Span<const u8> binary){
    using namespace __hidden_skeleton_runtime;

    Core::Alloc::ScratchArena scratchArena(s_AssetsSkeletonLoadBinaryArena);
    usize cursor = 0u;
    const auto headerResult = Core::Assets::ReadMagicHeaderPayload<SkeletonHeaderBinary>(binary, s_SkeletonBinaryMagic, s_SkeletonLoadBinaryContext, s_SkeletonAssetKindLabel, cursor);
    if(!headerResult)
        return false;
    const SkeletonHeaderBinary& header = *headerResult;

    if(!ValidateSkeletonHeaderBinary(header))
        return false;
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
        if(!ValidateSkeletonJointBinaryPadding(jointBinary))
            return false;
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

