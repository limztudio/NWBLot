// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <impl/assets_subsystem/asset.h>
#include <impl/ecs_scene/components.h>

#include <core/assets/auto_registration.h>
#include <core/assets/manager.h>
#include <core/assets/module.h>
#include <core/assets/ref.h>
#include <core/ecs/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class EntityAsset;
class ProjectAsset;
class SceneAsset;


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
    [[nodiscard]] virtual bool loadBinary(Span<const u8> binary) = 0;
    [[nodiscard]] virtual bool loadComponentPayload(Span<const u8> payload) = 0;
    [[nodiscard]] virtual bool validatePayload()const = 0;
    virtual void applyToEntity(Core::ECS::Entity& entity)const = 0;
#if defined(NWB_COOK)
    [[nodiscard]] virtual bool serializeComponentPayload(Core::Assets::AssetBytes& outPayload)const = 0;
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ComponentAssetRuntime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool LoadBinary(IComponent& component, Span<const u8> binary);


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
    [[nodiscard]] virtual bool loadBinary(const Span<const u8> binary)override{
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


[[nodiscard]] bool IsComponentAssetType(const Name& assetType)noexcept;
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
    virtual bool serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const override;
#endif
};


#if defined(NWB_COOK)
namespace ComponentAssetSerialization{
[[nodiscard]] bool Serialize(const IComponent& component, Core::Assets::AssetBytes& outBinary);
};
#endif


