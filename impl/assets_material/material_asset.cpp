// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "material_asset.h"

#include <core/alloc/scratch.h>
#include <core/graphics/shader_stage_names.h>
#include <logger/client/logger.h>
#include <core/assets/asset_auto_registration.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_material_asset{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr u32 s_MaterialMagic = 0x4D544C33u; // MTL3
static constexpr u32 s_MaterialVersion = 4u;
static constexpr usize s_ShaderEntryBytes = sizeof(Core::ShaderType::Enum) + sizeof(NameHash);

static_assert(sizeof(Core::ShaderType::Enum) == sizeof(u8), "Material shader stage indices must stay byte-sized");


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

    usize cursor = 0;
    u32 magic = 0;
    if(!ReadPOD(binary, cursor, magic)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: missing magic"));
        return false;
    }
    if(magic != __hidden_material_asset::s_MaterialMagic){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: invalid magic"));
        return false;
    }

    u32 version = 0;
    if(!ReadPOD(binary, cursor, version)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: missing version"));
        return false;
    }
    if(version != __hidden_material_asset::s_MaterialVersion){
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
    if(cursor > binary.size() || shaderCount > (binary.size() - cursor) / __hidden_material_asset::s_ShaderEntryBytes){
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

    u32 parameterCount = 0;
    if(!ReadPOD(binary, cursor, parameterCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: missing parameter count"));
        return false;
    }
    constexpr usize minParameterEntryBytes = sizeof(u32) * 2u;
    if(cursor > binary.size() || parameterCount > (binary.size() - cursor) / minParameterEntryBytes){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: parameter count exceeds available data"));
        return false;
    }
    m_parameters.reserve(parameterCount);

    for(u32 i = 0; i < parameterCount; ++i){
        CompactString key;
        CompactString value;
        if(!ReadString(binary, cursor, key) || !ReadString(binary, cursor, value)){
            NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: malformed parameter at index {}"), i);
            return false;
        }
        if(!key){
            NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: parameter key is empty"));
            return false;
        }

        if(!m_parameters.emplace(key, value).second){
            NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: duplicate parameter key '{}'"), StringConvert(key.c_str()));
            return false;
        }
    }

    if(cursor != binary.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: trailing bytes detected"));
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool MaterialAssetCodec::deserialize(const Name& virtualPath, const Core::Assets::AssetBytes& binary, UniquePtr<Core::Assets::IAsset>& outAsset)const{
    return Core::Assets::DeserializeTypedAsset<Material>(virtualPath, binary, outAsset);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool MaterialAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(asset.assetType() != assetType()){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: invalid asset type '{}', expected '{}'")
            , StringConvert(asset.assetType().c_str())
            , StringConvert(Material::s_AssetTypeText)
        );
        return false;
    }

    const Material& material = static_cast<const Material&>(asset);
    if(!material.virtualPath()){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: virtual path is empty"));
        return false;
    }
    if(material.stageShaderCount() == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: material has no shader stages"));
        return false;
    }
    if(material.parameters().size() > Limit<u32>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: parameter count exceeds u32 range"));
        return false;
    }

    usize reserveBytes =
        sizeof(u32) + // magic
        sizeof(u32)   // version
    ;
    bool canReserve = AddBinaryStringReserveBytes(reserveBytes, AStringView(material.shaderVariant()))
        && AddBinaryReserveBytes(reserveBytes, sizeof(u32))
        && AddBinaryRepeatedReserveBytes(reserveBytes, material.stageShaderCount(), __hidden_material_asset::s_ShaderEntryBytes)
        && AddBinaryReserveBytes(reserveBytes, sizeof(u32))
    ;
    for(const auto& [key, value] : material.parameters()){
        canReserve = canReserve
            && AddBinaryStringReserveBytes(reserveBytes, key.view())
            && AddBinaryStringReserveBytes(reserveBytes, value.view())
        ;
    }

    outBinary.clear();
    if(canReserve)
        outBinary.reserve(reserveBytes);

    AppendPOD(outBinary, __hidden_material_asset::s_MaterialMagic);
    AppendPOD(outBinary, __hidden_material_asset::s_MaterialVersion);
    if(!AppendString(outBinary, AStringView(material.shaderVariant()))){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: shader variant is too long"));
        return false;
    }
    AppendPOD(outBinary, material.stageShaderCount());

    const Material::StageShaderArray& stageShaders = material.stageShaders();
    for(usize shaderIndex = 0; shaderIndex < stageShaders.size(); ++shaderIndex){
        const Core::Assets::AssetRef<Shader>& shaderAsset = stageShaders[shaderIndex];
        if(!shaderAsset.valid())
            continue;

        const Core::ShaderType::Enum shaderType = static_cast<Core::ShaderType::Enum>(shaderIndex);
        if(!Core::ShaderType::IsValid(shaderType)){
            NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: shader stage index {} is invalid"), shaderIndex);
            return false;
        }

        AppendPOD(outBinary, shaderType);
        AppendPOD(outBinary, shaderAsset.name().hash());
    }

    AppendPOD(outBinary, static_cast<u32>(material.parameters().size()));

    using ParamEntry = Pair<const CompactString*, const CompactString*>;
    Core::Alloc::ScratchArena<> scratchArena;
    Vector<ParamEntry, Core::Alloc::ScratchAllocator<ParamEntry>> sortedParams{Core::Alloc::ScratchAllocator<ParamEntry>(scratchArena)};
    sortedParams.reserve(material.parameters().size());
    for(const auto& [key, value] : material.parameters())
        sortedParams.emplace_back(&key, &value);

    Sort(sortedParams.begin(), sortedParams.end(),
        [](const ParamEntry& lhs, const ParamEntry& rhs){
            return *lhs.first() < *rhs.first();
        }
    );

    for(const ParamEntry& paramEntry : sortedParams){
        const CompactString* key = paramEntry.first();
        const CompactString* value = paramEntry.second();
        if(!*key){
            NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: parameter key must not be empty"));
            return false;
        }
        if(!AppendString(outBinary, *key) || !AppendString(outBinary, *value)){
            NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: parameter text is too long"));
            return false;
        }
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

bool Material::setShaderForStage(const Name& stageName, const Core::Assets::AssetRef<Shader>& shaderAsset){
    return setShaderForStage(Core::ShaderStageNames::ShaderTypeFromArchiveStageName(stageName), shaderAsset);
}

bool Material::setParameter(const CompactString& key, const CompactString& value){
    if(!key)
        return false;

    m_parameters.insert_or_assign(key, value);
    return true;
}

bool Material::findShaderForStage(const Core::ShaderType::Enum shaderType, Core::Assets::AssetRef<Shader>& outShaderAsset)const{
    outShaderAsset.reset();
    if(!Core::ShaderType::IsValid(shaderType))
        return false;

    outShaderAsset = m_stageShaders[Core::ShaderType::ToIndex(shaderType)];
    return outShaderAsset.valid();
}

bool Material::findShaderForStage(const Name& stageName, Core::Assets::AssetRef<Shader>& outShaderAsset)const{
    outShaderAsset.reset();
    if(!stageName)
        return false;

    return findShaderForStage(Core::ShaderStageNames::ShaderTypeFromArchiveStageName(stageName), outShaderAsset);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

