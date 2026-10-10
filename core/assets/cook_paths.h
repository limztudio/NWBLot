// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cook_metadata.h"

#include <global/expected.h>
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

    explicit ResolvedCookPaths(CookArena& arena)
        : repoRoot(arena)
        , assetRoots(arena)
        , outputDirectory(arena)
        , cacheDirectory(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Retains first-occurrence order and original path spelling; callers select their duplicate identity contract.
[[nodiscard]] Expected<CookVector<ResolvedAssetRoot>, AssetRootResolutionError> ResolveAssetRoots(
    const Path& repoRoot,
    Span<const CookString> sources,
    bool inferFromInputs,
    AssetRootDuplicatePolicy::Enum duplicatePolicy,
    ScratchArena& scratchArena
);
[[nodiscard]] inline bool PrepareGeneratedIncludeRoot(const Path& includeRoot, const AStringView generatorName){
    const auto removed = RemoveAllIfExists(includeRoot);
    if(!removed){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to clear generated include directory '{}': {}")
            , StringConvert(generatorName)
            , PathToString<tchar>(includeRoot)
            , StringConvert(removed.error().message())
        );
        return false;
    }

    const auto ensured = EnsureDirectories(includeRoot);
    if(!ensured){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to create generated include directory '{}': {}")
            , StringConvert(generatorName)
            , PathToString<tchar>(includeRoot)
            , StringConvert(ensured.error().message())
        );
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

