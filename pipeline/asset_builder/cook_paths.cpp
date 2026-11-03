// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "build.h"
#include "../root_discovery.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSET_BUILDER_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Assets = Core::Assets;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cook_paths{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<Path, ErrorCode> ResolveBuildPath(Assets::AssetArena& arena, const Path& baseDirectory, const Path& relativeOrAbsolute){
    if(relativeOrAbsolute.empty())
        return MakeUnexpected(MakeErrorCode(ErrorCondition::invalid_argument));
    Path candidate(arena, relativeOrAbsolute);
    if(!candidate.isAbsolute())
        candidate = baseDirectory / candidate;
    const auto absolutePath = AbsolutePath(candidate);
    if(!absolutePath)
        return MakeUnexpected(absolutePath.error());
    return absolutePath->lexicallyNormal();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Assets::ResolvedCookPaths> ResolveCookPaths(const AssetBuildOptions& options, Assets::AssetArena& arena){
    if(options.assetRoots.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: no asset roots specified"));
        return MakeUnexpected(Failure{});
    }
    if(options.outputDirectory.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: output directory is empty"));
        return MakeUnexpected(Failure{});
    }

    Assets::ResolvedCookPaths paths(arena);
    auto repoRoot = ResolvePipelineRepoRoot(options.repoRoot, NWB_TEXT("AssetBuilder"), arena);
    if(!repoRoot)
        return MakeUnexpected(Failure{});
    paths.repoRoot = Move(*repoRoot);

    paths.assetRoots.reserve(options.assetRoots.size());
    for(const AssetBuildRoot& assetRoot : options.assetRoots){
        if(!IsValidAssetBuildVirtualRoot(assetRoot.virtualRoot.view(), Assets::s_EngineVirtualRoot, Assets::s_ProjectVirtualRoot)){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: asset root '{}' uses unsupported virtual root '{}'"), PathToString<tchar>(assetRoot.path), ConvertString(assetRoot.virtualRoot.view()));
            return MakeUnexpected(Failure{});
        }
        auto resolvedAssetRoot = __hidden_cook_paths::ResolveBuildPath(arena, paths.repoRoot, assetRoot.path);
        if(!resolvedAssetRoot){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: failed to resolve asset root '{}': {}"), PathToString<tchar>(assetRoot.path), ConvertString(resolvedAssetRoot.error().message()));
            return MakeUnexpected(Failure{});
        }
        paths.assetRoots.emplace_back(Move(*resolvedAssetRoot), assetRoot.virtualRoot);
    }

    auto outputDirectory = __hidden_cook_paths::ResolveBuildPath(arena, paths.repoRoot, options.outputDirectory);
    if(!outputDirectory){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: failed to resolve output directory '{}': {}"), PathToString<tchar>(options.outputDirectory), ConvertString(outputDirectory.error().message()));
        return MakeUnexpected(Failure{});
    }
    paths.outputDirectory = Move(*outputDirectory);

    paths.cacheDirectory = paths.repoRoot / s_AssetCacheDirectory;
    const auto created = EnsureDirectories(paths.cacheDirectory);
    if(!created){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: failed to create cache directory '{}': {}"), PathToString<tchar>(paths.cacheDirectory), ConvertString(created.error().message()));
        return MakeUnexpected(Failure{});
    }
    return paths;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSET_BUILDER_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

