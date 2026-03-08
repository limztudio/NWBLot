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

    const Name typeName = codec->assetType();
    if(!typeName)
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

bool AssetRegistry::unregisterCodec(const Name& assetType){
    if(!assetType)
        return false;

    ScopedLock lock(m_mutex);
    return m_codecs.erase(assetType) != 0;
}

bool AssetRegistry::deserializeAsset(
    const Name& assetType,
    const Name& virtualPath,
    const AssetBytes& binary,
    UniquePtr<IAsset>& outAsset
)const{
    outAsset.reset();

    if(!assetType){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetRegistry: asset type is empty"));
        return false;
    }

    return deserializeAssetByName(assetType, virtualPath, binary, outAsset);
}

bool AssetRegistry::deserializeAssetByName(
    const Name& assetType,
    const Name& virtualPath,
    const AssetBytes& binary,
    UniquePtr<IAsset>& outAsset
)const{
    ScopedLock lock(m_mutex);
    const auto found = m_codecs.find(assetType);
    if(found == m_codecs.end() || found.value() == nullptr){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetRegistry: no codec for type '{}'"), StringConvert(assetType.c_str()));
        return false;
    }

    const NotNull<const IAssetCodec*> codec(found.value().get());
    return codec->deserialize(virtualPath, binary, outAsset);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

