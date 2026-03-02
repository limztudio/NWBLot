// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "material_asset.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Material::loadBinary(const AStringView virtualPath, const Core::Assets::AssetBytes& binary, AString& outError){
    outError.clear();
    m_virtualPath = virtualPath;
    m_name = NAME_NONE;
    m_shaderName = NAME_NONE;
    m_shaderVariant = NAME_NONE;
    m_parameters.clear();

    const AString text(reinterpret_cast<const char*>(binary.data()), binary.size());
    AStringStream stream(text);

    AString line;
    bool magicSeen = false;
    while(ReadTextLine(stream, line)){
        if(!line.empty() && line.back() == '\r')
            line.pop_back();
        if(line.empty())
            continue;

        if(!magicSeen){
            if(line != "NWB_MATERIAL_V1"){
                outError = StringFormat("Material::loadBinary failed: invalid header '{}'", line);
                return false;
            }
            magicSeen = true;
            continue;
        }

        const usize equalPos = line.find('=');
        if(equalPos == AString::npos){
            outError = StringFormat("Material::loadBinary failed: malformed line '{}'", line);
            return false;
        }

        const AString key = ::Trim(AStringView(line).substr(0, equalPos));
        const AString value = ::Trim(AStringView(line).substr(equalPos + 1));
        Name parsedName = NAME_NONE;
        if(key == "name_hash"){
            if(!::DecodeNameHash(value, parsedName)){
                outError = StringFormat("Material::loadBinary failed: invalid name_hash '{}'", value);
                return false;
            }

            m_name = parsedName;
            continue;
        }
        if(key == "shader_hash"){
            if(!::DecodeNameHash(value, parsedName)){
                outError = StringFormat("Material::loadBinary failed: invalid shader_hash '{}'", value);
                return false;
            }

            m_shaderName = parsedName;
            continue;
        }
        if(key == "variant_hash"){
            if(!::DecodeNameHash(value, parsedName)){
                outError = StringFormat("Material::loadBinary failed: invalid variant_hash '{}'", value);
                return false;
            }

            m_shaderVariant = parsedName;
            continue;
        }

        if(key == "name"){
            m_name = ::ToName(value);
            continue;
        }
        if(key == "shader"){
            m_shaderName = ::ToName(value);
            continue;
        }
        if(key == "variant"){
            m_shaderVariant = ::ToName(value);
            continue;
        }

        static constexpr AStringView s_ParamPrefix = "param.";
        if(AStringView(key).starts_with(s_ParamPrefix)){
            const AString paramKey = AString(AStringView(key).substr(s_ParamPrefix.size()));
            if(paramKey.empty()){
                outError = "Material::loadBinary failed: parameter key is empty";
                return false;
            }

            m_parameters[paramKey] = value;
            continue;
        }
    }

    if(!magicSeen){
        outError = "Material::loadBinary failed: missing header";
        return false;
    }

    return true;
}

#if defined(NWB_COOK)


bool Material::saveBinary(Core::Assets::AssetBytes& outBinary, AString& outError)const{
    outError.clear();

    if(!m_name){
        outError = "Material::saveBinary failed: name is empty";
        return false;
    }

    AString text;
    text += "NWB_MATERIAL_V1\n";
    text += StringFormat("name_hash={}\n", ::EncodeNameHash<char>(m_name));
    text += StringFormat("shader_hash={}\n", ::EncodeNameHash<char>(m_shaderName));
    text += StringFormat("variant_hash={}\n", ::EncodeNameHash<char>(m_shaderVariant));

    Core::Assets::AssetVector<const AString*> sortedParameterKeys;
    sortedParameterKeys.reserve(m_parameters.size());
    for(const auto& [key, _] : m_parameters)
        sortedParameterKeys.push_back(&key);

    Sort(
        sortedParameterKeys.begin(),
        sortedParameterKeys.end(),
        [](const AString* lhs, const AString* rhs){
            return *lhs < *rhs;
        }
    );

    for(const AString* key : sortedParameterKeys){
        const auto found = m_parameters.find(*key);
        if(found == m_parameters.end())
            continue;

        text += StringFormat("param.{}={}\n", *key, found->second);
    }

    outBinary.assign(text.begin(), text.end());
    return true;
}


#endif


void Material::setShader(const Name& shaderName, const Name& variantName){
    m_shaderName = shaderName;
    m_shaderVariant = variantName;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool MaterialAssetCodec::deserialize(
    const AStringView virtualPath,
    const Core::Assets::AssetBytes& binary,
    UniquePtr<Core::Assets::IAsset>& outAsset,
    AString& outError
)const{
    auto material = MakeUnique<Material>();
    if(!material->loadBinary(virtualPath, binary, outError))
        return false;

    outAsset = Move(material);
    return true;
}

#if defined(NWB_COOK)


bool MaterialAssetCodec::serialize(
    const Core::Assets::IAsset& asset,
    Core::Assets::AssetBytes& outBinary,
    AString& outError
)const{
    if(asset.assetType() != assetType()){
        outError = StringFormat(
            "MaterialAssetCodec: invalid asset type '{}', expected '{}'",
            asset.assetType(),
            assetType()
        );
        return false;
    }

    return static_cast<const Material&>(asset).saveBinary(outBinary, outError);
}


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

