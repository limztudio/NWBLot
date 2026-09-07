// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <core/assets/global.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSET_BUILDER_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AssetBuildServices{
    Core::Alloc::ThreadPool& threadPool;

    explicit AssetBuildServices(Core::Alloc::ThreadPool& threadPool)
        : threadPool(threadPool)
    {}
};

struct AssetBuildRoot{
    Core::Assets::AssetString path;
    ACompactString virtualRoot;

    explicit AssetBuildRoot(Core::Assets::AssetArena& arena)
        : path(arena)
    {}

    AssetBuildRoot(Core::Assets::AssetArena& arena, AStringView inPath, const ACompactString& inVirtualRoot)
        : path(inPath, arena)
        , virtualRoot(inVirtualRoot)
    {}
};

struct AssetBuildOptions{
    Core::Assets::AssetString repoRoot;
    Core::Assets::AssetVector<AssetBuildRoot> assetRoots;
    Core::Assets::AssetString outputDirectory;
    Core::Assets::AssetString cacheDirectory;
    ACompactString configuration;
    ACompactString assetType;
    AssetBuildServices services;
    Core::Assets::AssetVector<Core::Assets::AssetString> inputs;
    bool useExplicitInputs = false;

    explicit AssetBuildOptions(Core::Assets::AssetArena& arena, Core::Alloc::ThreadPool& threadPool)
        : repoRoot(arena)
        , assetRoots(arena)
        , outputDirectory(arena)
        , cacheDirectory(arena)
        , services(threadPool)
        , inputs(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSET_BUILDER_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

