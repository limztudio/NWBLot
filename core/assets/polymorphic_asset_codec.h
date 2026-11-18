// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/assets/auto_registration.h>
#include <core/assets/binary_payload_io.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class PolymorphicAssetCodec final{
public:
    template<typename BaseAssetT, typename HeaderT, NameHash HeaderT::* TypeHashMember>
    [[nodiscard]] static Expected<UniquePtr<Core::Assets::IAsset>> DeserializePolymorphicAsset(
        const Name& virtualPath,
        const Span<const u8> binary,
        const u32 expectedMagic,
        const TStringView loadContext,
        const TStringView assetKindLabel,
        const TStringView deserializeContext,
        Core::Assets::AssetArena& arena
    ){
        usize cursor = 0u;
        const auto headerResult = Core::Assets::ReadMagicHeaderPayload<HeaderT>(binary, expectedMagic, loadContext, assetKindLabel, cursor);
        if(!headerResult)
            return MakeUnexpected(Failure{});

        auto asset = Core::Assets::PolymorphicAssetTypeAutoRegistrar<BaseAssetT>::CreateByTypeHash(virtualPath, (*headerResult).*TypeHashMember, arena);
        if(!asset){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("{} failed: unregistered {} type in '{}'"), deserializeContext, assetKindLabel, ConvertString(virtualPath.resolvedText()));
            return MakeUnexpected(Failure{});
        }
        if(!asset->loadBinary(binary))
            return MakeUnexpected(Failure{});

        return UniquePtr<Core::Assets::IAsset>(Move(asset));
    }

#if defined(NWB_COOK)
    template<typename BaseAssetT, b8(*SerializeFunc)(const BaseAssetT&, Core::Assets::AssetBytes&)>
    [[nodiscard]] static b8 SerializePolymorphicAsset(
        const Core::Assets::IAsset& asset,
        const TStringView serializeContext,
        Core::Assets::AssetBytes& outBinary
    ){
        const BaseAssetT* const typedAsset = Core::Assets::PolymorphicAssetTypeAutoRegistrar<BaseAssetT>::Cast(&asset);
        if(!typedAsset){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("{} failed: invalid asset type '{}'"), serializeContext, ConvertString(asset.assetType().resolvedText()));
            return false;
        }

        return SerializeFunc(*typedAsset, outBinary);
    }
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename FamilyT, typename PolicyT>
class FamilyAssetCodec final : public IAssetCodec{
public:
    FamilyAssetCodec()noexcept : IAssetCodec(FamilyT::AssetTypeName()){}

    [[nodiscard]] Expected<UniquePtr<IAsset>> deserialize(const Name& path, const Span<const u8> binary, AssetArena& arena)const override{
        const PolicyT policy;
        const auto type = policy.readType(binary);
        if(!type)
            return MakeUnexpected(Failure{});
        auto object = PolymorphicAssetTypeAutoRegistrar<FamilyT>::CreateByTypeHash(path, *type, arena);
        if(!object){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("{} failed: unregistered {} type in '{}'"), ConvertString(policy.deserializeContext()), ConvertString(FamilyT::s_AssetTypeText), ConvertString(path.resolvedText()));
            return MakeUnexpected(Failure{});
        }
        if(!object->loadBinary(binary))
            return MakeUnexpected(Failure{});
        return UniquePtr<IAsset>(Move(object));
    }

#if defined(NWB_COOK)
    b8 serialize(const IAsset& asset, AssetBytes& bytes)const override{
        const PolicyT policy;
        const FamilyT* object = PolymorphicAssetTypeAutoRegistrar<FamilyT>::Cast(&asset);
        if(!object){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("{} failed: invalid asset type '{}'"), ConvertString(policy.serializeContext()), ConvertString(asset.assetType().resolvedText()));
            return false;
        }
        return policy.write(*object, bytes);
    }
#endif
};

template<typename FamilyT, typename AssetT, typename PolicyT>
class FamilyMemberAssetCodec final : public AssetCodec<AssetT>{
public:
    FamilyMemberAssetCodec()noexcept(noexcept(DeclVal<void(*)(const Name&)noexcept>()(AssetT::AssetTypeName()))) = default;

#if defined(NWB_COOK)
    b8 serialize(const IAsset& asset, AssetBytes& bytes)const override{
        const PolicyT policy;
        if(!this->checkSerializeAssetType(asset, policy.memberSerializeContext()))
            return false;
        return policy.write(static_cast<const FamilyT&>(*MakeNotNull(checked_cast<const AssetT*>(&asset))), bytes);
    }
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

