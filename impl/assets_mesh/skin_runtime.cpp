// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "skin_asset.h"
#include "skin_binary_payload.h"
#include "skin_validation.h"

#include <core/assets/auto_registration.h>
#include <core/assets/binary_payload_io.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skin_runtime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_ASSET_CODEC_REGISTRAR(s_SkinAssetCodecAutoRegistrar, SkinAssetCodec);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Skin::validatePayload()const{
    if(!checkVirtualPath(NWB_TEXT("Skin::validatePayload")))
        return false;
    if(!m_mesh.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("Skin::validatePayload failed: mesh reference is empty"));
        return false;
    }
    if(!m_skeleton.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("Skin::validatePayload failed: skeleton reference is empty"));
        return false;
    }
    if(m_influences.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Skin::validatePayload failed: skin influence stream is empty"));
        return false;
    }
    if(m_inverseBindMatrices.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Skin::validatePayload failed: inverse bind matrix stream is empty"));
        return false;
    }
    if(m_inverseBindMatrices.size() > static_cast<usize>(Limit<u16>::s_Max) + 1u){
        NWB_LOGGER_ERROR(NWB_TEXT("Skin::validatePayload failed: inverse bind matrix count exceeds u16 joint index range"));
        return false;
    }
    const u32 jointCount = static_cast<u32>(m_inverseBindMatrices.size());
    for(usize index = 0u; index < m_influences.size(); ++index){
        const SkinInfluence4& influence = m_influences[index];
        if(!SkinValidation::ValidSkinInfluenceWeights(LoadFloat(influence.weight)) || !SkinValidation::SkinInfluenceFitsSkeleton(influence, jointCount)){
            NWB_LOGGER_ERROR(NWB_TEXT("Skin::validatePayload failed: influence {} has invalid weights or joint indices"), index);
            return false;
        }
    }
    for(usize index = 0u; index < m_inverseBindMatrices.size(); ++index){
        if(!MatrixIsInvertibleAffine(LoadFloat(m_inverseBindMatrices[index]), SkinValidation::s_Epsilon, SkinValidation::s_Epsilon)){
            NWB_LOGGER_ERROR(NWB_TEXT("Skin::validatePayload failed: inverse bind matrix {} is not a finite invertible affine matrix"), index);
            return false;
        }
    }
    return true;
}

bool Skin::loadBinary(const Core::Assets::AssetBytes& binary){
    usize cursor = 0u;
    const auto headerResult = Core::Assets::ReadMagicHeaderPayload<SkinBinaryPayload::HeaderBinary>(binary, cursor, SkinBinaryPayload::s_SkinMagic, NWB_TEXT("Skin::loadBinary"), NWB_TEXT("skin"));
    if(!headerResult)
        return false;
    const SkinBinaryPayload::HeaderBinary& header = *headerResult;

    if(header.padding0 != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Skin::loadBinary failed: reserved header fields must be zero"));
        return false;
    }
    Skin candidate(m_influences.get_allocator().arena(), virtualPath());
    candidate.m_mesh.virtualPath = Name(header.meshNameHash);
    candidate.m_skeleton.virtualPath = Name(header.skeletonNameHash);

    if(!Core::Assets::ReadVectorPayload(binary, cursor, header.influenceCount, candidate.m_influences, NWB_TEXT("Skin::loadBinary"), NWB_TEXT("influences")))
        return false;
    if(!Core::Assets::ReadVectorPayload(binary, cursor, header.inverseBindMatrixCount, candidate.m_inverseBindMatrices, NWB_TEXT("Skin::loadBinary"), NWB_TEXT("inverse bind matrices")))
        return false;

    if(!Core::Assets::ReadCompletePayload(binary, cursor, NWB_TEXT("Skin::loadBinary")) || !candidate.validatePayload())
        return false;
    *this = Move(candidate);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

