// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <core/common/log.h>

#include <global/expected.h>
#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using AssetBytes = AssetVector<u8>;

inline constexpr TStringView s_UnnamedAssetPathText = FOUNDATION_TEXT("<unnamed>");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_DEFINE_ASSET_TYPE(assetTypeLiteral) \
    static constexpr ::AStringView s_AssetTypeText = assetTypeLiteral; \
    inline static constexpr ::Name s_AssetTypeName = ::Name(assetTypeLiteral); \
    [[nodiscard]] static constexpr const ::Name& AssetTypeName()noexcept{ \
        return s_AssetTypeName; \
    }

#define NWB_DEFINE_ASSET_CODEC_REGISTRAR(codecVariable, codecType) \
    ::NWB::Core::Assets::AssetCodecAutoRegistrar codecVariable(&::NWB::Core::Assets::CreateAssetCodec<codecType>)

#if defined(NWB_COOK)
#define NWB_ASSET_CODEC_SERIALIZE_DECL() \
public: \
    virtual ::b8 serialize(const ::NWB::Core::Assets::IAsset& asset, ::NWB::Core::Assets::AssetBytes& outBinary)const override;
#else
#define NWB_ASSET_CODEC_SERIALIZE_DECL()
#endif
#define NWB_DEFINE_ASSET_CODEC(codecType, assetType) \
    class codecType final : public ::NWB::Core::Assets::AssetCodec<assetType>{ \
    public: \
        codecType()noexcept(noexcept(::DeclVal<void(*)(const ::Name&)noexcept>()(assetType::AssetTypeName()))) = default; \
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
    virtual ~IAsset()noexcept = default;


public:
    [[nodiscard]] const Name& assetType()const noexcept{ return m_assetType; }

    [[nodiscard]] const Name& virtualPath()const noexcept{ return m_virtualPath; }


public:
    [[nodiscard]] b8 checkVirtualPath(TStringView failureContext)const;
#if defined(FOUNDATION_UNICODE)
    [[nodiscard]] b8 checkVirtualPath(const AStringView failureContext)const{
        return checkVirtualPath(ConvertString(failureContext));
    }
#endif


private:
    Name m_assetType = s_NameNone;
    Name m_virtualPath = s_NameNone;
};

template<typename ArenaT>
[[nodiscard]] inline TString<ArenaT> AssetVirtualPathText(const IAsset& asset, ArenaT& arena){
    return asset.virtualPath() ? ConvertString(arena, asset.virtualPath().resolvedText()) : TString<ArenaT>(s_UnnamedAssetPathText, arena);
}

template<typename AssetT, typename BaseAssetT = IAsset>
class TypedAsset : public BaseAssetT{
protected:
    TypedAsset()noexcept(noexcept(DeclVal<void(*)(const Name&)noexcept>()(AssetT::AssetTypeName())))
        : BaseAssetT(AssetT::AssetTypeName())
    {}
    explicit TypedAsset(const Name& virtualPath)noexcept(noexcept(DeclVal<void(*)(const Name&)noexcept>()(AssetT::AssetTypeName())))
        : BaseAssetT(AssetT::AssetTypeName(), virtualPath)
    {}
};

template<typename AssetT>
[[nodiscard]] inline const AssetT* CastAsset(const IAsset* asset)noexcept(noexcept((!asset || asset->assetType() != AssetT::AssetTypeName()) ? true : false) && noexcept(checked_cast<const AssetT*>(asset))){
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
    virtual ~IAssetCodec()noexcept = default;


public:
    [[nodiscard]] const Name& assetType()const noexcept{ return m_assetType; }


public:
    [[nodiscard]] virtual Expected<UniquePtr<IAsset>> deserialize(const Name& virtualPath, Span<const u8> binary, AssetArena& arena)const = 0;

#if defined(NWB_COOK)


public:
    virtual b8 serialize(const IAsset& asset, AssetBytes& outBinary)const = 0;


public:
    [[nodiscard]] b8 checkSerializeAssetType(const IAsset& asset, TStringView failureContext)const;
#endif


private:
    Name m_assetType = s_NameNone;
};

template<typename AssetT>
class AssetCodec : public IAssetCodec{
protected:
    AssetCodec()noexcept(noexcept(DeclVal<void(*)(const Name&)noexcept>()(AssetT::AssetTypeName())))
        : IAssetCodec(AssetT::AssetTypeName())
    {}


public:
    [[nodiscard]] virtual Expected<UniquePtr<IAsset>> deserialize(const Name& virtualPath, const Span<const u8> binary, AssetArena& arena)const final override{
        auto asset = MakeUnique<AssetT>(arena, virtualPath);
        if(!asset || !asset->loadBinary(binary))
            return MakeUnexpected(Failure{});

        return UniquePtr<IAsset>(Move(asset));
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

