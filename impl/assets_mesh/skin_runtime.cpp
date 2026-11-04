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
    if(!ValidateSkinAssetHeaderAndCounts(m_mesh.valid(), m_skeleton.valid(), m_influences.size(), m_inverseBindMatrices.size()))
        return false;
    const u32 jointCount = static_cast<u32>(m_inverseBindMatrices.size());
    if(!ValidateSkinInfluenceStream(m_influences, jointCount))
        return false;
    for(usize index = 0u; index < m_inverseBindMatrices.size(); ++index){
        if(!MatrixIsInvertibleAffine(LoadFloat(m_inverseBindMatrices[index]), s_SkinValidationEpsilon, s_SkinValidationEpsilon)){
            NWB_LOGGER_ERROR(NWB_TEXT("Skin::validatePayload failed: inverse bind matrix {} is not a finite invertible affine matrix"), index);
            return false;
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

