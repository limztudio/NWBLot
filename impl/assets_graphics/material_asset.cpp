// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "material_asset.h"

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr u32 s_MaterialMagic = 0x4D544C31u; // MTL1
static constexpr u32 s_MaterialVersion = 1u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Material::loadBinary(const AStringView virtualPath, const Core::Assets::AssetBytes& binary){
    m_virtualPath = virtualPath;
    m_name = NAME_NONE;
    m_shaderName = NAME_NONE;
    m_shaderVariant = NAME_NONE;
    m_parameters.clear();

    usize cursor = 0;
    u32 magic = 0;
    if(!ReadPOD(binary, cursor, magic)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: missing magic"));
        return false;
    }
    if(magic != __hidden_assets::s_MaterialMagic){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: invalid magic"));
        return false;
    }

    u32 version = 0;
    if(!ReadPOD(binary, cursor, version)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: missing version"));
        return false;
    }
    if(version != __hidden_assets::s_MaterialVersion){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: unsupported version {}"), version);
        return false;
    }

    NameHash materialHash{};
    NameHash shaderHash{};
    NameHash variantHash{};
    if(!ReadPOD(binary, cursor, materialHash)
        || !ReadPOD(binary, cursor, shaderHash)
        || !ReadPOD(binary, cursor, variantHash))
    {
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: missing name hashes"));
        return false;
    }

    u32 parameterCount = 0;
    if(!ReadPOD(binary, cursor, parameterCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: missing parameter count"));
        return false;
    }

    m_name = Name(materialHash);
    m_shaderName = Name(shaderHash);
    m_shaderVariant = Name(variantHash);

    for(u32 i = 0; i < parameterCount; ++i){
        AString key;
        AString value;
        if(!ReadString(binary, cursor, key) || !ReadString(binary, cursor, value)){
            NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: malformed parameter at index {}"), i);
            return false;
        }
        if(key.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: parameter key is empty"));
            return false;
        }

        m_parameters[key] = value;
    }

    if(cursor != binary.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: trailing bytes detected"));
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool MaterialAssetCodec::deserialize(const AStringView virtualPath, const Core::Assets::AssetBytes& binary, UniquePtr<Core::Assets::IAsset>& outAsset)const{
    auto asset = MakeUnique<Material>();
    if(!asset->loadBinary(virtualPath, binary))
        return false;

    outAsset = Move(asset);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool MaterialAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(asset.assetType() != assetType()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("MaterialAssetCodec::serialize failed: invalid asset type '{}', expected '{}'"),
            StringConvert(asset.assetType()),
            StringConvert(assetType())
        );
        return false;
    }

    const Material& material = static_cast<const Material&>(asset);
    if(!material.name()){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: name is empty"));
        return false;
    }
    if(material.parameters().size() > Limit<u32>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: parameter count exceeds u32 range"));
        return false;
    }

    outBinary.clear();
    AppendPOD(outBinary, __hidden_assets::s_MaterialMagic);
    AppendPOD(outBinary, __hidden_assets::s_MaterialVersion);
    AppendPOD(outBinary, material.name().hash());
    AppendPOD(outBinary, material.shaderName().hash());
    AppendPOD(outBinary, material.shaderVariant().hash());
    AppendPOD(outBinary, static_cast<u32>(material.parameters().size()));

    using ParamEntry = Pair<const AString*, const AString*>;
    Vector<ParamEntry> sortedParams;
    sortedParams.reserve(material.parameters().size());
    for(const auto& [key, value] : material.parameters())
        sortedParams.push_back({ &key, &value });

    Sort(sortedParams.begin(), sortedParams.end(),
        [](const ParamEntry& lhs, const ParamEntry& rhs){
            return *lhs.first < *rhs.first;
        }
    );

    for(const auto& [key, value] : sortedParams){
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


void Material::setShader(const Name& shaderName, const Name& variantName){
    m_shaderName = shaderName;
    m_shaderVariant = variantName;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

