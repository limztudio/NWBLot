// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "manager.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<UniquePtr<IAsset>> AssetManager::loadSync(const Name& assetType, const Name& virtualPath)const{

    if(!assetType){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetManager: asset type is empty"));
        return MakeUnexpected(Failure{});
    }
    if(!virtualPath){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetManager: virtual path is empty"));
        return MakeUnexpected(Failure{});
    }

    struct LoadSyncContext final{
        const AssetRegistry& registry;
        const Name& assetType;
        const Name& virtualPath;
        Expected<UniquePtr<IAsset>>& asset;
        b8 binaryRead = false;
    };

    Expected<UniquePtr<IAsset>> asset = MakeUnexpected(Failure{});
    LoadSyncContext loadContext{ m_registry, assetType, virtualPath, asset };
    const b8 ok = m_binarySource.visitAssetBinary(virtualPath, m_arena, &loadContext, [](void* const rawContext, const Span<const u8> binary) -> b8{
        auto& ctx = *MakeNotNull(static_cast<LoadSyncContext*>(rawContext));
        ctx.binaryRead = true;
        ctx.asset = ctx.registry.deserializeAsset(ctx.assetType, ctx.virtualPath, binary);
        return ctx.asset.has_value();
    });
    if(!ok){
        if(!loadContext.binaryRead)
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetManager: failed to read binary for asset '{}' of type '{}'"), ConvertString(virtualPath.resolvedText()), ConvertString(assetType.resolvedText()));
        else
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetManager: failed to deserialize asset '{}' of type '{}'"), ConvertString(virtualPath.resolvedText()), ConvertString(assetType.resolvedText()));
        return MakeUnexpected(Failure{});
    }

    return Move(asset);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

