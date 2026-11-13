// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_input_selection.h"

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cook_input_selection{


struct IndexedCookPath{
    AStringView text;
    usize fileIndex = 0u;
};

class PathSelectionIndex final{
public:
    PathSelectionIndex(Span<const DiscoveredNwbFile> files, b8 preserveCase, ScratchArena& scratchArena);


public:
    [[nodiscard]] b8 selectRange(AStringView path, b8 prefix, Span<u8> outSelected, Vector<usize, ScratchArena>* outOrder = nullptr)const;


private:
    Vector<IndexedCookPath, ScratchArena> m_entries;
    Optional<Vector<ScratchString, ScratchArena>> m_ownedPaths;
};

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



};


using __hidden_cook_input_selection::PathSelectionIndex;


b8 CookInputSelector::IsDirectoryWithinRoots(const Path& directoryPath, const Span<const ResolvedAssetRoot> assetRoots, ScratchArena& scratchArena){
#if !defined(FOUNDATION_PLATFORM_WINDOWS) && !defined(FOUNDATION_UNICODE)
    (void)scratchArena;
    const auto normalized = directoryPath.lexicallyNormal();
    for(const ResolvedAssetRoot& root : assetRoots){
        const auto rootPath = root.path.lexicallyNormal();
        if(IsPathPrefixText(rootPath.native(), normalized.native()))
            return true;
    }
#else
    ScratchString normalized = PathToString(scratchArena, directoryPath.lexicallyNormal());
#if defined(FOUNDATION_PLATFORM_WINDOWS)
    CanonicalizeTextInPlace(normalized);
#endif
    for(const ResolvedAssetRoot& root : assetRoots){
        ScratchString rootText = PathToString(scratchArena, root.path.lexicallyNormal());
#if defined(FOUNDATION_PLATFORM_WINDOWS)
        CanonicalizeTextInPlace(rootText);
#endif
        if(IsPathPrefixText(AStringView(rootText.data(), rootText.size()), AStringView(normalized.data(), normalized.size())))
            return true;
    }
#endif
    return false;
}

Expected<void, CookInputSelectionError> CookInputSelector::Select(
    const Path& repoRoot,
    const Span<const ResolvedAssetRoot> assetRoots,
    const Span<const DiscoveredNwbFile> files,
    const Span<const AssetString> inputs,
    const b8 allowEmptyDirectory,
    ScratchArena& scratchArena,
    const Span<u8> outSelected,
    Vector<usize, ScratchArena>* const outOrder
){
    FOUNDATION_ASSERT(outSelected.size() == files.size());
    for(u8& flag : outSelected)
        flag = 0u;
    if(outOrder)
        outOrder->clear();

    const PathSelectionIndex fileIndex(files, false, scratchArena);
#if !defined(FOUNDATION_PLATFORM_WINDOWS)
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
        if(isDirectory && !IsDirectoryWithinRoots(path, assetRoots, scratchArena)){
            CookInputSelectionError error(repoRoot.arena());
            error.rawInput = AStringView(input);
            error.resolvedPath = Move(path);
            error.reason = CookInputSelectionFailure::DirectoryOutsideAssetRoots;
            return MakeUnexpected(Move(error));
        }

        ScratchString normalized = PathToString(scratchArena, path.lexicallyNormal());
#if defined(FOUNDATION_PLATFORM_WINDOWS)
        CanonicalizeTextInPlace(normalized);
#else
        if(!isDirectory)
            CanonicalizeTextInPlace(normalized);
#endif

        NotNull<const PathSelectionIndex*> selectionIndex(&fileIndex);
#if !defined(FOUNDATION_PLATFORM_WINDOWS)
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

void CookInputSelector::Compact(const Span<const u8> selected, DiscoveredNwbFileVector& inOutFiles)noexcept{
    FOUNDATION_ASSERT(selected.size() == inOutFiles.size());
    CompactByMask(selected, inOutFiles);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

