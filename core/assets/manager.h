// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "registry.h"
#include "ref.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class IAssetBinarySource{
public:
    virtual ~IAssetBinarySource()noexcept = default;


public:
    virtual b8 readAssetBinary(const Name& virtualPath, AssetBytes& outBinary)const = 0;
};


class AssetManager final : NoCopy{
public:
    template<typename TAsset, typename TResource, typename OwnerStringT>
    [[nodiscard]] static b8 CheckLoaderEnter(const AssetRef<TAsset>& assetRef, const TResource& resource, const OwnerStringT& owner){
        if(!assetRef.valid()){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("{}: {} asset reference is empty"), ConvertString(owner), ConvertString(TAsset::s_AssetTypeText));
            return false;
        }
        return !resource.valid();
    }


public:
    explicit AssetManager(AssetArena& arena, const AssetRegistry& registry, const IAssetBinarySource& binarySource)
        : m_registry(registry)
        , m_binarySource(binarySource)
        , m_arena(arena)
    {}


public:
    [[nodiscard]] Expected<UniquePtr<IAsset>> loadSync(const Name& assetType, const Name& virtualPath)const;

    template<typename AssetT, typename OwnerNameT>
    [[nodiscard]] Expected<UniquePtr<AssetT>> loadTypedSync(const Name& virtualPath, const OwnerNameT& ownerName)const{
        auto loadedAsset = loadSync(AssetT::AssetTypeName(), virtualPath);
        if(!loadedAsset){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("{}: failed to load {} asset '{}'"), ConvertString(ownerName), ConvertString(AssetT::s_AssetTypeText), ConvertString(virtualPath.resolvedText()));
            return MakeUnexpected(Failure{});
        }
        const AssetT* typedAsset = CastAsset<AssetT>(loadedAsset->get());
        if(!typedAsset){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("{}: asset '{}' is not a {}"), ConvertString(ownerName), ConvertString(virtualPath.resolvedText()), ConvertString(AssetT::s_AssetTypeText));
            return MakeUnexpected(Failure{});
        }
        return UniquePtr<AssetT>(checked_cast<AssetT*>(loadedAsset->release()));
    }


private:
    const AssetRegistry& m_registry;
    const IAssetBinarySource& m_binarySource;
    AssetArena& m_arena;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename TAsset, typename TResource, typename OwnerNameT>
[[nodiscard]] inline b8 CheckAssetLoaderEnter(const AssetRef<TAsset>& assetRef, const TResource& resource, const OwnerNameT& ownerName){
    return AssetManager::CheckLoaderEnter(assetRef, resource, ownerName);
}

template<typename AssetT, typename OwnerNameT>
[[nodiscard]] inline Expected<UniquePtr<AssetT>> LoadTypedAssetSync(const AssetManager& assetManager, const Name& virtualPath, const OwnerNameT& ownerName){
    return assetManager.loadTypedSync<AssetT>(virtualPath, ownerName);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

