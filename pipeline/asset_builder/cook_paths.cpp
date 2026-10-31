// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_paths.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSET_BUILDER_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Assets = Core::Assets;

static constexpr AStringView s_AssetCacheDirectory = "__asset_cache";


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
    const Path requestedRepoRoot(arena, options.repoRoot.empty() ? AStringView(".") : AStringView(options.repoRoot));
    auto repoRoot = AbsolutePath(requestedRepoRoot);
    if(!repoRoot){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: failed to resolve repo root: {}"), ConvertString(repoRoot.error().message()));
        return MakeUnexpected(Failure{});
    }
    paths.repoRoot = repoRoot->lexicallyNormal();

    paths.assetRoots.reserve(options.assetRoots.size());
    for(const AssetBuildRoot& assetRoot : options.assetRoots){
        if(assetRoot.virtualRoot.view() != Assets::s_EngineVirtualRoot && assetRoot.virtualRoot.view() != Assets::s_ProjectVirtualRoot){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: asset root '{}' uses unsupported virtual root '{}'"), ConvertString(assetRoot.path), ConvertString(assetRoot.virtualRoot.view()));
            return MakeUnexpected(Failure{});
        }
        auto resolvedAssetRoot = ResolveAbsolutePath(arena, paths.repoRoot, AStringView(assetRoot.path));
        if(!resolvedAssetRoot){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: failed to resolve asset root '{}': {}"), ConvertString(assetRoot.path), ConvertString(resolvedAssetRoot.error().message()));
            return MakeUnexpected(Failure{});
        }
        paths.assetRoots.emplace_back(Move(*resolvedAssetRoot), assetRoot.virtualRoot);
    }

    auto outputDirectory = ResolveAbsolutePath(arena, paths.repoRoot, AStringView(options.outputDirectory));
    if(!outputDirectory){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: failed to resolve output directory '{}': {}"), ConvertString(options.outputDirectory), ConvertString(outputDirectory.error().message()));
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

