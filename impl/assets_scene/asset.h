// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "components.h"

#include <impl/assets_scene/component_assets.adhd.h>
#include <impl/assets_subsystem/asset.h>

#include <core/assets/manager.h>
#include <core/assets/ref.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class EntityAsset;
class ProjectAsset;
class SceneAsset;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr TStringView s_ProjectAssetKindLabel = NWB_TEXT("project");
inline constexpr TStringView s_ProjectLoadBinaryContext = NWB_TEXT("ProjectAsset::loadBinary");
inline constexpr TStringView s_ProjectSerializeContext = NWB_TEXT("ProjectAssetCodec::serialize");

inline constexpr TStringView s_ComponentAssetKindLabel = NWB_TEXT("component");
inline constexpr TStringView s_ComponentLoadBinaryContext = NWB_TEXT("IComponent::loadBinary");
inline constexpr TStringView s_ComponentSerializeContext = NWB_TEXT("ComponentAssetCodec::serialize");

inline constexpr TStringView s_EntityAssetKindLabel = NWB_TEXT("entity");
inline constexpr TStringView s_EntityLoadBinaryContext = NWB_TEXT("EntityAsset::loadBinary");
inline constexpr TStringView s_EntitySerializeContext = NWB_TEXT("EntityAssetCodec::serialize");
inline constexpr TStringView s_EntityComponentsPayloadLabel = NWB_TEXT("entity components");

inline constexpr TStringView s_SceneAssetKindLabel = NWB_TEXT("scene");
inline constexpr TStringView s_SceneLoadBinaryContext = NWB_TEXT("SceneAsset::loadBinary");
inline constexpr TStringView s_SceneSerializeContext = NWB_TEXT("SceneAssetCodec::serialize");
inline constexpr TStringView s_SceneSubsystemsPayloadLabel = NWB_TEXT("scene subsystems");
inline constexpr TStringView s_SceneEntitiesPayloadLabel = NWB_TEXT("scene entities");
inline constexpr TStringView s_SceneAssetsPayloadLabel = NWB_TEXT("scene referenced assets");

inline constexpr Core::Assets::AssetRef<ProjectAsset> s_DefaultProjectRef(s_DefaultProjectVirtualPathText);


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
    [[nodiscard]] b8 validatePayload(Core::Alloc::ScratchArena& scratchArena)const;
    b8 loadBinary(Span<const u8> binary);

    void setComponents(ComponentRefVector&& components)noexcept{ m_components = Move(components); }
    [[nodiscard]] const ComponentRefVector& components()const noexcept{ return m_components; }


private:
    ComponentRefVector m_components;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_ASSET_CODEC(EntityAssetCodec, EntityAsset);


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
    [[nodiscard]] b8 validatePayload()const;
    b8 loadBinary(Span<const u8> binary);

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
    [[nodiscard]] b8 validatePayload(Core::Alloc::ScratchArena& scratchArena)const;
    b8 loadBinary(Span<const u8> binary);

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
    [[nodiscard]] b8 loadProjectStartScene(Core::Assets::AssetManager& assetManager, const ProjectRef& project = s_DefaultProjectRef);
    [[nodiscard]] b8 loadScene(Core::Assets::AssetManager& assetManager, const SceneRef& scene);
    [[nodiscard]] b8 instantiateEntities(Core::ECS::World& world)const;

    [[nodiscard]] b8 valid()const noexcept{ return m_scene.valid() && !m_loadedSubsystems.empty(); }
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
    [[nodiscard]] b8 hasSubsystem()const noexcept{
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
