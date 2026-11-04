// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "skin_asset.h"

#include <core/assets/auto_registration.h>
#include <core/assets/binary_payload_io.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skin_runtime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_ASSET_CODEC_REGISTRAR(s_SkinAssetCodecAutoRegistrar, SkinAssetCodec);
NWB_DEFINE_SUBSYSTEM_ASSET_REGISTRAR(s_SkinningSubsystemAssetAutoRegistrar, SkinningSubsystem);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 Skin::loadBinary(const Span<const u8> binary){
    usize cursor = 0u;
    const auto headerResult = Core::Assets::ReadMagicHeaderPayload<SkinHeaderBinary>(binary, s_SkinBinaryMagic, s_SkinLoadBinaryContext, s_SkinAssetKindLabel, cursor);
    if(!headerResult)
        return false;
    const SkinHeaderBinary& header = *headerResult;

    if(!ValidateSkinHeaderBinary(header))
        return false;
    Skin candidate(m_influences.get_allocator().arena(), virtualPath());
    candidate.m_mesh.virtualPath = Name(header.meshNameHash);
    candidate.m_skeleton.virtualPath = Name(header.skeletonNameHash);

    if(!Core::Assets::ReadVectorPayload(binary, header.influenceCount, s_SkinLoadBinaryContext, s_SkinInfluencesPayloadLabel, cursor, candidate.m_influences))
        return false;
    if(!Core::Assets::ReadVectorPayload(binary, header.inverseBindMatrixCount, s_SkinLoadBinaryContext, s_SkinInverseBindMatricesPayloadLabel, cursor, candidate.m_inverseBindMatrices))
        return false;

    if(!Core::Assets::ReadCompletePayload(binary, cursor, s_SkinLoadBinaryContext) || !candidate.validatePayload())
        return false;
    *this = Move(candidate);
    return true;
}

b8 Skin::validatePayload()const{
    if(!checkVirtualPath(NWB_TEXT("Skin::validatePayload")))
        return false;
    return ValidateSkinAssetPayload(m_mesh.valid(), m_skeleton.valid(), m_influences, m_inverseBindMatrices);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

