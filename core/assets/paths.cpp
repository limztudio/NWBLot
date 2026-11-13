// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "paths.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetPathsDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 CanonicalNativeTextEquals(const TStringView nativeText, const AStringView expected, Alloc::ScratchArena& scratchArena){
    if constexpr(!IsSame_V<tchar, char>){
        b8 needsConversion = false;
        for(const tchar ch : nativeText){
            if(static_cast<u32>(ch) >= 0x80u){
                needsConversion = true;
                break;
            }
        }
        if(!needsConversion){
            for(const char ch : expected){
                if(static_cast<u8>(ch) >= 0x80u){
                    needsConversion = true;
                    break;
                }
            }
        }
        if(needsConversion){
            auto converted = PathToString<char>(scratchArena, nativeText);
            CanonicalizeTextInPlace(converted);
            return AStringView(converted) == expected;
        }
    }
    if(nativeText.size() != expected.size())
        return false;
    for(usize index = 0u; index < nativeText.size(); ++index){
        if(Canonicalize(nativeText[index]) != static_cast<tchar>(expected[index]))
            return false;
    }
    return true;
}


Expected<ACompactString> ExtractAssetVirtualRoot(const AStringView virtualPath, Alloc::ScratchArena& scratchArena){
    ACompactString virtualRoot;

    const ::Path<Alloc::ScratchArena> virtualPathPath(scratchArena, virtualPath);
    const auto componentIt = virtualPathPath.begin();
    if(componentIt == virtualPathPath.end())
        return MakeUnexpected(Failure{});

    AString<Alloc::ScratchArena> convertedComponentText(scratchArena);
    const AStringView componentText = NativeTextView(componentIt.nativeComponent(), convertedComponentText);
    if(!virtualRoot.assign(componentText) || virtualRoot.empty())
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
        AString<Alloc::ScratchArena> convertedComponentText(scratchArena);
        const AStringView componentText = AssetPathsDetail::NativeTextView(componentIt.nativeComponent(), convertedComponentText);
        if(!requestedVirtualRoot.assign(componentText) || requestedVirtualRoot.empty())
            return MakeUnexpected(Failure{});
    }
    ++componentIt;

    Optional<Path> firstResolvedPath;
    for(const auto& assetRoot : assetRoots){
        if(assetRoot.virtualRoot != requestedVirtualRoot)
            continue;

        Path resolvedPath(arena, assetRoot.path);
        const auto appendComponent = [&](const AStringView componentText){
            if(componentText.empty() || componentText == "." || componentText == ".." || componentText.find('/') != AStringView::npos){
                NWB_LOGGER_ERROR(FOUNDATION_TEXT("Assets: invalid virtual path '{}'; components must not be empty, '.', '..' or contain path separators"), ConvertString(virtualPath));
                return false;
            }
            resolvedPath /= componentText;
            return true;
        };
        for(auto subIt = componentIt; subIt != virtualPathPath.end(); ++subIt){
#if !defined(FOUNDATION_UNICODE)
            const AStringView nativeComponent = subIt.nativeComponent();
            b8 alreadyCanonical = true;
            for(const char ch : nativeComponent){
                if(Canonicalize(ch) != ch){
                    alreadyCanonical = false;
                    break;
                }
            }
            if(alreadyCanonical){
                if(!appendComponent(nativeComponent))
                    return MakeUnexpected(Failure{});
                continue;
            }
#endif
            AString<Alloc::ScratchArena> componentText = PathToString(scratchArena, subIt.nativeComponent());
            CanonicalizeTextInPlace(componentText);
            if(!appendComponent(AStringView(componentText)))
                return MakeUnexpected(Failure{});
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
        static_assert(s_NwbExtension == ".nwb");
        if(EqualsAsciiIgnoreCase(candidatePath.extensionView(), FOUNDATION_TEXT(".nwb")))
            continue;
        if(!AssetPathsDetail::CanonicalNativeTextEquals(candidatePath.stemView(), AStringView(nwbStem), scratchArena))
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

