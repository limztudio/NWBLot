// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AssetBuildServices{
    Alloc::ThreadPool& threadPool;

    explicit AssetBuildServices(Alloc::ThreadPool& threadPool)
        : threadPool(threadPool)
    {}
};

struct AssetBuildRoot{
    AssetString path;
    ACompactString virtualRoot;

    explicit AssetBuildRoot(AssetArena& arena)
        : path(arena)
    {}

    AssetBuildRoot(AssetArena& arena, AStringView inPath, const ACompactString& inVirtualRoot)
        : path(inPath, arena)
        , virtualRoot(inVirtualRoot)
    {}
};

struct AssetBuildOptions{
    AssetString repoRoot;
    AssetVector<AssetBuildRoot> assetRoots;
    AssetString outputDirectory;
    AssetString cacheDirectory;
    ACompactString configuration;
    ACompactString assetType;
    AssetBuildServices services;
    AssetVector<AssetString> inputs;
    bool useExplicitInputs = false;

    explicit AssetBuildOptions(AssetArena& arena, Alloc::ThreadPool& threadPool)
        : repoRoot(arena)
        , assetRoots(arena)
        , outputDirectory(arena)
        , cacheDirectory(arena)
        , services(threadPool)
        , inputs(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

