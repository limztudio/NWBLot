// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset.h"
#include "binary_payload.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ShaderAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(asset.assetType() != assetType()){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderAssetCodec::serialize failed: invalid asset type '{}', expected '{}'")
            , StringConvert(asset.assetType().c_str())
            , StringConvert(Shader::s_AssetTypeText)
        );
        return false;
    }

    const Shader& shader = static_cast<const Shader&>(asset);
    const Core::Assets::AssetBytes& bytecode = shader.bytecode();
    switch(ShaderBinaryPayload::EncodeAssetPayload(AStringView(shader.entryPoint()), bytecode, outBinary)){
    case ShaderBinaryPayload::AssetPayloadFailure::None:
        break;
    case ShaderBinaryPayload::AssetPayloadFailure::InvalidHeader:
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderAssetCodec::serialize failed: invalid shader payload header"));
        return false;
    case ShaderBinaryPayload::AssetPayloadFailure::UnsupportedVersion:
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderAssetCodec::serialize failed: unsupported shader payload version"));
        return false;
    case ShaderBinaryPayload::AssetPayloadFailure::InvalidEntryPoint:
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderAssetCodec::serialize failed: shader entry point is empty or too long"));
        return false;
    case ShaderBinaryPayload::AssetPayloadFailure::InvalidBytecode:
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderAssetCodec::serialize failed: invalid SPIR-V bytecode"));
        return false;
    case ShaderBinaryPayload::AssetPayloadFailure::OutputSizeOverflow:
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderAssetCodec::serialize failed: shader payload size overflow"));
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

