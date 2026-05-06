// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "material_asset.h"
#include "material_binary_payload.h"

#include <logger/client/logger.h>
#include <core/assets/asset_auto_registration.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_material_asset{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UniquePtr<Core::Assets::IAssetCodec> CreateMaterialAssetCodec(){
    return MakeUnique<MaterialAssetCodec>();
}
Core::Assets::AssetCodecAutoRegistrar s_MaterialAssetCodecAutoRegistrar(&CreateMaterialAssetCodec);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Material::loadBinary(const Core::Assets::AssetBytes& binary){
    if(!virtualPath()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: virtual path is empty"));
        return false;
    }

    m_shaderVariant.clear();
    clearStageShaders();
    m_parameters.clear();
    m_alpha = 1.f;
    m_transparent = false;
#if defined(NWB_COOK)
    m_alphaPriority = Limit<u32>::s_Max;
    m_modePriority = Limit<u32>::s_Max;
    m_modeTransparent = false;
#endif

    usize cursor = 0;
    u32 magic = 0;
    if(!ReadPOD(binary, cursor, magic)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: missing magic"));
        return false;
    }
    if(magic != MaterialBinaryPayload::s_MaterialMagic){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: invalid magic"));
        return false;
    }

    u32 version = 0;
    if(!ReadPOD(binary, cursor, version)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: missing version"));
        return false;
    }
    if(version != MaterialBinaryPayload::s_MaterialVersion){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: unsupported version {}"), version);
        return false;
    }

    if(!ReadString(binary, cursor, m_shaderVariant)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: missing shader variant"));
        return false;
    }

    u32 shaderCount = 0;
    if(!ReadPOD(binary, cursor, shaderCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: missing shader count"));
        return false;
    }
    if(shaderCount == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: material has no shader stages"));
        return false;
    }
    if(shaderCount > static_cast<u32>(Core::ShaderType::Count)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: shader count exceeds supported shader stage count"));
        return false;
    }
    if(cursor > binary.size() || shaderCount > (binary.size() - cursor) / MaterialBinaryPayload::s_ShaderEntryBytes){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: shader count exceeds available data"));
        return false;
    }

    for(u32 i = 0; i < shaderCount; ++i){
        Core::ShaderType::Enum shaderType = Core::ShaderType::Invalid;
        NameHash shaderNameHash = {};
        if(!ReadPOD(binary, cursor, shaderType) || !ReadPOD(binary, cursor, shaderNameHash)){
            NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: malformed shader stage at index {}"), i);
            return false;
        }

        const Name shaderName(shaderNameHash);
        Core::Assets::AssetRef<Shader> shaderAsset;
        shaderAsset.virtualPath = shaderName;
        if(!Core::ShaderType::IsValid(shaderType) || !shaderAsset.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: shader stage entries must not be empty"));
            return false;
        }

        const usize shaderIndex = Core::ShaderType::ToIndex(shaderType);
        if(m_stageShaders[shaderIndex].valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: duplicate shader stage index {}"), shaderIndex);
            return false;
        }

        m_stageShaders[shaderIndex] = shaderAsset;
        ++m_stageShaderCount;
    }

    u32 materialFlags = 0u;
    if(!ReadPOD(binary, cursor, m_alpha) || !ReadPOD(binary, cursor, materialFlags)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: missing material render properties"));
        return false;
    }
    if(!IsFinite(m_alpha)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: material alpha is not finite"));
        return false;
    }
    if((materialFlags & ~MaterialBinaryPayload::s_MaterialFlagTransparent) != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: material flags contain unsupported bits {}"), materialFlags);
        return false;
    }
    m_alpha = static_cast<f32>(Max<f64>(0.0, Min<f64>(1.0, static_cast<f64>(m_alpha))));
    m_transparent = (materialFlags & MaterialBinaryPayload::s_MaterialFlagTransparent) != 0u;

    u32 parameterCount = 0;
    if(!ReadPOD(binary, cursor, parameterCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: missing parameter count"));
        return false;
    }
    if(cursor > binary.size() || parameterCount > (binary.size() - cursor) / MaterialBinaryPayload::s_ParameterEntryBytes){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: parameter count exceeds available data"));
        return false;
    }
    m_parameters.reserve(parameterCount);

    for(u32 i = 0; i < parameterCount; ++i){
        MaterialParameterGpuData parameter;
        if(!ReadPOD(binary, cursor, parameter)){
            NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: malformed parameter at index {}"), i);
            return false;
        }

        if(parameter.meta.w == 0u || parameter.meta.w > 4u){
            NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: parameter at index {} has invalid component count {}"), i, parameter.meta.w);
            return false;
        }
        const MaterialParameterValueType::Enum valueType = static_cast<MaterialParameterValueType::Enum>(parameter.meta.z);
        if(valueType != MaterialParameterValueType::Float
            && valueType != MaterialParameterValueType::Int
            && valueType != MaterialParameterValueType::UInt
            && valueType != MaterialParameterValueType::Bool
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: parameter at index {} has invalid value type {}"), i, parameter.meta.z);
            return false;
        }

        m_parameters.push_back(parameter);
    }

    if(cursor != binary.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: trailing bytes detected"));
        return false;
    }

    return true;
}

void Material::clearStageShaders(){
    for(Core::Assets::AssetRef<Shader>& shaderAsset : m_stageShaders)
        shaderAsset.reset();
    m_stageShaderCount = 0;
}

bool Material::setShaderForStage(const Core::ShaderType::Enum shaderType, const Core::Assets::AssetRef<Shader>& shaderAsset){
    if(!Core::ShaderType::IsValid(shaderType) || !shaderAsset.valid())
        return false;

    Core::Assets::AssetRef<Shader>& storedShader = m_stageShaders[Core::ShaderType::ToIndex(shaderType)];
    if(!storedShader.valid())
        ++m_stageShaderCount;

    storedShader = shaderAsset;
    return true;
}

bool Material::findShaderForStage(const Core::ShaderType::Enum shaderType, Core::Assets::AssetRef<Shader>& outShaderAsset)const{
    outShaderAsset.reset();
    if(!Core::ShaderType::IsValid(shaderType))
        return false;

    outShaderAsset = m_stageShaders[Core::ShaderType::ToIndex(shaderType)];
    return outShaderAsset.valid();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

