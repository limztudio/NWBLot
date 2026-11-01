// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <core/task/cpu/scheduler.h>
#include <core/assets/volume/cook_types.h>
#include <core/assets/cook_paths.h>
#include <core/assets/global.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSET_BUILDER_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AssetBuildServices{
    Core::CpuTaskScheduler& cpuScheduler;


public:
    explicit AssetBuildServices(Core::CpuTaskScheduler& cpuScheduler)noexcept
        : cpuScheduler(cpuScheduler)
    {}
};

struct AssetBuildRoot{
    Core::Assets::AssetString path;
    Path resolvedPath;
    ACompactString virtualRoot;


public:
    explicit AssetBuildRoot(Core::Assets::AssetArena& arena)noexcept
        : path(arena)
        , resolvedPath(arena)
    {}
    AssetBuildRoot(Core::Assets::AssetArena& arena, AStringView inPath, AStringView inVirtualRoot)
        : path(inPath, arena)
        , resolvedPath(arena)
        , virtualRoot(inVirtualRoot)
    {}
    AssetBuildRoot(Path&& inResolvedPath, const ACompactString& inVirtualRoot)noexcept
        : path(inResolvedPath.arena())
        , resolvedPath(Move(inResolvedPath))
        , virtualRoot(inVirtualRoot)
    {}
};

struct AssetBuildOptions{
    Core::Assets::AssetString repoRoot;
    Path resolvedRepoRoot;
    Core::Assets::AssetVector<AssetBuildRoot> assetRoots;
    Core::Assets::AssetString outputDirectory;
    ACompactString configuration;
    ACompactString assetType{ Core::Assets::AssetsVolumeCookDetail::s_AssetVolumeName };
    AssetBuildServices services;
    Core::Assets::AssetVector<Core::Assets::AssetString> inputs;
    b8 useExplicitInputs = false;


public:
    explicit AssetBuildOptions(Core::Assets::AssetArena& arena, Core::CpuTaskScheduler& cpuScheduler)noexcept
        : repoRoot(arena)
        , resolvedRepoRoot(arena)
        , assetRoots(arena)
        , outputDirectory(arena)
        , services(cpuScheduler)
        , inputs(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<Core::Assets::ResolvedCookPaths> ResolveCookPaths(const AssetBuildOptions& options, Core::Assets::AssetArena& arena);
[[nodiscard]] b8 SelectBuildInputs(const AssetBuildOptions& options, const Core::Assets::ResolvedCookPaths& paths, Core::Assets::ScratchArena& scratchArena, Core::Assets::DiscoveredNwbFileVector& files);
[[nodiscard]] b8 BuildAssets(const AssetBuildOptions& options, Core::Assets::ScratchArena& scratchArena);
[[nodiscard]] b8 BuildAssets(const AssetBuildOptions& options);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSET_BUILDER_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

