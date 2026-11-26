// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cook_metadata.h"

#include <global/expected.h>
#include <global/filesystem/operations_file_io.h>
#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetRootDuplicatePolicy{
    enum Enum : u8{
        ExactText,
        HostFilesystem
    };
};

namespace AssetRootResolutionFailure{
    enum Enum : u8{
        ResolvePath,
        InspectInput
    };
};

struct AssetRootResolutionError{
    Path inputPath;
    usize sourceIndex;
    AssetRootResolutionFailure::Enum reason;
    ErrorCode error;
};

struct ResolvedCookPaths{
    Path repoRoot;
    CookVector<ResolvedAssetRoot> assetRoots;
    Path outputDirectory;
    Path cacheDirectory;


public:
    explicit ResolvedCookPaths(CookArena& arena)noexcept
        : repoRoot(arena)
        , assetRoots(arena)
        , outputDirectory(arena)
        , cacheDirectory(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline Expected<::Path<ArenaT>> ResolveCookVirtualAssetPath(ArenaT& arena, const ResolvedCookPaths& paths, const AStringView name){
    ScratchArena scratch(Name("ResolveCookVirtualAssetPath"));
    const auto path = ResolveVirtualAssetPath(paths.assetRoots, name, paths.repoRoot.arena(), scratch);
    if(!path)
        return MakeUnexpected(Failure{});
    return ::Path<ArenaT>(arena, *path);
}

[[nodiscard]] Expected<CookVector<ResolvedAssetRoot>, AssetRootResolutionError> ResolveAssetRoots(const Path& repoRoot, Span<const CookString> sources, b8 inferFromInputs, AssetRootDuplicatePolicy::Enum duplicatePolicy, ScratchArena& scratchArena);

template<typename PathArenaT>
[[nodiscard]] inline b8 PrepareGeneratedIncludeRoot(const ::Path<PathArenaT>& includeRoot, const AStringView generatorName){
    const auto removed = RemoveAllIfExists(includeRoot);
    if(!removed){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("{}: failed to clear generated include directory '{}': {}"), ConvertString(generatorName), PathToString<tchar>(includeRoot), ConvertString(removed.error().message()));
        return false;
    }

    const auto ensured = EnsureDirectories(includeRoot);
    if(!ensured){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("{}: failed to create generated include directory '{}': {}"), ConvertString(generatorName), PathToString<tchar>(includeRoot), ConvertString(ensured.error().message()));
        return false;
    }
    return true;
}

template<typename PathArenaT>
[[nodiscard]] inline b8 WriteGeneratedIncludeFile(const ::Path<PathArenaT>& outputPath, const AStringView source, const AStringView generatorName){
    const auto parentPath = outputPath.parentPath();
    const auto ensured = EnsureDirectories(parentPath);
    if(!ensured){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("{}: failed to create generated include parent '{}' (error {})"), ConvertString(generatorName), PathToString<tchar>(parentPath), ensured.error().value());
        return false;
    }
    if(!WriteTextFile(outputPath, source)){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("{}: failed to write generated include '{}'"), ConvertString(generatorName), PathToString<tchar>(outputPath));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

