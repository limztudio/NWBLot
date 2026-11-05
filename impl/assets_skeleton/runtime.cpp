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
static_assert(sizeof(SkeletonHeaderBinary) == s_SkeletonHeaderBinaryBytes, "SkeletonHeaderBinary layout drifted");
static_assert(sizeof(SkeletonJointBinary) == s_SkeletonJointBinaryBytes, "SkeletonJointBinary layout drifted");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 Skeleton::validatePayload(Core::Alloc::ScratchArena& scratchArena)const{
    if(!checkVirtualPath(NWB_TEXT("Skeleton::validatePayload")))
        return false;

    Vector<u8, Core::Alloc::ScratchArena> namedJoints(scratchArena);
    return ValidateSkeletonPayloadData(m_joints, m_jointIndices, namedJoints);
}

b8 Skeleton::loadBinary(const Span<const u8> binary){
    using namespace __hidden_skeleton_runtime;

    Core::Alloc::ScratchArena scratchArena(s_AssetsSkeletonLoadBinaryArena);
    usize cursor = 0u;
    const auto headerResult = Core::Assets::ReadMagicHeaderPayload<SkeletonHeaderBinary>(binary, s_SkeletonBinaryMagic, s_SkeletonLoadBinaryContext, s_SkeletonAssetKindLabel, cursor);
    if(!headerResult)
        return false;
    const SkeletonHeaderBinary& header = *headerResult;

    Skeleton candidate(m_joints.get_allocator().arena(), virtualPath());
    if(!ReadSkeletonJointsPayload(binary, header, cursor, candidate.m_joints, candidate.m_jointIndices))
        return false;

    if(!Core::Assets::ReadCompletePayload(binary, cursor, s_SkeletonLoadBinaryContext) || !candidate.validatePayload(scratchArena))
        return false;
    *this = Move(candidate);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

