// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "skin_asset.h"
#include "skin_binary_payload.h"

#include <core/common/log.h>
#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skin_cook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ValueContainer>
[[nodiscard]] bool AppendVector(
    Core::Assets::AssetBytes& outBinary,
    const ValueContainer& values,
    const tchar* failureContext,
    const tchar* label
){
    const BinaryVectorPayloadFailure::Enum failure = AppendBinaryVectorPayload(outBinary, values);
    if(failure == BinaryVectorPayloadFailure::None)
        return true;

    if(failure == BinaryVectorPayloadFailure::CountOverflow){
        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: '{}' payload byte size overflows"), failureContext, label);
    }
    else{
        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: '{}' payload overflows output binary"), failureContext, label);
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool SkinAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(asset.assetType() != assetType()){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinAssetCodec::serialize failed: invalid asset type '{}', expected '{}'")
            , StringConvert(asset.assetType().c_str())
            , StringConvert(Skin::s_AssetTypeText)
        );
        return false;
    }

    const Skin& skin = static_cast<const Skin&>(asset);
    if(!skin.validatePayload())
        return false;

    usize reserveBytes = sizeof(SkinBinaryPayload::HeaderBinary);
    const bool canReserve =
        AddBinaryVectorReserveBytes(reserveBytes, skin.influences())
        && AddBinaryVectorReserveBytes(reserveBytes, skin.inverseBindMatrices())
    ;

    outBinary.clear();
    if(canReserve)
        outBinary.reserve(reserveBytes);

    SkinBinaryPayload::HeaderBinary header;
    header.meshNameHash = skin.mesh().name().hash();
    header.skeletonNameHash = skin.skeleton().name().hash();
    header.influenceCount = static_cast<u64>(skin.influences().size());
    header.inverseBindMatrixCount = static_cast<u64>(skin.inverseBindMatrices().size());
    AppendPOD(outBinary, header);

    return __hidden_skin_cook::AppendVector(
        outBinary,
        skin.influences(),
        NWB_TEXT("SkinAssetCodec::serialize"),
        NWB_TEXT("influences")
    )
        && __hidden_skin_cook::AppendVector(
            outBinary,
            skin.inverseBindMatrices(),
            NWB_TEXT("SkinAssetCodec::serialize"),
            NWB_TEXT("inverse bind matrices")
        )
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
