// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "registry.h"

#include <core/common/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using AssetCodecFactory = UniquePtr<IAssetCodec>(*)();


template<typename AssetCodecT>
[[nodiscard]] inline UniquePtr<IAssetCodec> CreateAssetCodec(){
    return MakeUnique<AssetCodecT>();
}

class AssetCodecAutoRegistrar final : public Core::Common::Initializerable{
public:
    explicit AssetCodecAutoRegistrar(AssetCodecFactory factory)
        : m_factory(factory)
    {}


public:
    virtual b8 initialize()override;
    virtual void finalize()noexcept override{}


private:
    AssetCodecFactory m_factory = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename BaseAssetT>
using PolymorphicAssetInstanceFactory = UniquePtr<BaseAssetT>(*)(const Name& virtualPath, AssetArena& arena);

template<typename BaseAssetT>
class PolymorphicAssetTypeAutoRegistrar final : NoCopy{
public:
    using InstanceFactory = PolymorphicAssetInstanceFactory<BaseAssetT>;


public:
    [[nodiscard]] static UniquePtr<BaseAssetT> CreateByTypeHash(const Name& virtualPath, const NameHash& typeHash, AssetArena& arena){
        for(const PolymorphicAssetTypeAutoRegistrar* node = s_Head; node != nullptr; node = node->m_next){
            if(node->m_assetType.hash() == typeHash && node->m_create != nullptr)
                return node->m_create(virtualPath, arena);
        }
        return nullptr;
    }

    [[nodiscard]] static b8 IsRegisteredAssetType(const Name& assetType)noexcept{
        if(!assetType)
            return false;
        for(const PolymorphicAssetTypeAutoRegistrar* node = s_Head; node != nullptr; node = node->m_next){
            if(node->m_assetType == assetType)
                return true;
        }
        return false;
    }

    [[nodiscard]] static const BaseAssetT* Cast(const IAsset* const asset)noexcept{
        if(!asset || !IsRegisteredAssetType(asset->assetType()))
            return nullptr;
        return checked_cast<const BaseAssetT*>(asset);
    }


public:
    PolymorphicAssetTypeAutoRegistrar(const Name& assetType, const InstanceFactory create)noexcept
        : m_assetType(assetType)
        , m_create(create)
        , m_next(s_Head)
    {
        s_Head = this;
    }


public:
    [[nodiscard]] const Name& assetType()const noexcept{ return m_assetType; }
    [[nodiscard]] InstanceFactory create()const noexcept{ return m_create; }
    [[nodiscard]] const PolymorphicAssetTypeAutoRegistrar* next()const noexcept{ return m_next; }


private:
    inline static const PolymorphicAssetTypeAutoRegistrar* s_Head = nullptr;

    Name m_assetType = s_NameNone;
    InstanceFactory m_create = nullptr;
    const PolymorphicAssetTypeAutoRegistrar* m_next = nullptr;
};

template<typename BaseAssetT, typename ConcreteAssetT, typename CodecT>
class TypedPolymorphicAssetAutoRegistrar final : NoCopy{
private:
    [[nodiscard]] static UniquePtr<BaseAssetT> CreateInstance(const Name& virtualPath, AssetArena& arena){
        return MakeUnique<ConcreteAssetT>(arena, virtualPath);
    }


public:
    TypedPolymorphicAssetAutoRegistrar()noexcept
        : m_typeRegistrar(ConcreteAssetT::AssetTypeName(), &CreateInstance)
        , m_codecRegistrar(&CreateAssetCodec<CodecT>)
    {}


private:
    PolymorphicAssetTypeAutoRegistrar<BaseAssetT> m_typeRegistrar;
    AssetCodecAutoRegistrar m_codecRegistrar;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RegisterAutoCollectedAssetCodecs(AssetRegistry& outRegistry);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

