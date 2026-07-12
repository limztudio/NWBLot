
#include "skin_asset.h"
#include "skin_binary_payload.h"

#include <global/core/assets/auto_registration.h>
#include <global/core/assets/binary_payload_io.h>
#include <global/core/common/log.h>
#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skin_runtime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Core::Assets::AssetCodecAutoRegistrar s_SkinAssetCodecAutoRegistrar(&Core::Assets::CreateAssetCodec<SkinAssetCodec>);


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Skin::validatePayload()const{
    if(!virtualPath()){
        NWB_LOGGER_ERROR(NWB_TEXT("Skin::validatePayload failed: virtual path is empty"));
        return false;
    }
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
    SkinBinaryPayload::HeaderBinary header;
    if(!ReadPOD(binary, cursor, header)){
        NWB_LOGGER_ERROR(NWB_TEXT("Skin::loadBinary failed: malformed header"));
        return false;
    }
    if(header.magic != SkinBinaryPayload::s_SkinMagic){
        NWB_LOGGER_ERROR(NWB_TEXT("Skin::loadBinary failed: invalid skin asset format; recook required"));
        return false;
    }

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

