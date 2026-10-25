// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <impl/assets_subsystem/asset.h>

#include <core/assets/manager.h>
#include <core/assets/module.h>
#include <core/assets/ref.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ProjectAsset;
class SceneAsset;


namespace ProjectAssetDefaults{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr AStringView s_ProjectFileName = "project.nwb";
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
    using AssetRef = Core::Assets::AssetRef<Core::Assets::IAsset>;
    using SubsystemRefVector = Core::Assets::AssetVector<SubsystemRef>;
    using AssetMap = HashMap<Name, AssetRef, Core::Assets::AssetArena, Hasher<Name>, EqualTo<Name>>;


public:
    NWB_DEFINE_ASSET_TYPE("scene")


public:
    explicit SceneAsset(Core::Assets::AssetArena& arena)noexcept
        : m_subsystems(arena)
        , m_assets(0, Hasher<Name>(), EqualTo<Name>(), arena)
    {}
    SceneAsset(Core::Assets::AssetArena& arena, const Name& virtualPath)noexcept
        : Core::Assets::TypedAsset<SceneAsset>(virtualPath)
        , m_subsystems(arena)
        , m_assets(0, Hasher<Name>(), EqualTo<Name>(), arena)
    {}


public:
    [[nodiscard]] bool validatePayload(Core::Alloc::ScratchArena& scratchArena)const;
    bool loadBinary(Span<const u8> binary);

    void setContents(SubsystemRefVector&& subsystems, AssetMap&& assets)noexcept{
        m_subsystems = Move(subsystems);
        m_assets = Move(assets);
    }

    [[nodiscard]] const SubsystemRefVector& subsystems()const noexcept{ return m_subsystems; }
    [[nodiscard]] const AssetMap& assets()const noexcept{ return m_assets; }


private:
    SubsystemRefVector m_subsystems;
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
    using AssetRef = Core::Assets::AssetRef<Core::Assets::IAsset>;
    using AssetMap = HashMap<Name, AssetRef, Core::Alloc::GlobalArena, Hasher<Name>, EqualTo<Name>>;
    using LoadedSubsystemVector = Vector<UniquePtr<ISubsystem>, Core::Alloc::GlobalArena>;


public:
    explicit ActiveSceneSubsystems(Core::Alloc::GlobalArena& arena)noexcept
        : m_arena(arena)
        , m_sceneAssets(0, Hasher<Name>(), EqualTo<Name>(), arena)
        , m_loadedSubsystems(arena)
    {}


public:
    [[nodiscard]] bool loadProjectStartScene(Core::Assets::AssetManager& assetManager, const ProjectRef& project = ProjectAssetDefaults::s_DefaultProjectRef);
    [[nodiscard]] bool loadScene(Core::Assets::AssetManager& assetManager, const SceneRef& scene);

    [[nodiscard]] bool valid()const noexcept{ return m_scene.valid() && !m_loadedSubsystems.empty(); }
    [[nodiscard]] const ProjectRef& project()const noexcept{ return m_project; }
    [[nodiscard]] const SceneRef& scene()const noexcept{ return m_scene; }
    [[nodiscard]] const AssetMap& sceneAssets()const noexcept{ return m_sceneAssets; }
    [[nodiscard]] const LoadedSubsystemVector& loadedSubsystems()const noexcept{ return m_loadedSubsystems; }

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
    ProjectRef m_project;
    SceneRef m_scene;
    AssetMap m_sceneAssets;
    LoadedSubsystemVector m_loadedSubsystems;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
