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


struct ProjectCookEntry{
    Name virtualPath = s_NameNone;
    ProjectAsset::SceneRef startScene;


public:
    explicit ProjectCookEntry(Core::Assets::AssetArena&)noexcept{}
};

struct SceneCookEntry{
    Name virtualPath = s_NameNone;
    SceneAsset::SubsystemRefVector subsystems;
    SceneAsset::NameVector enabledFeatures;
    SceneAsset::AssetRefVector assets;


public:
    explicit SceneCookEntry(Core::Assets::AssetArena& arena)noexcept
        : subsystems(arena)
        , enabledFeatures(arena)
        , assets(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<ProjectCookEntry> ParseProjectCookMetadata(
    const Path& assetRoot,
    AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
);
[[nodiscard]] Expected<ProjectCookEntry> ParseProjectCookMetadata(
    Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
);
[[nodiscard]] Expected<ProjectAsset> BuildProjectAsset(
    ProjectCookEntry& entry,
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
[[nodiscard]] Expected<SceneCookEntry> ParseSceneCookMetadata(
    Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
);
[[nodiscard]] Expected<SceneAsset> BuildSceneAsset(
    SceneCookEntry& entry,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

