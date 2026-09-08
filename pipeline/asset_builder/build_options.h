// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <core/task/cpu/scheduler.h>

#include <core/assets/global.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSET_BUILDER_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AssetBuildServices{
    Core::CpuTaskScheduler& cpuScheduler;

    explicit AssetBuildServices(Core::CpuTaskScheduler& cpuScheduler)
        : cpuScheduler(cpuScheduler)
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

    explicit AssetBuildOptions(Core::Assets::AssetArena& arena, Core::CpuTaskScheduler& cpuScheduler)
        : repoRoot(arena)
        , assetRoots(arena)
        , outputDirectory(arena)
        , cacheDirectory(arena)
        , services(cpuScheduler)
        , inputs(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSET_BUILDER_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

