// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "material_asset.h"

#include <core/alloc/scratch.h>
#include <logger/client/logger.h>
#include <core/assets/asset_auto_registration.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_material_asset{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr u32 s_MaterialMagic = 0x4D544C33u; // MTL3
static constexpr u32 s_MaterialVersion = 3u;


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
    m_stageShaders.clear();
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
    constexpr usize shaderEntryBytes = sizeof(NameHash) * 2u;
    if(cursor > binary.size() || shaderCount > (binary.size() - cursor) / shaderEntryBytes){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: shader count exceeds available data"));
        return false;
    }
    m_stageShaders.reserve(shaderCount);

    for(u32 i = 0; i < shaderCount; ++i){
        NameHash stageNameHash = {};
        NameHash shaderNameHash = {};
        if(!ReadPOD(binary, cursor, stageNameHash) || !ReadPOD(binary, cursor, shaderNameHash)){
            NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: malformed shader stage at index {}"), i);
            return false;
        }

        const Name stageName(stageNameHash);
        const Name shaderName(shaderNameHash);
        Core::Assets::AssetRef<Shader> shaderAsset;
        shaderAsset.virtualPath = shaderName;
        if(!stageName || !shaderAsset.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: shader stage entries must not be empty"));
            return false;
        }

        if(!m_stageShaders.emplace(stageName, shaderAsset).second){
            NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: duplicate shader stage '{}'")
                , StringConvert(stageName.c_str())
            );
            return false;
        }
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
    if(material.stageShaders().empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: material has no shader stages"));
        return false;
    }
    if(material.parameters().size() > Limit<u32>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: parameter count exceeds u32 range"));
        return false;
    }
    if(material.stageShaders().size() > Limit<u32>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: shader stage count exceeds u32 range"));
        return false;
    }

    usize reserveBytes =
        sizeof(u32) + // magic
        sizeof(u32)   // version
    ;
    bool canReserve = AddBinaryStringReserveBytes(reserveBytes, AStringView(material.shaderVariant()))
        && AddBinaryReserveBytes(reserveBytes, sizeof(u32))
        && AddBinaryRepeatedReserveBytes(reserveBytes, material.stageShaders().size(), sizeof(NameHash) * 2u)
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
    AppendPOD(outBinary, static_cast<u32>(material.stageShaders().size()));

    using ShaderStageEntry = Pair<const Name*, const Core::Assets::AssetRef<Shader>*>;
    Core::Alloc::ScratchArena<> scratchArena;
    Vector<ShaderStageEntry, Core::Alloc::ScratchAllocator<ShaderStageEntry>> sortedShaders{Core::Alloc::ScratchAllocator<ShaderStageEntry>(scratchArena)};
    sortedShaders.resize(material.stageShaders().size());
    usize sortedShaderIndex = 0u;
    for(const auto& [stageName, shaderAsset] : material.stageShaders())
        sortedShaders[sortedShaderIndex++] = { &stageName, &shaderAsset };

    Sort(sortedShaders.begin(), sortedShaders.end(),
        [](const ShaderStageEntry& lhs, const ShaderStageEntry& rhs){
            return *lhs.first() < *rhs.first();
        }
    );

    for(const ShaderStageEntry& shaderStageEntry : sortedShaders){
        const Name* stageName = shaderStageEntry.first();
        const Core::Assets::AssetRef<Shader>* shaderAsset = shaderStageEntry.second();
        if(!*stageName || !shaderAsset->valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: shader stage entries must not be empty"));
            return false;
        }
        AppendPOD(outBinary, stageName->hash());
        AppendPOD(outBinary, shaderAsset->name().hash());
    }

    AppendPOD(outBinary, static_cast<u32>(material.parameters().size()));

    using ParamEntry = Pair<const CompactString*, const CompactString*>;
    Vector<ParamEntry, Core::Alloc::ScratchAllocator<ParamEntry>> sortedParams{Core::Alloc::ScratchAllocator<ParamEntry>(scratchArena)};
    sortedParams.resize(material.parameters().size());
    usize sortedParamIndex = 0u;
    for(const auto& [key, value] : material.parameters())
        sortedParams[sortedParamIndex++] = { &key, &value };

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


void Material::setShaderForStage(const Name& stageName, const Core::Assets::AssetRef<Shader>& shaderAsset){
    if(!stageName || !shaderAsset.valid())
        return;

    m_stageShaders.insert_or_assign(stageName, shaderAsset);
}

bool Material::setParameter(const CompactString& key, const CompactString& value){
    if(!key)
        return false;

    m_parameters.insert_or_assign(key, value);
    return true;
}

bool Material::findShaderForStage(const Name& stageName, Core::Assets::AssetRef<Shader>& outShaderAsset)const{
    outShaderAsset.reset();
    if(!stageName)
        return false;

    const auto found = m_stageShaders.find(stageName);
    if(found == m_stageShaders.end())
        return false;

    outShaderAsset = found.value();
    return outShaderAsset.valid();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

