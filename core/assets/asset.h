// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using AssetBytes = Vector<u8>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_DEFINE_ASSET_TYPE(assetTypeLiteral) \
    static constexpr AStringView s_AssetTypeText = assetTypeLiteral; \
    [[nodiscard]] static const Name& AssetTypeName(){ \
        static const Name s_AssetType(s_AssetTypeText.data()); \
        return s_AssetType; \
    }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class IAsset{
protected:
    IAsset() = delete;
    explicit IAsset(const Name& assetType, const Name& virtualPath = NAME_NONE)
        : m_assetType(assetType)
        , m_virtualPath(virtualPath)
    {}


public:
    virtual ~IAsset() = default;


public:
    [[nodiscard]] const Name& assetType()const{ return m_assetType; }
    [[nodiscard]] const Name& virtualPath()const{ return m_virtualPath; }


private:
    Name m_assetType = NAME_NONE;
    Name m_virtualPath = NAME_NONE;
};

template<typename AssetT>
class TypedAsset : public IAsset{
protected:
    TypedAsset()
        : IAsset(AssetT::AssetTypeName())
    {}
    explicit TypedAsset(const Name& virtualPath)
        : IAsset(AssetT::AssetTypeName(), virtualPath)
    {}
};


class IAssetCodec{
protected:
    IAssetCodec() = delete;
    explicit IAssetCodec(const Name& assetType)
        : m_assetType(assetType)
    {}


public:
    virtual ~IAssetCodec() = default;


public:
    [[nodiscard]] const Name& assetType()const{ return m_assetType; }

    virtual bool deserialize(
        const Name& virtualPath,
        const AssetBytes& binary,
        UniquePtr<IAsset>& outAsset
    )const = 0;
#if defined(NWB_COOK)
    virtual bool serialize(
        const IAsset& asset,
        AssetBytes& outBinary
    )const = 0;
#endif


private:
    Name m_assetType = NAME_NONE;
};

template<typename AssetT>
class TypedAssetCodec : public IAssetCodec{
protected:
    TypedAssetCodec()
        : IAssetCodec(AssetT::AssetTypeName())
    {}
};


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
