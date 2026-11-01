// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_paths.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cook_paths{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr AStringView s_ImplDirectoryName = "impl";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void CanonicalizeRootIdentity([[maybe_unused]] const AssetRootDuplicatePolicy::Enum duplicatePolicy, [[maybe_unused]] ScratchString& identity)noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
    if(duplicatePolicy == AssetRootDuplicatePolicy::HostFilesystem)
        CanonicalizeTextInPlace(identity);
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


PathSelectionIndex::PathSelectionIndex(const Span<const DiscoveredNwbFile> files, const b8 preserveCase, ScratchArena& scratchArena)
    : m_entries(scratchArena)
{
    m_entries.reserve(files.size());
    if(preserveCase){
        m_ownedPaths.emplace(scratchArena);
        m_ownedPaths->reserve(files.size());
    }
    for(usize i = 0u; i < files.size(); ++i){
        if(preserveCase){
            m_ownedPaths->emplace_back(PathToString(scratchArena, files[i].filePath.lexicallyNormal()));
            m_entries.push_back(IndexedCookPath{ AStringView(m_ownedPaths->back()), i });
        }
        else
            m_entries.push_back(IndexedCookPath{ AStringView(files[i].normalizedPathText), i });
    }
    Sort(m_entries.begin(), m_entries.end(), [](const IndexedCookPath& lhs, const IndexedCookPath& rhs)noexcept{ return lhs.text < rhs.text; });
}

