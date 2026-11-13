// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "paths.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetPathsDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<ACompactString> ExtractAssetVirtualRoot(const AStringView virtualPath, Alloc::ScratchArena& scratchArena){
    ACompactString virtualRoot;

    const ::Path<Alloc::ScratchArena> virtualPathPath(scratchArena, virtualPath);
    const auto componentIt = virtualPathPath.begin();
    if(componentIt == virtualPathPath.end())
        return MakeUnexpected(Failure{});

    const AString<Alloc::ScratchArena> componentText = PathToString(scratchArena, componentIt.nativeComponent());
    if(!virtualRoot.assign(AStringView(componentText)) || virtualRoot.empty())
        return MakeUnexpected(Failure{});
    return virtualRoot;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 HasReservedAssetVirtualRoot(const AStringView virtualPath, Alloc::ScratchArena& scratchArena){
    const auto virtualRoot = AssetPathsDetail::ExtractAssetVirtualRoot(virtualPath, scratchArena);
    if(!virtualRoot)
        return false;

    return virtualRoot->view() == s_EngineVirtualRoot || virtualRoot->view() == s_ProjectVirtualRoot;
}

Expected<Path> ResolveVirtualAssetPath(const Span<const ResolvedAssetRoot> assetRoots, const AStringView virtualPath, AssetArena& arena, Alloc::ScratchArena& scratchArena){
    const ::Path<Alloc::ScratchArena> virtualPathPath(scratchArena, virtualPath);
    auto componentIt = virtualPathPath.begin();
    if(componentIt == virtualPathPath.end())
        return MakeUnexpected(Failure{});

    ACompactString requestedVirtualRoot;
    {
        const AString<Alloc::ScratchArena> componentText = PathToString(scratchArena, componentIt.nativeComponent());
        if(!requestedVirtualRoot.assign(AStringView(componentText)) || requestedVirtualRoot.empty())
            return MakeUnexpected(Failure{});
    }
    ++componentIt;

    Optional<Path> firstResolvedPath;
    for(const auto& assetRoot : assetRoots){
        if(assetRoot.virtualRoot != requestedVirtualRoot)
            continue;

        Path resolvedPath(arena, assetRoot.path);
        for(auto subIt = componentIt; subIt != virtualPathPath.end(); ++subIt){
            AString<Alloc::ScratchArena> componentText = PathToString(scratchArena, subIt.nativeComponent());
            CanonicalizeTextInPlace(componentText);
            if(componentText.empty() || componentText == "." || componentText == ".." || componentText.find('/') != AString<Alloc::ScratchArena>::npos){
                NWB_LOGGER_ERROR(FOUNDATION_TEXT("Assets: invalid virtual path '{}'; components must not be empty, '.', '..' or contain path separators"), ConvertString(virtualPath));
                return MakeUnexpected(Failure{});
            }

            resolvedPath /= componentText;
        }
        resolvedPath = resolvedPath.lexicallyNormal();
        const auto fileExists = FileExists(resolvedPath);
        if(!fileExists && !IsMissingPathError(fileExists.error())){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Assets: failed to inspect resolved path '{}': {}"), PathToString<tchar>(resolvedPath), ConvertString(fileExists.error().message()));
            return MakeUnexpected(Failure{});
        }
        if(fileExists && *fileExists)
            return resolvedPath;
        if(!firstResolvedPath.has_value())
            firstResolvedPath.emplace(Move(resolvedPath));
    }

    if(firstResolvedPath.has_value())
        return Move(*firstResolvedPath);
    return MakeUnexpected(Failure{});
}

Expected<AssetString> ResolvePairedSourcePathFromMetadata(const Path& nwbFilePath, AssetArena& arena, Alloc::ScratchArena& scratchArena){
    const ::Path<Alloc::ScratchArena> parentDirectory(scratchArena, nwbFilePath.parentPathView());
    if(parentDirectory.empty()){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Meta '{}': failed to resolve paired source because the metadata directory is empty"), PathToString<tchar>(nwbFilePath));
        return MakeUnexpected(Failure{});
    }

    auto nwbStem = PathToString(scratchArena, nwbFilePath.stemView());
    CanonicalizeTextInPlace(nwbStem);
    if(nwbStem.empty()){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Meta '{}': failed to resolve paired source because the metadata filename stem is empty"), PathToString<tchar>(nwbFilePath));
        return MakeUnexpected(Failure{});
    }

    const ::Path<Alloc::ScratchArena>* matchedSourcePath = nullptr;
    usize matchCount = 0;
    const auto entries = DirectoryIterator<Alloc::ScratchArena>::Create(parentDirectory);
    if(!entries){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Meta '{}': failed to scan metadata directory '{}': {}"), PathToString<tchar>(nwbFilePath), PathToString<tchar>(parentDirectory), ConvertString(entries.error().message()));
        return MakeUnexpected(Failure{});
    }
    for(const auto& dirEntry : *entries){
        const auto isRegularFile = dirEntry.isRegularFile();
        if(!isRegularFile){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Meta '{}': failed to inspect '{}' while resolving paired source: {}"), PathToString<tchar>(nwbFilePath), PathToString<tchar>(dirEntry.path()), ConvertString(isRegularFile.error().message()));
            return MakeUnexpected(Failure{});
        }
        if(!*isRegularFile)
            continue;

        const ::Path<Alloc::ScratchArena>& candidatePath = dirEntry.path();
        auto candidateExtension = PathToString(scratchArena, candidatePath.extensionView());
        CanonicalizeTextInPlace(candidateExtension);
        if(candidateExtension == s_NwbExtension)
            continue;
        auto candidateStem = PathToString(scratchArena, candidatePath.stemView());
        CanonicalizeTextInPlace(candidateStem);
        if(candidateStem != nwbStem)
            continue;

        matchedSourcePath = &candidatePath;
        ++matchCount;
        if(matchCount > 1){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Meta '{}': paired source is ambiguous; multiple source files share stem '{}'"), PathToString<tchar>(nwbFilePath), ConvertString(nwbStem));
            return MakeUnexpected(Failure{});
        }
    }

    if(matchCount == 0){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Meta '{}': failed to find a paired source file with stem '{}'"), PathToString<tchar>(nwbFilePath), ConvertString(nwbStem));
        return MakeUnexpected(Failure{});
    }

    const ::Path<Alloc::ScratchArena> normalizedSourcePath = MakeNotNull(matchedSourcePath)->lexicallyNormal();
    return PathToString(arena, normalizedSourcePath);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