template<typename ComponentT>
class TypedComponentAssetCodec final : public Core::Assets::AssetCodec<ComponentT>{
public:
    TypedComponentAssetCodec()noexcept(noexcept(DeclVal<void(*)(const Name&)noexcept>()(ComponentT::AssetTypeName()))) = default;


#if defined(NWB_COOK)
public:
    virtual bool serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const override{
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


class TransformComponentAsset final : public TypedComponent<TransformComponentAsset>{
public:
    NWB_DEFINE_ASSET_TYPE("transform_component")


public:
    Scene::TransformComponent component;


public:
    using TypedComponent<TransformComponentAsset>::TypedComponent;


public:
    [[nodiscard]] virtual bool loadComponentPayload(Span<const u8> payload)override;
    [[nodiscard]] virtual bool validatePayload()const override;
    virtual void applyToEntity(Core::ECS::Entity& entity)const override;
#if defined(NWB_COOK)
    [[nodiscard]] virtual bool serializeComponentPayload(Core::Assets::AssetBytes& outPayload)const override;
#endif
};

class CameraComponentAsset final : public TypedComponent<CameraComponentAsset>{
public:
    NWB_DEFINE_ASSET_TYPE("camera_component")


public:
    Scene::CameraComponent component;


public:
    using TypedComponent<CameraComponentAsset>::TypedComponent;


public:
    [[nodiscard]] virtual bool loadComponentPayload(Span<const u8> payload)override;
    [[nodiscard]] virtual bool validatePayload()const override;
    virtual void applyToEntity(Core::ECS::Entity& entity)const override;
#if defined(NWB_COOK)
    [[nodiscard]] virtual bool serializeComponentPayload(Core::Assets::AssetBytes& outPayload)const override;
#endif
};

class LightComponentAsset final : public TypedComponent<LightComponentAsset>{
public:
    NWB_DEFINE_ASSET_TYPE("light_component")


public:
    Scene::LightComponent component;


public:
    using TypedComponent<LightComponentAsset>::TypedComponent;


public:
    [[nodiscard]] virtual bool loadComponentPayload(Span<const u8> payload)override;
    [[nodiscard]] virtual bool validatePayload()const override;
    virtual void applyToEntity(Core::ECS::Entity& entity)const override;
#if defined(NWB_COOK)
    [[nodiscard]] virtual bool serializeComponentPayload(Core::Assets::AssetBytes& outPayload)const override;
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class EntityAsset final : public Core::Assets::TypedAsset<EntityAsset>{
public:
    using ComponentRef = Core::Assets::AssetRef<IComponent>;
    using ComponentRefVector = Core::Assets::AssetVector<ComponentRef>;


public:
    NWB_DEFINE_ASSET_TYPE("entity")


public:
    explicit EntityAsset(Core::Assets::AssetArena& arena)noexcept
        : m_components(arena)
    {}
    EntityAsset(Core::Assets::AssetArena& arena, const Name& virtualPath)noexcept
        : Core::Assets::TypedAsset<EntityAsset>(virtualPath)
        , m_components(arena)
    {}


public:
    [[nodiscard]] bool validatePayload(Core::Alloc::ScratchArena& scratchArena)const;
    bool loadBinary(Span<const u8> binary);

    void setComponents(ComponentRefVector&& components)noexcept{ m_components = Move(components); }
    [[nodiscard]] const ComponentRefVector& components()const noexcept{ return m_components; }


private:
    ComponentRefVector m_components;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_ASSET_CODEC(EntityAssetCodec, EntityAsset);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ProjectAssetDefaults{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr AStringView s_DefaultProjectVirtualPathText = "project/project";
inline constexpr Name s_DefaultProjectVirtualPath(s_DefaultProjectVirtualPathText);
inline constexpr Core::Assets::AssetRef<ProjectAsset> s_DefaultProjectRef(s_DefaultProjectVirtualPathText);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ProjectAsset final : public Core::Assets::TypedAsset<ProjectAsset>{
public:
    using SceneRef = Core::Assets::AssetRef<SceneAsset>;


public:
    NWB_DEFINE_ASSET_TYPE("project")


public:
    explicit ProjectAsset(Core::Assets::AssetArena&)noexcept{}
    ProjectAsset(Core::Assets::AssetArena&, const Name& virtualPath)noexcept
        : Core::Assets::TypedAsset<ProjectAsset>(virtualPath)
    {}


public:
    [[nodiscard]] bool validatePayload()const;
    bool loadBinary(Span<const u8> binary);

    void setStartScene(const SceneRef& startScene)noexcept{ m_startScene = startScene; }
    [[nodiscard]] const SceneRef& startScene()const noexcept{ return m_startScene; }


private:
    SceneRef m_startScene;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_ASSET_CODEC(ProjectAssetCodec, ProjectAsset);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SceneAsset final : public Core::Assets::TypedAsset<SceneAsset>{
public:
    using SubsystemRef = Core::Assets::AssetRef<ISubsystem>;
    using EntityRef = Core::Assets::AssetRef<EntityAsset>;
    using AssetRef = Core::Assets::AssetRef<Core::Assets::IAsset>;
    using SubsystemRefVector = Core::Assets::AssetVector<SubsystemRef>;
    using EntityRefVector = Core::Assets::AssetVector<EntityRef>;
    using AssetMap = HashMap<Name, AssetRef, Core::Assets::AssetArena, Hasher<Name>, EqualTo<Name>>;


public:
    NWB_DEFINE_ASSET_TYPE("scene")


public:
    explicit SceneAsset(Core::Assets::AssetArena& arena)noexcept
        : m_subsystems(arena)
        , m_entities(arena)
        , m_assets(0, Hasher<Name>(), EqualTo<Name>(), arena)
    {}
    SceneAsset(Core::Assets::AssetArena& arena, const Name& virtualPath)noexcept
        : Core::Assets::TypedAsset<SceneAsset>(virtualPath)
        , m_subsystems(arena)
        , m_entities(arena)
        , m_assets(0, Hasher<Name>(), EqualTo<Name>(), arena)
    {}


public:
    [[nodiscard]] bool validatePayload(Core::Alloc::ScratchArena& scratchArena)const;
    bool loadBinary(Span<const u8> binary);

    void setContents(SubsystemRefVector&& subsystems, AssetMap&& assets)noexcept;
    void setContents(SubsystemRefVector&& subsystems, AssetMap&& assets, EntityRefVector&& entities)noexcept;

    [[nodiscard]] const SubsystemRefVector& subsystems()const noexcept{ return m_subsystems; }
    [[nodiscard]] const EntityRefVector& entities()const noexcept{ return m_entities; }
    [[nodiscard]] const AssetMap& assets()const noexcept{ return m_assets; }


private:
    SubsystemRefVector m_subsystems;
    EntityRefVector m_entities;
    AssetMap m_assets;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_ASSET_CODEC(SceneAssetCodec, SceneAsset);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ActiveSceneSubsystems final{
public:
    using ProjectRef = Core::Assets::AssetRef<ProjectAsset>;
    using SceneRef = Core::Assets::AssetRef<SceneAsset>;
    using SubsystemRef = Core::Assets::AssetRef<ISubsystem>;
    using EntityRef = Core::Assets::AssetRef<EntityAsset>;
    using ComponentRef = Core::Assets::AssetRef<IComponent>;
    using AssetRef = Core::Assets::AssetRef<Core::Assets::IAsset>;
    using AssetMap = HashMap<Name, AssetRef, Core::Alloc::GlobalArena, Hasher<Name>, EqualTo<Name>>;
    using LoadedSubsystemVector = Vector<UniquePtr<ISubsystem>, Core::Alloc::GlobalArena>;
    using LoadedComponentVector = Vector<UniquePtr<IComponent>, Core::Alloc::GlobalArena>;

    struct LoadedEntity{
        EntityRef entity;
        LoadedComponentVector components;


    public:
        explicit LoadedEntity(Core::Alloc::GlobalArena& arena)noexcept
            : components(arena)
        {}
        LoadedEntity(const EntityRef& inEntity, LoadedComponentVector&& inComponents)noexcept
            : entity(inEntity)
            , components(Move(inComponents))
        {}
    };
    using LoadedEntityVector = Vector<LoadedEntity, Core::Alloc::GlobalArena>;


public:
    explicit ActiveSceneSubsystems(Core::Alloc::GlobalArena& arena)noexcept
        : m_arena(arena)
        , m_sceneAssets(0, Hasher<Name>(), EqualTo<Name>(), arena)
        , m_loadedSubsystems(arena)
        , m_loadedEntities(arena)
    {}


public:
    [[nodiscard]] bool loadProjectStartScene(Core::Assets::AssetManager& assetManager, const ProjectRef& project = ProjectAssetDefaults::s_DefaultProjectRef);
    [[nodiscard]] bool loadScene(Core::Assets::AssetManager& assetManager, const SceneRef& scene);
    [[nodiscard]] bool instantiateEntities(Core::ECS::World& world)const;

    [[nodiscard]] bool valid()const noexcept{ return m_scene.valid() && !m_loadedSubsystems.empty(); }
    [[nodiscard]] const SceneRef& scene()const noexcept{ return m_scene; }
    [[nodiscard]] const LoadedSubsystemVector& subsystems()const noexcept{ return m_loadedSubsystems; }
    [[nodiscard]] usize subsystemCount()const noexcept{ return m_loadedSubsystems.size(); }
    [[nodiscard]] const LoadedEntityVector& entities()const noexcept{ return m_loadedEntities; }

    template<typename SubsystemT>
    [[nodiscard]] const SubsystemT* findSubsystem()const noexcept{
        for(const UniquePtr<ISubsystem>& loaded : m_loadedSubsystems){
            if(loaded && loaded->assetType() == SubsystemT::AssetTypeName())
                return checked_cast<const SubsystemT*>(loaded.get());
        }
        return nullptr;
    }

    template<typename SubsystemT>
    [[nodiscard]] bool hasSubsystem()const noexcept{
        return findSubsystem<SubsystemT>() != nullptr;
    }

    template<typename TAsset = Core::Assets::IAsset>
    [[nodiscard]] Core::Assets::AssetRef<TAsset> findSceneAsset(const Name& nickname)const noexcept{
        if(!nickname)
            return {};
        const auto found = m_sceneAssets.find(nickname);
        if(found == m_sceneAssets.end())
            return {};
        Core::Assets::AssetRef<TAsset> result;
        result.virtualPath = found.value().name();
        return result;
    }


private:
    Core::Alloc::GlobalArena& m_arena;
    SceneRef m_scene;
    AssetMap m_sceneAssets;
    LoadedSubsystemVector m_loadedSubsystems;
    LoadedEntityVector m_loadedEntities;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

