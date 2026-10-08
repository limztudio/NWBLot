// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "skin_asset.h"
#include "skin_binary_payload.h"

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
    return true;
}

bool Skin::loadBinary(const Core::Assets::AssetBytes& binary){
    m_mesh.reset();
    m_skeleton.reset();
    m_influences.clear();
    m_inverseBindMatrices.clear();

    usize cursor = 0u;
    const auto headerResult = Core::Assets::ReadMagicHeaderPayload<SkinBinaryPayload::HeaderBinary>(
        binary,
        cursor,
        SkinBinaryPayload::s_SkinMagic,
        NWB_TEXT("Skin::loadBinary"),
        NWB_TEXT("skin")
    );
    if(!headerResult)
        return false;
    const SkinBinaryPayload::HeaderBinary& header = *headerResult;

    m_mesh.virtualPath = Name(header.meshNameHash);
    m_skeleton.virtualPath = Name(header.skeletonNameHash);

    if(!Core::Assets::ReadVectorPayload(
        binary,
        cursor,
        header.influenceCount,
        m_influences,
        NWB_TEXT("Skin::loadBinary"),
        NWB_TEXT("influences")
    ))
        return false;
    if(!Core::Assets::ReadVectorPayload(
        binary,
        cursor,
        header.inverseBindMatrixCount,
        m_inverseBindMatrices,
        NWB_TEXT("Skin::loadBinary"),
        NWB_TEXT("inverse bind matrices")
    ))
        return false;

    return Core::Assets::ReadCompletePayload(binary, cursor, NWB_TEXT("Skin::loadBinary"))
        && validatePayload()
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

