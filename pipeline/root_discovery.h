// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <engine_namespace.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_PIPELINE_BEGIN
#define NWB_PIPELINE_BEGIN NWB_BEGIN namespace Pipeline{
#define NWB_PIPELINE_END }; NWB_END
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/alloc/scratch.h>
#include <core/assets/cook_paths.h>
#include <core/assets/global.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_PIPELINE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr AStringView s_DefaultRepoRoot = ".";

[[nodiscard]] inline Expected<Path> ResolvePipelineRepoRoot(
    const Path& requestedRepoRoot,
    const TStringView toolLabel,
    Core::Assets::AssetArena& arena
){
    const auto repoRoot = AbsolutePath(requestedRepoRoot.empty() ? Path(arena, s_DefaultRepoRoot) : requestedRepoRoot);
    if(!repoRoot){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to resolve repository root: {}"), toolLabel, ConvertString(repoRoot.error().message()));
        return MakeUnexpected(Failure{});
    }
    return repoRoot->lexicallyNormal();
}

[[nodiscard]] inline Expected<Path> ResolvePipelineRepoRoot(
    const AStringView repoRootText,
    const TStringView toolLabel,
    Core::Assets::AssetArena& arena
){
    return ResolvePipelineRepoRoot(Path(arena, repoRootText.empty() ? s_DefaultRepoRoot : repoRootText), toolLabel, arena);
}

[[nodiscard]] inline Expected<Core::Assets::CookVector<Core::Assets::ResolvedAssetRoot>> ResolvePipelineAssetRoots(
    const Path& repoRoot,
    const Core::Assets::AssetVector<Core::Assets::AssetString>& inputs,
    const Core::Assets::AssetVector<Core::Assets::AssetString>& assetRoots,
    const Core::Assets::AssetRootDuplicatePolicy::Enum duplicatePolicy,
    const TStringView toolLabel,
    const TStringView emptyErrorMessage,
    Core::Alloc::ScratchArena& scratchArena
){
    const auto& sources = assetRoots.empty() ? inputs : assetRoots;
    auto roots = Core::Assets::ResolveAssetRoots(repoRoot, sources, assetRoots.empty(), duplicatePolicy, scratchArena);
    if(!roots){
        const auto& error = roots.error();
        if(error.reason == Core::Assets::AssetRootResolutionFailure::InspectInput)
            NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to inspect input '{}'"), toolLabel, ConvertString(sources[error.sourceIndex]));
        else
            NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to resolve asset root from '{}'"), toolLabel, ConvertString(sources[error.sourceIndex]));
        return MakeUnexpected(Failure{});
    }
    if(roots->empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: {}"), toolLabel, emptyErrorMessage);
        return MakeUnexpected(Failure{});
    }
    return Move(*roots);
}

inline void LogCookInputSelectionFailure(const Core::Assets::CookInputSelectionError& error, const TStringView toolLabel){
    switch(error.reason){
        case Core::Assets::CookInputSelectionFailure::ResolveInput:
            NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to resolve input '{}'"), toolLabel, ConvertString(error.rawInput));
            break;
        case Core::Assets::CookInputSelectionFailure::NotFileOrDirectory:
            NWB_LOGGER_ERROR(NWB_TEXT("{}: input is not a file or directory '{}'"), toolLabel, PathToString<tchar>(error.resolvedPath));
            break;
        case Core::Assets::CookInputSelectionFailure::DirectoryOutsideAssetRoots:
            NWB_LOGGER_ERROR(NWB_TEXT("{}: input directory is outside the asset roots '{}'"), toolLabel, PathToString<tchar>(error.resolvedPath));
            break;
        case Core::Assets::CookInputSelectionFailure::NoMatchingAsset:
        case Core::Assets::CookInputSelectionFailure::None:
            NWB_LOGGER_ERROR(NWB_TEXT("{}: input is not a .nwb asset within the asset roots '{}'"), toolLabel, PathToString<tchar>(error.resolvedPath));
            break;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_PIPELINE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

