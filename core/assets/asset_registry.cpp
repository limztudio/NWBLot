// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset_registry.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetRegistry::registerCodec(UniquePtr<IAssetCodec>&& codec, const bool replaceExisting){
    if(!codec)
        return false;

    const AString typeName = ::CanonicalizeText(codec->assetType());
    if(typeName.empty())
        return false;

    ScopedLock lock(m_mutex);

    const auto found = m_codecs.find(typeName);
    if(found != m_codecs.end()){
        if(!replaceExisting)
            return false;

        found.value() = Move(codec);
        return true;
    }

    m_codecs[typeName] = Move(codec);
    return true;
}


bool AssetRegistry::unregisterCodec(const AStringView assetType){
    const AString typeName = ::CanonicalizeText(assetType);
    if(typeName.empty())
        return false;

    ScopedLock lock(m_mutex);
    return m_codecs.erase(typeName) != 0;
}


const IAssetCodec* AssetRegistry::findCodec(const AStringView assetType)const{
    const AString typeName = ::CanonicalizeText(assetType);
    if(typeName.empty())
        return nullptr;

    ScopedLock lock(m_mutex);

    const auto found = m_codecs.find(typeName);
    if(found == m_codecs.end())
        return nullptr;

    return found->second.get();
}


bool AssetRegistry::deserializeAsset(
    const AStringView assetType,
    const AStringView virtualPath,
    const AssetBytes& binary,
    UniquePtr<IAsset>& outAsset,
    AString& outError
)const{
    outAsset.reset();
    outError.clear();

    const IAssetCodec* codec = findCodec(assetType);
    if(codec == nullptr){
        outError = StringFormat("AssetRegistry: no codec for type '{}'", assetType);
        return false;
    }

    return codec->deserialize(virtualPath, binary, outAsset, outError);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetRegistry::serializeAsset(
    const IAsset& asset,
    AssetBytes& outBinary,
    AString& outError
)const{
    outBinary.clear();
    outError.clear();

    const IAssetCodec* codec = findCodec(asset.assetType());
    if(codec == nullptr){
        outError = StringFormat("AssetRegistry: no codec for type '{}'", asset.assetType());
        return false;
    }

    return codec->serialize(asset, outBinary, outError);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

