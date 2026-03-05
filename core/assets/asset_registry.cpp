// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset_registry.h"

#include <logger/client/logger.h>


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

bool AssetRegistry::deserializeAsset(const AStringView assetType, const AStringView virtualPath, const AssetBytes& binary, UniquePtr<IAsset>& outAsset)const{
    outAsset.reset();

    const AString typeName = ::CanonicalizeText(assetType);
    if(typeName.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetRegistry: asset type is empty"));
        return false;
    }

    const IAssetCodec* codec = nullptr;
    {
        ScopedLock lock(m_mutex);
        const auto found = m_codecs.find(typeName);
        if(found != m_codecs.end())
            codec = found.value().get();
    }

    if(codec == nullptr){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetRegistry: no codec for type '{}'"), StringConvert(assetType));
        return false;
    }

    return codec->deserialize(virtualPath, binary, outAsset);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

