// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "material_asset.h"


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


bool Material::loadBinary(const AStringView virtualPath, const Core::Assets::AssetBytes& binary, AString& outError){
    outError.clear();
    m_virtualPath = virtualPath;
    m_name = NAME_NONE;
    m_shaderName = NAME_NONE;
    m_shaderVariant = NAME_NONE;
    m_parameters.clear();

    usize cursor = 0;
    u32 magic = 0;
    if(!ReadPOD(binary, cursor, magic)){
        outError = "Material::loadBinary failed: missing magic";
        return false;
    }
    if(magic != __hidden_assets::s_MaterialMagic){
        outError = "Material::loadBinary failed: invalid magic";
        return false;
    }

    u32 version = 0;
    if(!ReadPOD(binary, cursor, version)){
        outError = "Material::loadBinary failed: missing version";
        return false;
    }
    if(version != __hidden_assets::s_MaterialVersion){
        outError = StringFormat("Material::loadBinary failed: unsupported version {}", version);
        return false;
    }

    NameHash materialHash{};
    NameHash shaderHash{};
    NameHash variantHash{};
    if(!ReadPOD(binary, cursor, materialHash)
        || !ReadPOD(binary, cursor, shaderHash)
        || !ReadPOD(binary, cursor, variantHash))
    {
        outError = "Material::loadBinary failed: missing name hashes";
        return false;
    }

    u32 parameterCount = 0;
    if(!ReadPOD(binary, cursor, parameterCount)){
        outError = "Material::loadBinary failed: missing parameter count";
        return false;
    }

    m_name = Name(materialHash);
    m_shaderName = Name(shaderHash);
    m_shaderVariant = Name(variantHash);

    for(u32 i = 0; i < parameterCount; ++i){
        AString key;
        AString value;
        if(!ReadString(binary, cursor, key) || !ReadString(binary, cursor, value)){
            outError = StringFormat("Material::loadBinary failed: malformed parameter at index {}", i);
            return false;
        }
        if(key.empty()){
            outError = "Material::loadBinary failed: parameter key is empty";
            return false;
        }

        m_parameters[key] = value;
    }

    if(cursor != binary.size()){
        outError = "Material::loadBinary failed: trailing bytes detected";
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
    if(m_parameters.size() > Limit<u32>::s_Max){
        outError = "Material::saveBinary failed: parameter count exceeds u32 range";
        return false;
    }

    outBinary.clear();
    AppendPOD(outBinary, __hidden_assets::s_MaterialMagic);
    AppendPOD(outBinary, __hidden_assets::s_MaterialVersion);
    AppendPOD(outBinary, m_name.hash());
    AppendPOD(outBinary, m_shaderName.hash());
    AppendPOD(outBinary, m_shaderVariant.hash());
    AppendPOD(outBinary, static_cast<u32>(m_parameters.size()));

    Vector<const AString*> sortedParameterKeys;
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

        if(!AppendString(outBinary, *key) || !AppendString(outBinary, found->second)){
            outError = "Material::saveBinary failed: parameter text is too long";
            return false;
        }
    }

    return true;
}


#endif


void Material::setShader(const Name& shaderName, const Name& variantName){
    m_shaderName = shaderName;
    m_shaderVariant = variantName;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

