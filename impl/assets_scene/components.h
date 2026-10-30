// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <impl/ecs_scene/components.adhd.h>

#include <core/assets/auto_registration.h>
#include <core/assets/module.h>
#include <core/common/log.h>
#include <core/ecs/module.h>

#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


interface IComponent : Core::Assets::IAsset{
public:
    NWB_DEFINE_ASSET_TYPE("component")


public:
    virtual ~IComponent()noexcept override = 0;
protected:
    explicit IComponent(const Name& assetType, const Name& virtualPath = s_NameNone)noexcept
        : Core::Assets::IAsset(assetType, virtualPath)
    {}
    IComponent(const IComponent&) = default;
    IComponent(IComponent&&)noexcept = default;

protected:
    IComponent& operator=(const IComponent&) = default;
    IComponent& operator=(IComponent&&)noexcept = default;


public:
    [[nodiscard]] virtual b8 loadBinary(Span<const u8> binary) = 0;
    [[nodiscard]] virtual b8 loadComponentPayload(Span<const u8> payload) = 0;
    [[nodiscard]] virtual b8 validatePayload()const = 0;
    virtual void applyToEntity(Core::ECS::Entity& entity)const = 0;
#if defined(NWB_COOK)
    [[nodiscard]] virtual b8 serializeComponentPayload(Core::Assets::AssetBytes& outPayload)const = 0;
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ComponentAssetRuntime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 LoadBinary(IComponent& component, Span<const u8> binary);

template<typename TPod>
[[nodiscard]] Expected<TPod> ReadSinglePodPayload(const Span<const u8> payload, const TStringView contextLabel){
    usize cursor = 0u;
    const auto binaryPayload = ReadPOD<TPod>(payload, cursor);
    if(!binaryPayload || cursor != payload.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: invalid payload size"), contextLabel);
        return MakeUnexpected(Failure{});
    }
    return binaryPayload;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ComponentT>
class TypedComponent : public IComponent{
public:
    explicit TypedComponent(Core::Assets::AssetArena&)noexcept(noexcept(DeclVal<void(*)(const Name&)noexcept>()(ComponentT::AssetTypeName())))
        : IComponent(ComponentT::AssetTypeName())
    {}
    TypedComponent(Core::Assets::AssetArena&, const Name& virtualPath)noexcept(noexcept(DeclVal<void(*)(const Name&)noexcept>()(ComponentT::AssetTypeName())))
        : IComponent(ComponentT::AssetTypeName(), virtualPath)
    {}
    TypedComponent(const TypedComponent&) = default;
    TypedComponent(TypedComponent&&)noexcept = default;
    virtual ~TypedComponent()noexcept override = 0;


public:
    TypedComponent& operator=(const TypedComponent&) = default;
    TypedComponent& operator=(TypedComponent&&)noexcept = default;


public:
    [[nodiscard]] virtual b8 loadBinary(const Span<const u8> binary)override{
        return ComponentAssetRuntime::LoadBinary(*this, binary);
    }
};

template<typename ComponentT>
TypedComponent<ComponentT>::~TypedComponent()noexcept = default;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ComponentInstanceFactory = UniquePtr<IComponent>(*)(Core::Assets::AssetArena& arena, const Name& virtualPath);

class ComponentTypeAutoRegistrar final : NoCopy{
public:
    ComponentTypeAutoRegistrar(const Name& assetType, ComponentInstanceFactory create)noexcept;


public:
    [[nodiscard]] const Name& assetType()const noexcept{ return m_assetType; }
    [[nodiscard]] ComponentInstanceFactory create()const noexcept{ return m_create; }
    [[nodiscard]] const ComponentTypeAutoRegistrar* next()const noexcept{ return m_next; }


private:
    Name m_assetType = s_NameNone;
    ComponentInstanceFactory m_create = nullptr;
    const ComponentTypeAutoRegistrar* m_next = nullptr;
};

[[nodiscard]] const IComponent* CastComponent(const Core::Assets::IAsset* asset)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ComponentAssetCodec final : public Core::Assets::IAssetCodec{
public:
    ComponentAssetCodec()noexcept
        : Core::Assets::IAssetCodec(IComponent::AssetTypeName())
    {}


public:
    [[nodiscard]] virtual Expected<UniquePtr<Core::Assets::IAsset>> deserialize(Core::Assets::AssetArena& arena, const Name& virtualPath, Span<const u8> binary)const override;
#if defined(NWB_COOK)
    virtual b8 serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const override;
#endif
};

#if defined(NWB_COOK)
namespace ComponentAssetSerialization{
[[nodiscard]] b8 Serialize(const IComponent& component, Core::Assets::AssetBytes& outBinary);

template<typename TPod>
[[nodiscard]] b8 WriteSinglePodPayload(const TPod& pod, Core::Assets::AssetBytes& outBytes){
    outBytes.clear();
    outBytes.reserve(sizeof(pod));
    AppendPOD(outBytes, pod);
    return true;
}
};
#endif

template<typename ComponentT>
class TypedComponentAssetCodec final : public Core::Assets::AssetCodec<ComponentT>{
public:
    TypedComponentAssetCodec()noexcept(noexcept(DeclVal<void(*)(const Name&)noexcept>()(ComponentT::AssetTypeName()))) = default;

#if defined(NWB_COOK)
public:
    virtual b8 serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const override{
        if(!this->checkSerializeAssetType(asset, NWB_TEXT("TypedComponentAssetCodec::serialize")))
            return false;

        return ComponentAssetSerialization::Serialize(*MakeNotNull(checked_cast<const ComponentT*>(&asset)), outBinary);
    }
#endif
};

template<typename ComponentT>
class TypedComponentAutoRegistrar final : NoCopy{
private:
    [[nodiscard]] static UniquePtr<IComponent> CreateInstance(Core::Assets::AssetArena& arena, const Name& virtualPath){
        return MakeUnique<ComponentT>(arena, virtualPath);
    }


public:
    TypedComponentAutoRegistrar()noexcept
        : m_typeRegistrar(ComponentT::AssetTypeName(), &CreateInstance)
        , m_codecRegistrar(&Core::Assets::CreateAssetCodec<TypedComponentAssetCodec<ComponentT>>)
    {}


private:
    ComponentTypeAutoRegistrar m_typeRegistrar;
    Core::Assets::AssetCodecAutoRegistrar m_codecRegistrar;
};

#define NWB_DEFINE_COMPONENT_ASSET_REGISTRAR(registrarVariable, componentType) \
    TypedComponentAutoRegistrar<componentType> registrarVariable


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using Scene::TransformComponent;
using Scene::CameraComponent;
using Scene::LightComponent;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

