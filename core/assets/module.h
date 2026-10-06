// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using AssetBytes = AssetVector<u8>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_DEFINE_ASSET_TYPE(assetTypeLiteral) \
    static constexpr AStringView s_AssetTypeText = assetTypeLiteral; \
    inline static constexpr Name s_AssetTypeName = Name(assetTypeLiteral); \
    [[nodiscard]] static const Name& AssetTypeName()noexcept{ \
        return s_AssetTypeName; \
    }

#define NWB_DEFINE_ASSET_CODEC_REGISTRAR(codecVariable, codecType) \
    Core::Assets::AssetCodecAutoRegistrar codecVariable(&Core::Assets::CreateAssetCodec<codecType>)

#if defined(NWB_COOK)
#define NWB_ASSET_CODEC_SERIALIZE_DECL() \
public: \
    virtual bool serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const override;
#else
#define NWB_ASSET_CODEC_SERIALIZE_DECL()
#endif
#define NWB_DEFINE_ASSET_CODEC(codecType, assetType) \
    class codecType final : public Core::Assets::AssetCodec<assetType>{ \
    public: \
        codecType() = default; \
        \
        \
        NWB_ASSET_CODEC_SERIALIZE_DECL() \
    }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class IAsset{
protected:
    IAsset() = delete;
    explicit IAsset(const Name& assetType, const Name& virtualPath = s_NameNone)noexcept
        : m_assetType(assetType)
        , m_virtualPath(virtualPath)
    {}


public:
    virtual ~IAsset() = default;


public:
    [[nodiscard]] const Name& assetType()const noexcept{ return m_assetType; }
    [[nodiscard]] const Name& virtualPath()const noexcept{ return m_virtualPath; }

public:
    [[nodiscard]] bool checkVirtualPath(const TStringView failureContext)const{
        if(virtualPath())
            return true;

        NWB_LOGGER_ERROR(GLB_TEXT("{} failed: virtual path is empty"), failureContext);
        return false;
    }


private:
    Name m_assetType = s_NameNone;
    Name m_virtualPath = s_NameNone;
};

template<typename ArenaT>
[[nodiscard]] inline TString<ArenaT> AssetVirtualPathText(ArenaT& arena, const IAsset& asset){
    return asset.virtualPath()
        ? StringConvert(arena, asset.virtualPath().resolvedText())
        : TString<ArenaT>(GLB_TEXT("<unnamed>"), arena)
    ;
}

template<typename AssetT>
class TypedAsset : public IAsset{
protected:
    TypedAsset()noexcept(noexcept(Name(AssetT::AssetTypeName())))
        : IAsset(AssetT::AssetTypeName())
    {}
    explicit TypedAsset(const Name& virtualPath)noexcept(noexcept(Name(AssetT::AssetTypeName())))
        : IAsset(AssetT::AssetTypeName(), virtualPath)
    {}
};

template<typename AssetT>
[[nodiscard]] inline const AssetT* CastAsset(const IAsset* asset){
    if(!asset || asset->assetType() != AssetT::AssetTypeName())
        return nullptr;
    return checked_cast<const AssetT*>(asset);
}


class IAssetCodec{
protected:
    IAssetCodec() = delete;
    explicit IAssetCodec(const Name& assetType)noexcept
        : m_assetType(assetType)
    {}


public:
    virtual ~IAssetCodec() = default;


public:
    [[nodiscard]] const Name& assetType()const noexcept{ return m_assetType; }

public:
    virtual bool deserialize(AssetArena& arena, const Name& virtualPath, const AssetBytes& binary, UniquePtr<IAsset>& outAsset)const = 0;

#if defined(NWB_COOK)
public:
    virtual bool serialize(const IAsset& asset, AssetBytes& outBinary)const = 0;

public:
    [[nodiscard]] bool checkSerializeAssetType(
        const IAsset& asset,
        const TStringView failureContext
    )const{
        if(asset.assetType() == assetType())
            return true;

        NWB_LOGGER_ERROR(GLB_TEXT("{} failed: invalid asset type '{}', expected '{}'")
            , failureContext
            , StringConvert(asset.assetType().resolvedText())
            , StringConvert(assetType().resolvedText())
        );
        return false;
    }
#endif


private:
    Name m_assetType = s_NameNone;
};

template<typename AssetT>
class AssetCodec : public IAssetCodec{
protected:
    AssetCodec()noexcept(noexcept(Name(AssetT::AssetTypeName())))
        : IAssetCodec(AssetT::AssetTypeName())
    {}


public:
    virtual bool deserialize(AssetArena& arena, const Name& virtualPath, const AssetBytes& binary, UniquePtr<IAsset>& outAsset)const final override{
        auto asset = MakeUnique<AssetT>(arena, virtualPath);
        if(!asset->loadBinary(binary))
            return false;

        outAsset = Move(asset);
        return true;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

