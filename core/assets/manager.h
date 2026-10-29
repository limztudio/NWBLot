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
    template<typename TAsset, typename TResource>
    [[nodiscard]] static b8 CheckLoaderEnter(const AssetRef<TAsset>& assetRef, const TResource& resource, const TStringView owner){
        if(!assetRef.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("{}: {} asset reference is empty"), owner, ConvertString(TAsset::s_AssetTypeText));
            return false;
        }
        return !resource.valid();
    }


public:
    explicit AssetManager(AssetArena& arena, const AssetRegistry& registry, const IAssetBinarySource& binarySource);


public:
    [[nodiscard]] Expected<UniquePtr<IAsset>> loadSync(const Name& assetType, const Name& virtualPath)const;

    template<typename AssetT>
    [[nodiscard]] Expected<UniquePtr<AssetT>> loadTypedSync(const Name& virtualPath, const TStringView ownerName)const{
        auto loadedAsset = loadSync(AssetT::AssetTypeName(), virtualPath);
        if(!loadedAsset){
            NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to load {} asset '{}'"), ownerName, ConvertString(AssetT::s_AssetTypeText), ConvertString(virtualPath.resolvedText()));
            return MakeUnexpected(Failure{});
        }
        const AssetT* typedAsset = CastAsset<AssetT>(loadedAsset->get());
        if(!typedAsset){
            NWB_LOGGER_ERROR(NWB_TEXT("{}: asset '{}' is not a {}"), ownerName, ConvertString(virtualPath.resolvedText()), ConvertString(AssetT::s_AssetTypeText));
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


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

