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


public:
    explicit ResolvedCookPaths(CookArena& arena)noexcept
        : repoRoot(arena)
        , assetRoots(arena)
        , outputDirectory(arena)
        , cacheDirectory(arena)
    {}
};

struct IndexedCookPath{
    AStringView text;
    usize fileIndex = 0u;
};

class PathSelectionIndex final{
public:
    PathSelectionIndex(Span<const DiscoveredNwbFile> files, b8 preserveCase, ScratchArena& scratchArena);


public:
    [[nodiscard]] b8 selectRange(AStringView path, b8 prefix, Span<u8> outSelected, Vector<usize, ScratchArena>* outOrder = nullptr)const;
    [[nodiscard]] Span<const IndexedCookPath> entries()const noexcept{ return m_entries; }


private:
    Vector<IndexedCookPath, ScratchArena> m_entries;
    Optional<Vector<ScratchString, ScratchArena>> m_ownedPaths;
};

namespace CookInputSelectionFailure{
    enum Enum : u8{
        None,
        ResolveInput,
        NotFileOrDirectory,
        DirectoryOutsideAssetRoots,
        NoMatchingAsset
    };
};

struct CookInputSelectionError{
    AStringView rawInput;
    Path resolvedPath;
    CookInputSelectionFailure::Enum reason = CookInputSelectionFailure::None;


public:
    explicit CookInputSelectionError(CookArena& arena)noexcept
        : resolvedPath(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Retains first-occurrence order and original path spelling; callers select their duplicate identity contract.
[[nodiscard]] Expected<CookVector<ResolvedAssetRoot>, AssetRootResolutionError> ResolveAssetRoots(const Path& repoRoot, Span<const CookString> sources, b8 inferFromInputs, AssetRootDuplicatePolicy::Enum duplicatePolicy, ScratchArena& scratchArena);
[[nodiscard]] b8 IsDirectoryWithinAssetRoots(const Path& directoryPath, Span<const ResolvedAssetRoot> assetRoots, ScratchArena& scratchArena);
[[nodiscard]] Expected<void, CookInputSelectionError> SelectDiscoveredNwbInputs(
    const Path& repoRoot,
    Span<const ResolvedAssetRoot> assetRoots,
    Span<const DiscoveredNwbFile> files,
    Span<const AssetString> inputs,
    b8 allowEmptyDirectory,
    ScratchArena& scratchArena,
    Span<u8> outSelected,
    Vector<usize, ScratchArena>* outOrder = nullptr
);
void CompactSelectedDiscoveredNwbFiles(Span<const u8> selected, DiscoveredNwbFileVector& inOutFiles)noexcept;
[[nodiscard]] b8 PrepareGeneratedIncludeRoot(const Path& includeRoot, AStringView generatorName);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

