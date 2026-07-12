
#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset.h"
#include "binary_payload.h"

#include <global/core/common/log.h>


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

    const Core::Assets::AssetBytes& bytecode = static_cast<const Shader&>(asset).bytecode();
    switch(ShaderBinaryPayload::ValidateBytecode(bytecode)){
    case ShaderBinaryPayload::BytecodeValidationFailure::None:
        break;
    case ShaderBinaryPayload::BytecodeValidationFailure::InvalidSize:
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderAssetCodec::serialize failed: invalid bytecode size"));
        return false;
    case ShaderBinaryPayload::BytecodeValidationFailure::InvalidMagic:
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderAssetCodec::serialize failed: invalid SPIR-V magic"));
        return false;
    }

    outBinary = bytecode;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

