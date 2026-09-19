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
    [[nodiscard]] static const Name& AssetTypeName(){ \
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
    explicit IAsset(const Name& assetType, const Name& virtualPath = NAME_NONE)
        : m_assetType(assetType)
        , m_virtualPath(virtualPath)
    {}


public:
    virtual ~IAsset() = default;


public:
    [[nodiscard]] const Name& assetType()const{ return m_assetType; }
    [[nodiscard]] const Name& virtualPath()const{ return m_virtualPath; }

public:
    // Shared "virtual path is empty" guard used by loadBinary/validatePayload/serialize entry points.
    [[nodiscard]] bool checkVirtualPath(const NotNull<const tchar*> failureContext)const{
        if(virtualPath())
            return true;

        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: virtual path is empty"), failureContext.get());
        return false;
    }


private:
    Name m_assetType = NAME_NONE;
    Name m_virtualPath = NAME_NONE;
};

template<typename ArenaT>
[[nodiscard]] inline TString<ArenaT> AssetVirtualPathText(ArenaT& arena, const IAsset& asset){
    return asset.virtualPath()
        ? StringConvert(arena, asset.virtualPath().c_str())
        : TString<ArenaT>(NWB_TEXT("<unnamed>"), arena)
    ;
}

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

// Shared checked downcast for typed assets: verifies the stored asset type before casting.
template<typename AssetT>
[[nodiscard]] inline const AssetT* CastAsset(const IAsset* asset){
    if(!asset || asset->assetType() != AssetT::AssetTypeName())
        return nullptr;
    return checked_cast<const AssetT*>(asset);
}


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

public:
    virtual bool deserialize(AssetArena& arena, const Name& virtualPath, const AssetBytes& binary, UniquePtr<IAsset>& outAsset)const = 0;

#if defined(NWB_COOK)
public:
    virtual bool serialize(const IAsset& asset, AssetBytes& outBinary)const = 0;

public:
    [[nodiscard]] bool checkSerializeAssetType(
        const IAsset& asset,
        const NotNull<const tchar*> failureContext
    )const{
        if(asset.assetType() == assetType())
            return true;

        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: invalid asset type '{}', expected '{}'")
            , failureContext.get()
            , StringConvert(asset.assetType().c_str())
            , StringConvert(assetType().c_str())
        );
        return false;
    }
#endif


private:
    Name m_assetType = NAME_NONE;
};

template<typename AssetT>
class AssetCodec : public IAssetCodec{
protected:
    AssetCodec()
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

