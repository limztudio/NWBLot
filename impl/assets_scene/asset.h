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

public:
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
    using SubsystemRef = Core::Assets::AssetRef<Subsystem>;
    using AssetRef = Core::Assets::AssetRef<Core::Assets::IAsset>;
    using SubsystemRefVector = Core::Assets::AssetVector<SubsystemRef>;
    using NameVector = Core::Assets::AssetVector<Name>;
    using AssetRefVector = Core::Assets::AssetVector<AssetRef>;


public:
    NWB_DEFINE_ASSET_TYPE("scene")


public:
    explicit SceneAsset(Core::Assets::AssetArena& arena)noexcept
        : m_subsystems(arena)
        , m_enabledFeatures(arena)
        , m_assets(arena)
    {}
    SceneAsset(Core::Assets::AssetArena& arena, const Name& virtualPath)noexcept
        : Core::Assets::TypedAsset<SceneAsset>(virtualPath)
        , m_subsystems(arena)
        , m_enabledFeatures(arena)
        , m_assets(arena)
    {}


public:
    [[nodiscard]] bool validatePayload(Core::Alloc::ScratchArena& scratchArena)const;
    bool loadBinary(Span<const u8> binary);

public:
    void setContents(SubsystemRefVector&& subsystems, NameVector&& enabledFeatures, AssetRefVector&& assets)noexcept{
        m_subsystems = Move(subsystems);
        m_enabledFeatures = Move(enabledFeatures);
        m_assets = Move(assets);
    }

public:
    [[nodiscard]] const SubsystemRefVector& subsystems()const noexcept{ return m_subsystems; }
    [[nodiscard]] const NameVector& enabledFeatures()const noexcept{ return m_enabledFeatures; }
    [[nodiscard]] const AssetRefVector& assets()const noexcept{ return m_assets; }

    [[nodiscard]] bool hasSubsystem(const SubsystemRef& subsystem)const noexcept{
        if(!subsystem.valid())
            return false;
        for(const SubsystemRef& entry : m_subsystems){
            if(entry == subsystem)
                return true;
        }
        return false;
    }

    [[nodiscard]] bool hasEnabledFeature(const Name& featureName)const noexcept{
        if(!featureName)
            return false;
        for(const Name& entry : m_enabledFeatures){
            if(entry == featureName)
                return true;
        }
        return false;
    }


private:
    SubsystemRefVector m_subsystems;
    NameVector m_enabledFeatures;
    AssetRefVector m_assets;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_ASSET_CODEC(SceneAssetCodec, SceneAsset);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ActiveSceneSubsystems final{
public:
    using ProjectRef = Core::Assets::AssetRef<ProjectAsset>;
    using SceneRef = Core::Assets::AssetRef<SceneAsset>;
    using SubsystemRef = Core::Assets::AssetRef<Subsystem>;
    using AssetRef = Core::Assets::AssetRef<Core::Assets::IAsset>;
    using SubsystemRefVector = Vector<SubsystemRef, Core::Alloc::GlobalArena>;
    using NameVector = Vector<Name, Core::Alloc::GlobalArena>;
    using AssetRefVector = Vector<AssetRef, Core::Alloc::GlobalArena>;


public:
    explicit ActiveSceneSubsystems(Core::Alloc::GlobalArena& arena)noexcept
        : m_subsystems(arena)
        , m_enabledFeatures(arena)
        , m_resolvedAssets(arena)
    {}


public:
    [[nodiscard]] bool loadProjectStartScene(Core::Assets::AssetManager& assetManager, const ProjectRef& project = ProjectAssetDefaults::s_DefaultProjectRef);
    [[nodiscard]] bool loadScene(Core::Assets::AssetManager& assetManager, const SceneRef& scene);

    void enableFeature(const Name& featureName);

    [[nodiscard]] bool valid()const noexcept{ return m_scene.valid() && !m_subsystems.empty(); }
    [[nodiscard]] const ProjectRef& project()const noexcept{ return m_project; }
    [[nodiscard]] const SceneRef& scene()const noexcept{ return m_scene; }
    [[nodiscard]] const SubsystemRefVector& subsystems()const noexcept{ return m_subsystems; }
    [[nodiscard]] const NameVector& enabledFeatures()const noexcept{ return m_enabledFeatures; }
    [[nodiscard]] const AssetRefVector& resolvedAssets()const noexcept{ return m_resolvedAssets; }

    [[nodiscard]] bool hasSubsystem(const SubsystemRef& subsystem)const noexcept{
        if(!subsystem.valid())
            return false;
        for(const SubsystemRef& enabled : m_subsystems){
            if(enabled == subsystem)
                return true;
        }
        return false;
    }

    [[nodiscard]] bool isFeatureEnabled(const Name& featureName)const noexcept{
        if(!featureName)
            return false;
        for(const Name& enabled : m_enabledFeatures){
            if(enabled == featureName)
                return true;
        }
        return false;
    }


private:
    ProjectRef m_project;
    SceneRef m_scene;
    SubsystemRefVector m_subsystems;
    NameVector m_enabledFeatures;
    AssetRefVector m_resolvedAssets;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

