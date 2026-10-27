// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "asset.h"

#include <core/alloc/scratch.h>
#include <core/metascript/parser.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct TransformComponentCookEntry{
    Name virtualPath = s_NameNone;
    Scene::TransformComponent component;


public:
    explicit TransformComponentCookEntry(Core::Assets::AssetArena&)noexcept{}
};

struct CameraComponentCookEntry{
    Name virtualPath = s_NameNone;
    Scene::CameraComponent component;


public:
    explicit CameraComponentCookEntry(Core::Assets::AssetArena&)noexcept{}
};

struct LightComponentCookEntry{
    Name virtualPath = s_NameNone;
    Scene::LightComponent component;


public:
    explicit LightComponentCookEntry(Core::Assets::AssetArena&)noexcept{}
};

struct EntityCookEntry{
    Name virtualPath = s_NameNone;
    EntityAsset::ComponentRefVector components;


public:
    explicit EntityCookEntry(Core::Assets::AssetArena& arena)noexcept
        : components(arena)
    {}
};

struct ProjectCookEntry{
    Name virtualPath = s_NameNone;
    ProjectAsset::SceneRef startScene;


public:
    explicit ProjectCookEntry(Core::Assets::AssetArena&)noexcept{}
};

struct SceneCookEntry{
    Name virtualPath = s_NameNone;
    SceneAsset::SubsystemRefVector subsystems;
    SceneAsset::EntityRefVector entities;
    SceneAsset::AssetMap assets;


public:
    explicit SceneCookEntry(Core::Assets::AssetArena& arena)noexcept
        : subsystems(arena)
        , entities(arena)
        , assets(0, Hasher<Name>(), EqualTo<Name>(), arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<TransformComponentCookEntry> ParseTransformComponentCookMetadata(
    Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
);
[[nodiscard]] Expected<TransformComponentCookEntry> ParseTransformComponentCookMetadata(
    const Path& assetRoot,
    AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
);
[[nodiscard]] Expected<TransformComponentAsset> BuildTransformComponentAsset(
    TransformComponentCookEntry& entry,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
);

[[nodiscard]] Expected<CameraComponentCookEntry> ParseCameraComponentCookMetadata(
    Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
);
[[nodiscard]] Expected<CameraComponentCookEntry> ParseCameraComponentCookMetadata(
    const Path& assetRoot,
    AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
);
[[nodiscard]] Expected<CameraComponentAsset> BuildCameraComponentAsset(
    CameraComponentCookEntry& entry,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
);

[[nodiscard]] Expected<LightComponentCookEntry> ParseLightComponentCookMetadata(
    Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
);
[[nodiscard]] Expected<LightComponentCookEntry> ParseLightComponentCookMetadata(
    const Path& assetRoot,
    AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
);
[[nodiscard]] Expected<LightComponentAsset> BuildLightComponentAsset(
    LightComponentCookEntry& entry,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
);

[[nodiscard]] Expected<EntityCookEntry> ParseEntityCookMetadata(
    Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
);
[[nodiscard]] Expected<EntityCookEntry> ParseEntityCookMetadata(
    const Path& assetRoot,
    AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
);
[[nodiscard]] Expected<EntityAsset> BuildEntityAsset(EntityCookEntry& entry, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena);

[[nodiscard]] Expected<ProjectCookEntry> ParseProjectCookMetadata(
    Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
);
[[nodiscard]] Expected<ProjectCookEntry> ParseProjectCookMetadata(
    const Path& assetRoot,
    AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
);
[[nodiscard]] Expected<ProjectAsset> BuildProjectAsset(ProjectCookEntry& entry, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena);

[[nodiscard]] Expected<SceneCookEntry> ParseSceneCookMetadata(
    Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
);
[[nodiscard]] Expected<SceneCookEntry> ParseSceneCookMetadata(
    const Path& assetRoot,
    AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
);
[[nodiscard]] Expected<SceneAsset> BuildSceneAsset(SceneCookEntry& entry, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

