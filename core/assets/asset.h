// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using AssetBytes = Vector<u8>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class IAsset{
public:
    virtual ~IAsset() = default;


public:
    [[nodiscard]] virtual AStringView assetType()const = 0;
    [[nodiscard]] virtual AStringView virtualPath()const = 0;
};


class IAssetCodec{
public:
    virtual ~IAssetCodec() = default;


public:
    [[nodiscard]] virtual AStringView assetType()const = 0;

    virtual bool deserialize(
        AStringView virtualPath,
        const AssetBytes& binary,
        UniquePtr<IAsset>& outAsset,
        AString& outError
    )const = 0;
#if defined(NWB_COOK)
    virtual bool serialize(
        const IAsset& asset,
        AssetBytes& outBinary,
        AString& outError
    )const = 0;
#endif
};


template<typename T>
class AssetCodecOf : public IAssetCodec{
public:
    virtual bool deserialize(AStringView virtualPath, const AssetBytes& binary, UniquePtr<IAsset>& outAsset, AString& outError)const override{
        auto asset = MakeUnique<T>();
        if(!asset->loadBinary(virtualPath, binary, outError))
            return false;
        outAsset = Move(asset);
        return true;
    }

#if defined(NWB_COOK)
    virtual bool serialize(const IAsset& asset, AssetBytes& outBinary, AString& outError)const override{
        if(asset.assetType() != assetType()){
            outError = StringFormat(
                "'{}' codec: invalid asset type '{}', expected '{}'",
                assetType(),
                asset.assetType(),
                assetType()
            );
            return false;
        }
        return static_cast<const T&>(asset).saveBinary(outBinary, outError);
    }
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

