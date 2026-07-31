// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset.h"
#include "binary_payload.h"

#include <core/common/log.h>
#include <core/assets/auto_registration.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_runtime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Core::Assets::AssetCodecAutoRegistrar s_ShaderAssetCodecAutoRegistrar(&Core::Assets::CreateAssetCodec<ShaderAssetCodec>);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Shader::loadBinary(const Core::Assets::AssetBytes& binary){
    if(!virtualPath()){
        NWB_LOGGER_ERROR(NWB_TEXT("Shader::loadBinary failed: virtual path is empty"));
        return false;
    }

    Core::Assets::AssetString entryPoint(m_entryPoint.get_allocator().arena());
    Core::Assets::AssetBytes bytecode(m_bytecode.get_allocator().arena());
    switch(ShaderBinaryPayload::DecodeAssetPayload(binary, entryPoint, bytecode)){
    case ShaderBinaryPayload::AssetPayloadFailure::None:
        break;
    case ShaderBinaryPayload::AssetPayloadFailure::InvalidHeader:
        NWB_LOGGER_ERROR(NWB_TEXT("Shader::loadBinary failed: invalid shader payload header"));
        return false;
    case ShaderBinaryPayload::AssetPayloadFailure::UnsupportedVersion:
        NWB_LOGGER_ERROR(NWB_TEXT("Shader::loadBinary failed: unsupported shader payload version"));
        return false;
    case ShaderBinaryPayload::AssetPayloadFailure::InvalidEntryPoint:
        NWB_LOGGER_ERROR(NWB_TEXT("Shader::loadBinary failed: invalid shader entry point"));
        return false;
    case ShaderBinaryPayload::AssetPayloadFailure::InvalidBytecode:
        NWB_LOGGER_ERROR(NWB_TEXT("Shader::loadBinary failed: invalid SPIR-V bytecode"));
        return false;
    case ShaderBinaryPayload::AssetPayloadFailure::OutputSizeOverflow:
        NWB_LOGGER_ERROR(NWB_TEXT("Shader::loadBinary failed: shader payload exceeds runtime limits"));
        return false;
    }

    m_entryPoint = Move(entryPoint);
    m_bytecode = Move(bytecode);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

