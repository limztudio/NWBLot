// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "asset.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class AssetRegistry final : NoCopy{
private:
    using CodecMapAllocator = Alloc::CustomAllocator<Pair<const Name, UniquePtr<IAssetCodec>>>;
    using CodecMap = HashMap<Name, UniquePtr<IAssetCodec>, Hasher<Name>, EqualTo<Name>, CodecMapAllocator>;


public:
    explicit AssetRegistry(Alloc::CustomArena& arena);

    bool registerCodec(UniquePtr<IAssetCodec>&& codec, bool replaceExisting = false);
    bool unregisterCodec(const Name& assetType);

    bool deserializeAsset(
        const Name& assetType,
        const Name& virtualPath,
        const AssetBytes& binary,
        UniquePtr<IAsset>& outAsset
    )const;


private:
    bool deserializeAssetByName(
        const Name& assetType,
        const Name& virtualPath,
        const AssetBytes& binary,
        UniquePtr<IAsset>& outAsset
    )const;


private:
    mutable Futex m_mutex;
    CodecMap m_codecs;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

