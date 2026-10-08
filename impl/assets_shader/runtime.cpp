// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset.h"
#include "binary_payload.h"

#include <core/common/log.h>
#include <core/graphics/spirv_entry_point.h>
#include <core/assets/auto_registration.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_runtime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_REGISTER_SHADER_ASSET_CODEC(Type, Stage, Text) NWB_DEFINE_ASSET_CODEC_REGISTRAR(s_##Type##AssetCodecAutoRegistrar, Type##AssetCodec);
NWB_SHADER_ASSET_TYPE_ENTRIES(NWB_REGISTER_SHADER_ASSET_CODEC)
#undef NWB_REGISTER_SHADER_ASSET_CODEC


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


IShader::~IShader()noexcept = default;


bool IShader::loadBinaryForStage(const Core::Assets::AssetBytes& binary, const Core::ShaderType::Enum shaderType){
    if(!checkVirtualPath(NWB_TEXT("IShader::loadBinaryForStage")))
        return false;

    const auto payload = ShaderBinaryPayload::DecodeAssetPayload(binary);
    if(!payload){
        switch(payload.error()){
        case ShaderBinaryPayload::AssetPayloadDecodeFailure::InvalidHeader:
            NWB_LOGGER_ERROR(NWB_TEXT("IShader::loadBinaryForStage failed: invalid shader payload header"));
            return false;
        case ShaderBinaryPayload::AssetPayloadDecodeFailure::UnsupportedVersion:
            NWB_LOGGER_ERROR(NWB_TEXT("IShader::loadBinaryForStage failed: unsupported shader payload version"));
            return false;
        case ShaderBinaryPayload::AssetPayloadDecodeFailure::InvalidEntryPoint:
            NWB_LOGGER_ERROR(NWB_TEXT("IShader::loadBinaryForStage failed: invalid shader entry point"));
            return false;
        case ShaderBinaryPayload::AssetPayloadDecodeFailure::InvalidBytecode:
            NWB_LOGGER_ERROR(NWB_TEXT("IShader::loadBinaryForStage failed: invalid SPIR-V bytecode"));
            return false;
        }
        return false;
    }

    const auto lookupResult = Core::ResolveSpirvEntryPointName(payload->bytecode, payload->entryPoint, Core::ShaderType::ToMask(shaderType));
    if(!lookupResult){
        NWB_LOGGER_ERROR(NWB_TEXT("IShader::loadBinaryForStage failed: entry point '{}' does not match asset type '{}' or SPIR-V is malformed")
            , StringConvert(payload->entryPoint)
            , StringConvert(assetType().resolvedText())
        );
        return false;
    }

    Core::Assets::AssetString entryPoint(payload->entryPoint.begin(), payload->entryPoint.end(), m_entryPoint.get_allocator().arena());
    Core::Assets::AssetBytes bytecode(payload->bytecode.data(), payload->bytecode.data() + payload->bytecode.size(), m_bytecode.get_allocator().arena());
    m_entryPoint = Move(entryPoint);
    m_bytecode = Move(bytecode);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