b8 PathSelectionIndex::selectRange(const AStringView path, const b8 prefix, const Span<u8> outSelected, Vector<usize, ScratchArena>* const outOrder)const{
    auto entry = LowerBound(m_entries.begin(), m_entries.end(), path, [](const IndexedCookPath& candidate, const AStringView value)noexcept{ return candidate.text < value; });
    b8 matched = false;
    for(; entry != m_entries.end(); ++entry){
        if(prefix ? !entry->text.starts_with(path) : entry->text != path)
            break;
        if(!outSelected[entry->fileIndex]){
            outSelected[entry->fileIndex] = 1u;
            if(outOrder)
                outOrder->push_back(entry->fileIndex);
        }
        matched = true;
    }
    return matched;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<CookVector<ResolvedAssetRoot>, AssetRootResolutionError> ResolveAssetRoots(const Path& repoRoot, const Span<const CookString> sources, const b8 inferFromInputs, const AssetRootDuplicatePolicy::Enum duplicatePolicy, ScratchArena& scratchArena){
    CookArena& arena = repoRoot.arena();
    CookVector<ResolvedAssetRoot> roots(arena);
    roots.reserve(sources.size());
    for(usize sourceIndex = 0u; sourceIndex < sources.size(); ++sourceIndex){
        const AStringView source(sources[sourceIndex]);
        auto resolved = ResolveAbsolutePath(arena, repoRoot, source);
        if(!resolved){
            return MakeUnexpected(AssetRootResolutionError{ .inputPath = Path(arena, source), .sourceIndex = sourceIndex, .reason = AssetRootResolutionFailure::ResolvePath, .error = resolved.error(), });
        }
        Path path = Move(*resolved);
        if(inferFromInputs){
            const auto directory = IsDirectory(path);
            if(!directory){
                return MakeUnexpected(AssetRootResolutionError{ .inputPath = Move(path), .sourceIndex = sourceIndex, .reason = AssetRootResolutionFailure::InspectInput, .error = directory.error(), });
            }
            if(!*directory)
                path = Move(path).parentPath();
            ::Path<ScratchArena> ancestor(scratchArena, path);
            while(!ancestor.empty()){
                ScratchString name = PathToString(scratchArena, ancestor.filenameView());
                CanonicalizeTextInPlace(name);
                if(name == s_AssetsDirectoryName){
                    path = Path(arena, ancestor);
                    break;
                }
                if(ancestor.parentPathView() == ancestor.native())
                    break;
                ancestor = Move(ancestor).parentPath();
            }
        }

        ScratchString identity = PathToString(scratchArena, path);
        __hidden_cook_paths::CanonicalizeRootIdentity(duplicatePolicy, identity);
        b8 duplicate = false;
        for(const ResolvedAssetRoot& root : roots){
            ScratchString existing = PathToString(scratchArena, root.path);
            __hidden_cook_paths::CanonicalizeRootIdentity(duplicatePolicy, existing);
            if(existing == identity){
                duplicate = true;
                break;
            }
        }
        if(duplicate)
            continue;

        ScratchString parentName = PathToString(scratchArena, ::Path<ScratchArena>(scratchArena, path.parentPathView()).filenameView());
        CanonicalizeTextInPlace(parentName);
        const AStringView virtualRoot = parentName == __hidden_cook_paths::s_ImplDirectoryName ? s_EngineVirtualRoot : s_ProjectVirtualRoot;
        roots.emplace_back(Move(path), virtualRoot);
    }
    return roots;
}

b8 IsDirectoryWithinAssetRoots(const Path& directoryPath, const Span<const ResolvedAssetRoot> assetRoots, ScratchArena& scratchArena){
    ScratchString normalized = PathToString(scratchArena, directoryPath.lexicallyNormal());
#if defined(NWB_PLATFORM_WINDOWS)
    CanonicalizeTextInPlace(normalized);
#endif
    for(const ResolvedAssetRoot& root : assetRoots){
        ScratchString rootText = PathToString(scratchArena, root.path.lexicallyNormal());
#if defined(NWB_PLATFORM_WINDOWS)
        CanonicalizeTextInPlace(rootText);
#endif
        if(IsPathPrefixText(AStringView(rootText.data(), rootText.size()), AStringView(normalized.data(), normalized.size())))
            return true;
    }
    return false;
}

Expected<void, CookInputSelectionError> SelectDiscoveredNwbInputs(
    const Path& repoRoot,
    const Span<const ResolvedAssetRoot> assetRoots,
    const Span<const DiscoveredNwbFile> files,
    const Span<const AssetString> inputs,
    const b8 allowEmptyDirectory,
    ScratchArena& scratchArena,
    const Span<u8> outSelected,
    Vector<usize, ScratchArena>* const outOrder
){
    NWB_ASSERT(outSelected.size() == files.size());
    for(u8& flag : outSelected)
        flag = 0u;
    if(outOrder)
        outOrder->clear();

    const PathSelectionIndex fileIndex(files, false, scratchArena);
#if !defined(NWB_PLATFORM_WINDOWS)
    Optional<PathSelectionIndex> directoryIndex;
#endif
    for(const AssetString& input : inputs){
        auto pathResult = ResolveAbsolutePath(repoRoot.arena(), repoRoot, AStringView(input));
        if(!pathResult){
            CookInputSelectionError error(repoRoot.arena());
            error.rawInput = AStringView(input);
            error.reason = CookInputSelectionFailure::ResolveInput;
            return MakeUnexpected(Move(error));
        }

        Path path = Move(*pathResult);
        const auto directory = IsDirectory(path);
        const auto regular = directory && *directory ? Expected<b8, ErrorCode>(true) : IsRegularFile(path);
        if(!directory || (!*directory && (!regular || !*regular))){
            CookInputSelectionError error(repoRoot.arena());
            error.rawInput = AStringView(input);
            error.resolvedPath = Move(path);
            error.reason = CookInputSelectionFailure::NotFileOrDirectory;
            return MakeUnexpected(Move(error));
        }

        const b8 isDirectory = *directory;
        if(isDirectory && !IsDirectoryWithinAssetRoots(path, assetRoots, scratchArena)){
            CookInputSelectionError error(repoRoot.arena());
            error.rawInput = AStringView(input);
            error.resolvedPath = Move(path);
            error.reason = CookInputSelectionFailure::DirectoryOutsideAssetRoots;
            return MakeUnexpected(Move(error));
        }

        ScratchString normalized = PathToString(scratchArena, path.lexicallyNormal());
#if defined(NWB_PLATFORM_WINDOWS)
        CanonicalizeTextInPlace(normalized);
#else
        if(!isDirectory)
            CanonicalizeTextInPlace(normalized);
#endif

        NotNull<const PathSelectionIndex*> selectionIndex(&fileIndex);
#if !defined(NWB_PLATFORM_WINDOWS)
        // Directory containment follows host case rules; explicit asset identities retain their canonical spelling.
        if(isDirectory){
            if(!directoryIndex)
                directoryIndex.emplace(files, true, scratchArena);
            selectionIndex = &*directoryIndex;
        }
#endif
        const usize orderStart = outOrder ? outOrder->size() : 0u;
        b8 matched = selectionIndex->selectRange(normalized, false, outSelected, outOrder);
        if(isDirectory){
            if(!normalized.empty() && normalized.back() != '/')
                normalized += '/';
            matched |= selectionIndex->selectRange(normalized, true, outSelected, outOrder);
        }
        if(outOrder && outOrder->size() - orderStart > 1u)
            Sort(outOrder->begin() + static_cast<isize>(orderStart), outOrder->end());

        if(!matched && (!isDirectory || !allowEmptyDirectory)){
            CookInputSelectionError error(repoRoot.arena());
            error.rawInput = AStringView(input);
            error.resolvedPath = Move(path);
            error.reason = CookInputSelectionFailure::NoMatchingAsset;
            return MakeUnexpected(Move(error));
        }
    }
    return {};
}

void CompactSelectedDiscoveredNwbFiles(const Span<const u8> selected, DiscoveredNwbFileVector& inOutFiles)noexcept{
    NWB_ASSERT(selected.size() == inOutFiles.size());
    usize next = 0u;
    for(usize i = 0u; i < inOutFiles.size(); ++i){
        if(!selected[i])
            continue;
        if(next != i)
            inOutFiles[next] = Move(inOutFiles[i]);
        ++next;
    }
    inOutFiles.erase(inOutFiles.begin() + next, inOutFiles.end());
}

b8 PrepareGeneratedIncludeRoot(const Path& includeRoot, const AStringView generatorName){
    const auto removed = RemoveAllIfExists(includeRoot);
    if(!removed){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to clear generated include directory '{}': {}"), ConvertString(generatorName), PathToString<tchar>(includeRoot), ConvertString(removed.error().message()));
        return false;
    }

    const auto ensured = EnsureDirectories(includeRoot);
    if(!ensured){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to create generated include directory '{}': {}"), ConvertString(generatorName), PathToString<tchar>(includeRoot), ConvertString(ensured.error().message()));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

