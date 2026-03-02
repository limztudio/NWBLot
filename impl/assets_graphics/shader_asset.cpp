// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shader_asset.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Shader::loadBinary(const AStringView virtualPath, const Core::Assets::AssetBytes& binary, AString& outError){
    m_virtualPath = virtualPath;
    m_bytecode = binary;
    outError.clear();
    return true;
}

#if defined(NWB_COOK)


bool Shader::saveBinary(Core::Assets::AssetBytes& outBinary, AString& outError)const{
    outBinary = m_bytecode;
    outError.clear();
    return true;
}


#endif


void Shader::setMetadata(
    const Name& shaderName,
    const Name& variantName,
    const AStringView stage,
    const AStringView entryPoint,
    const AStringView sourceChecksumHex,
    const AStringView bytecodeChecksumHex,
    const AStringView virtualPath
){
    m_shaderName = shaderName;
    m_variantName = variantName;
    m_stage = stage;
    m_entryPoint = entryPoint;
    m_sourceChecksumHex = sourceChecksumHex;
    m_bytecodeChecksumHex = bytecodeChecksumHex;
    m_virtualPath = virtualPath;
}


bool Shader::buildIndexLine(AString& outLine, AString& outError)const{
    if(!m_shaderName || !m_variantName || m_stage.empty() || m_entryPoint.empty() || m_virtualPath.empty()){
        outError = "Shader::buildIndexLine failed: required metadata is empty";
        return false;
    }

    outLine.clear();
    outLine += ::EncodeNameHash<char>(m_shaderName);
    outLine += '|';
    outLine += ::EncodeNameHash<char>(m_variantName);
    outLine += '|';
    outLine += m_stage;
    outLine += '|';
    outLine += m_entryPoint;
    outLine += '|';
    outLine += m_sourceChecksumHex;
    outLine += '|';
    outLine += m_bytecodeChecksumHex;
    outLine += '|';
    outLine += m_virtualPath;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ShaderAssetCodec::deserialize(
    const AStringView virtualPath,
    const Core::Assets::AssetBytes& binary,
    UniquePtr<Core::Assets::IAsset>& outAsset,
    AString& outError
)const{
    auto shader = MakeUnique<Shader>();
    if(!shader->loadBinary(virtualPath, binary, outError))
        return false;

    outAsset = Move(shader);
    return true;
}

#if defined(NWB_COOK)


bool ShaderAssetCodec::serialize(
    const Core::Assets::IAsset& asset,
    Core::Assets::AssetBytes& outBinary,
    AString& outError
)const{
    if(asset.assetType() != assetType()){
        outError = StringFormat(
            "ShaderAssetCodec: invalid asset type '{}', expected '{}'",
            asset.assetType(),
            assetType()
        );
        return false;
    }

    return static_cast<const Shader&>(asset).saveBinary(outBinary, outError);
}


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

