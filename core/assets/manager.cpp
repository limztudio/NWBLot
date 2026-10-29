// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "manager.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AssetManager::AssetManager(AssetArena& arena, const AssetRegistry& registry, const IAssetBinarySource& binarySource)
    : m_registry(registry)
    , m_binarySource(binarySource)
    , m_arena(arena)
{}


Expected<UniquePtr<IAsset>> AssetManager::loadSync(const Name& assetType, const Name& virtualPath)const{

    if(!assetType){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetManager: asset type is empty"));
        return MakeUnexpected(Failure{});
    }
    if(!virtualPath){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetManager: virtual path is empty"));
        return MakeUnexpected(Failure{});
    }

    AssetBytes binary{m_arena};
    if(!m_binarySource.readAssetBinary(virtualPath, binary)){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetManager: failed to read binary for asset '{}' of type '{}'"), ConvertString(virtualPath.resolvedText()), ConvertString(assetType.resolvedText()));
        return MakeUnexpected(Failure{});
    }

    auto asset = m_registry.deserializeAsset(assetType, virtualPath, binary);
    if(!asset){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetManager: failed to deserialize asset '{}' of type '{}'"), ConvertString(virtualPath.resolvedText()), ConvertString(assetType.resolvedText()));
        return MakeUnexpected(Failure{});
    }

    return asset;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

