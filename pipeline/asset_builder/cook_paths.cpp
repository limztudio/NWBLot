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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Assets::ResolvedCookPaths> ResolveCookPaths(
    const AssetBuildOptions& options,
    Assets::AssetArena& arena,
    Assets::ScratchArena& scratchArena
){
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
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: failed to resolve repo root: {}"), StringConvert(repoRoot.error().message()));
        return MakeUnexpected(Failure{});
    }
    paths.repoRoot = repoRoot->lexicallyNormal();

    paths.assetRoots.reserve(options.assetRoots.size());
    for(const AssetBuildRoot& assetRoot : options.assetRoots){
        if(assetRoot.virtualRoot.view() != Assets::s_EngineVirtualRoot && assetRoot.virtualRoot.view() != Assets::s_ProjectVirtualRoot){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: asset root '{}' uses unsupported virtual root '{}'")
                , StringConvert(assetRoot.path)
                , StringConvert(assetRoot.virtualRoot.view())
            );
            return MakeUnexpected(Failure{});
        }
        auto resolvedAssetRoot = ResolveAbsolutePath(arena, paths.repoRoot, AStringView(assetRoot.path));
        if(!resolvedAssetRoot){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: failed to resolve asset root '{}': {}")
                , StringConvert(assetRoot.path)
                , StringConvert(resolvedAssetRoot.error().message())
            );
            return MakeUnexpected(Failure{});
        }
        paths.assetRoots.emplace_back(Move(*resolvedAssetRoot), assetRoot.virtualRoot);
    }

    auto outputDirectory = ResolveAbsolutePath(arena, paths.repoRoot, AStringView(options.outputDirectory));
    if(!outputDirectory){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: failed to resolve output directory '{}': {}")
            , StringConvert(options.outputDirectory)
            , StringConvert(outputDirectory.error().message())
        );
        return MakeUnexpected(Failure{});
    }
    paths.outputDirectory = Move(*outputDirectory);

    const Path defaultCacheDirectory = paths.repoRoot / "__build_obj/asset_cache";
    const Assets::AssetString& requestedCacheDirectory = options.cacheDirectory;
    Assets::ScratchString defaultCacheDirectoryText(scratchArena);
    if(requestedCacheDirectory.empty())
        defaultCacheDirectoryText = PathToString(scratchArena, defaultCacheDirectory);
    const AStringView cacheText = requestedCacheDirectory.empty() ? AStringView(defaultCacheDirectoryText) : AStringView(requestedCacheDirectory);
    auto cacheDirectory = ResolveAbsolutePath(arena, paths.repoRoot, cacheText);
    if(!cacheDirectory){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: failed to resolve cache directory '{}': {}")
            , StringConvert(cacheText)
            , StringConvert(cacheDirectory.error().message())
        );
        return MakeUnexpected(Failure{});
    }
    paths.cacheDirectory = Move(*cacheDirectory);
    const auto created = EnsureDirectories(paths.cacheDirectory);
    if(!created){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: failed to create cache directory '{}': {}")
            , PathToString<tchar>(paths.cacheDirectory)
            , StringConvert(created.error().message())
        );
        return MakeUnexpected(Failure{});
    }
    return paths;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSET_BUILDER_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

