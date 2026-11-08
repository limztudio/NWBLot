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



class CookInputSelector final{
private:
    [[nodiscard]] static b8 IsDirectoryWithinRoots(const Path& directoryPath, Span<const ResolvedAssetRoot> assetRoots, ScratchArena& scratchArena);


public:
    [[nodiscard]] static Expected<void, CookInputSelectionError> Select(
        const Path& repoRoot,
        Span<const ResolvedAssetRoot> assetRoots,
        Span<const DiscoveredNwbFile> files,
        Span<const AssetString> inputs,
        b8 allowEmptyDirectory,
        ScratchArena& scratchArena,
        Span<u8> outSelected,
        Vector<usize, ScratchArena>* outOrder = nullptr
    );
    static void Compact(Span<const u8> selected, DiscoveredNwbFileVector& inOutFiles)noexcept;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
