// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "skin_asset.h"
#include "skin_binary_payload.h"

#include <core/assets/auto_registration.h>
#include <core/common/log.h>
#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skin_runtime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UniquePtr<Core::Assets::IAssetCodec> CreateSkinAssetCodec(){
    return MakeUnique<SkinAssetCodec>();
}
Core::Assets::AssetCodecAutoRegistrar s_SkinAssetCodecAutoRegistrar(&CreateSkinAssetCodec);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ValueContainer>
[[nodiscard]] bool ReadVector(
    const Core::Assets::AssetBytes& binary,
    usize& inOutCursor,
    const u64 count,
    ValueContainer& outValues,
    const tchar* failureContext,
    const tchar* label
){
    const BinaryVectorPayloadFailure::Enum failure = ReadBinaryVectorPayload(binary, inOutCursor, count, outValues);
    if(failure == BinaryVectorPayloadFailure::None)
        return true;

    if(failure == BinaryVectorPayloadFailure::CountOverflow){
        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: '{}' payload byte size overflows"), failureContext, label);
    }
    else{
        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: malformed '{}' payload"), failureContext, label);
    }
    return false;
}

[[nodiscard]] bool ReadComplete(const Core::Assets::AssetBytes& binary, const usize cursor, const tchar* failureContext){
    if(cursor == binary.size())
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("{} failed: trailing bytes detected"), failureContext);
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

    if(!__hidden_skin_runtime::ReadVector(
        binary,
        cursor,
        header.influenceCount,
        m_influences,
        NWB_TEXT("Skin::loadBinary"),
        NWB_TEXT("influences")
    ))
        return false;
    if(!__hidden_skin_runtime::ReadVector(
        binary,
        cursor,
        header.inverseBindMatrixCount,
        m_inverseBindMatrices,
        NWB_TEXT("Skin::loadBinary"),
        NWB_TEXT("inverse bind matrices")
    ))
        return false;

    return __hidden_skin_runtime::ReadComplete(binary, cursor, NWB_TEXT("Skin::loadBinary"))
        && validatePayload()
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
