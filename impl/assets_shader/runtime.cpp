// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset.h"
#include "shader_types.h"

#include <core/common/log.h>
#include <core/graphics/spirv_entry_point.h>
#include <core/assets/auto_registration.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_shader_runtime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_REGISTER_SHADER_ASSET_CODEC(Type) NWB_DEFINE_ASSET_CODEC_REGISTRAR(s_##Type##AssetCodecAutoRegistrar, ShaderAssetCodec<Type>);
NWB_SHADER_ASSET_TYPE_ENTRIES(NWB_REGISTER_SHADER_ASSET_CODEC)
#undef NWB_REGISTER_SHADER_ASSET_CODEC


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<ShaderBinaryPayload::AssetPayloadView, ShaderBinaryPayload::AssetPayloadDecodeFailure::Enum> ShaderBinaryPayload::DecodeAssetPayload(const Span<const u8> payload)noexcept{
    usize cursor = 0u;
    const auto magic = ReadPOD<u32>(payload, cursor);
    const auto version = ReadPOD<u32>(payload, cursor);
    if(!magic || !version || *magic != s_AssetPayloadMagic)
        return MakeUnexpected(AssetPayloadDecodeFailure::InvalidHeader);
    if(*version != s_AssetPayloadVersion)
        return MakeUnexpected(AssetPayloadDecodeFailure::UnsupportedVersion);
    const auto entryPoint = ReadString(payload, cursor);
    if(!entryPoint || entryPoint->empty())
        return MakeUnexpected(AssetPayloadDecodeFailure::InvalidEntryPoint);

    const BinaryByteView bytecode{ payload.data() + cursor, payload.size() - cursor };
    if(ValidateBytecode(bytecode) != BytecodeValidationFailure::None)
        return MakeUnexpected(AssetPayloadDecodeFailure::InvalidBytecode);
    return AssetPayloadView{ *entryPoint, bytecode };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


IShader::~IShader()noexcept = default;


b8 IShader::loadBinaryForStage(const Span<const u8> binary, const Core::ShaderType::Enum shaderType){
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

    const auto lookupResult = Core::ResolveSpirvEntryPointName(BinaryByteView{ payload->bytecode.data(), payload->bytecode.size() }, payload->entryPoint, Core::ShaderType::ToMask(shaderType));
    if(!lookupResult){
        NWB_LOGGER_ERROR(NWB_TEXT("IShader::loadBinaryForStage failed: entry point '{}' does not match asset type '{}' or SPIR-V is malformed"), ConvertString(payload->entryPoint), ConvertString(assetType().resolvedText()));
        return false;
    }

    Core::Assets::AssetString entryPoint(payload->entryPoint.begin(), payload->entryPoint.end(), m_entryPoint.get_allocator().arena());
    Core::Assets::AssetBytes bytecode(payload->bytecode.data(), payload->bytecode.data() + payload->bytecode.size(), m_bytecode.get_allocator().arena());
    m_entryPoint = Move(entryPoint);
    m_bytecode = Move(bytecode);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Core::ShaderType::Enum ShaderAssetTypes::ShaderTypeFromAssetType(const Name& assetType)noexcept{
#define NWB_SHADER_ASSET_TYPE_MATCH(Type) if(assetType == Type::AssetTypeName()) return Type::s_Stage;
    NWB_SHADER_ASSET_TYPE_ENTRIES(NWB_SHADER_ASSET_TYPE_MATCH)
#undef NWB_SHADER_ASSET_TYPE_MATCH
    return Core::ShaderType::Invalid;
}

Core::ShaderType::Enum ShaderAssetTypes::ShaderTypeFromAssetTypeText(const AStringView assetType)noexcept{
#define NWB_SHADER_ASSET_TYPE_TEXT_MATCH(Type) if(assetType == Type::s_AssetTypeText) return Type::s_Stage;
    NWB_SHADER_ASSET_TYPE_ENTRIES(NWB_SHADER_ASSET_TYPE_TEXT_MATCH)
#undef NWB_SHADER_ASSET_TYPE_TEXT_MATCH
    return Core::ShaderType::Invalid;
}

const Name& ShaderAssetTypes::AssetTypeNameFromShaderType(const Core::ShaderType::Enum shaderType)noexcept{
    switch(shaderType){
#define NWB_SHADER_ASSET_TYPE_CASE(Type) case Type::s_Stage: return Type::AssetTypeName();
        NWB_SHADER_ASSET_TYPE_ENTRIES(NWB_SHADER_ASSET_TYPE_CASE)
#undef NWB_SHADER_ASSET_TYPE_CASE
        default: return s_NameNone;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

