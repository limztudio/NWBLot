// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"
#include "ref.h"

#include <core/common/log.h>

#include <global/allocation_size.h>
#include <global/counting_output.h>
#include <global/expected.h>
#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_AssetsDirectoryName = "assets";
static constexpr AStringView s_EngineVirtualRoot = "engine";
static constexpr AStringView s_ProjectVirtualRoot = "project";
static constexpr AStringView s_NwbExtension = ".nwb";
static constexpr AStringView s_MetadataAssetVariable = "asset";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ResolvedAssetRoot{
    Path path;
    ACompactString virtualRoot;


public:
    explicit ResolvedAssetRoot(AssetArena& arena)noexcept
        : path(arena)
    {}
    ResolvedAssetRoot(Path inPath, const AStringView inVirtualRoot)noexcept
        : path(Move(inPath))
        , virtualRoot(inVirtualRoot)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetPathsDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 CanonicalNativeTextEquals(TStringView nativeText, AStringView expected, Alloc::ScratchArena& scratchArena);

// The result borrows nativeText or convertedText.
template<typename ArenaT>
[[nodiscard]] inline AStringView NativeTextView(const TStringView nativeText, AString<ArenaT>& convertedText){
#if defined(FOUNDATION_UNICODE)
    convertedText.clear();
    CountingByteOutput counter;
    WriteConvertedText<char>(counter, nativeText);
    convertedText.reserve(counter.byteCount);
    auto out = BackInserter(convertedText);
    WriteConvertedText<char>(out, nativeText);
    return AStringView(convertedText);
#else
    (void)convertedText;
    return AStringView(nativeText);
#endif
}


template<typename ArenaT, typename Predicate>
[[nodiscard]] inline b8 ValidateWithNativeText(const TStringView nativeText, ArenaT& arena, Predicate&& predicate){
    AString<ArenaT> convertedText(arena);
    return predicate(NativeTextView(nativeText, convertedText));
}


template<typename PathArenaT>
struct RelativeAssetPathLayout{
    typename ::Path<PathArenaT>::const_iterator acceptedEnd;
    usize byteCount = 0u;
    b8 accepted = false;
};


template<typename PathArenaT>
[[nodiscard]] inline RelativeAssetPathLayout<PathArenaT> MeasureRelativeAssetPathText(const ::Path<PathArenaT>& relativePath){
    using NativeStringView = typename ::Path<PathArenaT>::native_string_view;
    constexpr NativeStringView s_PathSeparatorChars{ FOUNDATION_TEXT("/\\") };

    RelativeAssetPathLayout<PathArenaT> layout{ relativePath.end() };
    CountingByteOutput counter;
    for(auto componentIt = relativePath.begin(); componentIt != relativePath.end(); ++componentIt){
        const auto component = componentIt.nativeComponent();
        if(component.empty() || IsDot(component))
            continue;
        if(IsDotDot(component) || component.find_first_of(s_PathSeparatorChars) != NativeStringView::npos){
            layout.acceptedEnd = componentIt;
            layout.byteCount = counter.byteCount;
            return layout;
        }

        if(counter.byteCount != 0u)
            counter.byteCount = AddSize(counter.byteCount, 1u);
        WriteConvertedText<char>(counter, component);
    }
    layout.byteCount = counter.byteCount;
    layout.accepted = counter.byteCount != 0u;
    return layout;
}

template<typename PathArenaT>
inline void WriteRelativeAssetPathText(const ::Path<PathArenaT>& relativePath, const RelativeAssetPathLayout<PathArenaT>& layout, const NotNull<char*> output)noexcept{
    char* cursor = output.get();
    b8 hasComponent = false;
    for(auto componentIt = relativePath.begin(); componentIt != layout.acceptedEnd; ++componentIt){
        const auto component = componentIt.nativeComponent();
        if(component.empty() || IsDot(component))
            continue;
        if(hasComponent)
            *cursor++ = '/';
        WriteConvertedText<char>(cursor, component);
        hasComponent = true;
    }
    FOUNDATION_ASSERT(static_cast<usize>(cursor - output.get()) == layout.byteCount);
    for(char* character = output.get(); character != cursor; ++character)
        *character = Canonicalize(*character);
}

template<typename StringT, typename PathArenaT>
[[nodiscard]] inline b8 BuildRelativeAssetPathText(const ::Path<PathArenaT>& relativePath, StringT& outRelativePath){
    outRelativePath.clear();
    const auto layout = MeasureRelativeAssetPathText(relativePath);
    outRelativePath.resize(layout.byteCount);
    WriteRelativeAssetPathText(relativePath, layout, MakeNotNull(outRelativePath.data()));
    return layout.accepted;
}

[[nodiscard]] Expected<ACompactString> ExtractAssetVirtualRoot(AStringView virtualPath, Alloc::ScratchArena& scratchArena);

template<typename ArenaT, typename RootArenaT, typename SourceArenaT>
[[nodiscard]] inline Expected<AString<ArenaT>> BuildDerivedAssetVirtualPathText(const ::Path<RootArenaT>& assetRoot, const AStringView virtualRoot, const ::Path<SourceArenaT>& sourceOrMetaPath, ArenaT& arena, Alloc::ScratchArena& scratchArena){
    AString<ArenaT> virtualPath{arena};
    usize virtualPathSize = 0u;
    for(const b8 writeOutput : { false, true }){
        {
            const ::Path<Alloc::ScratchArena> sourcePath(scratchArena, sourceOrMetaPath);
            const ::Path<Alloc::ScratchArena> rootPath(scratchArena, assetRoot);
            const ::Path<Alloc::ScratchArena> relativePath = sourcePath.lexicallyRelative(rootPath);
            if(relativePath.empty()){
                NWB_LOGGER_ERROR(FOUNDATION_TEXT("Assets: failed to derive asset path from '{}' relative to asset root '{}'"), PathToString<tchar>(sourceOrMetaPath), PathToString<tchar>(assetRoot));
                return MakeUnexpected(Failure{});
            }

            ::Path<Alloc::ScratchArena> logicalPath = relativePath;
            logicalPath.replaceExtension();
            const auto layout = MeasureRelativeAssetPathText(logicalPath);
            if(!layout.accepted){
                NWB_LOGGER_ERROR(FOUNDATION_TEXT("Assets: asset '{}' is not under asset root '{}'"), PathToString<tchar>(sourceOrMetaPath), PathToString<tchar>(assetRoot));
                return MakeUnexpected(Failure{});
            }

            if(writeOutput){
                FOUNDATION_ASSERT(virtualPathSize == virtualRoot.size() + 1u + layout.byteCount);
                WriteRelativeAssetPathText(logicalPath, layout, MakeNotNull(virtualPath.data() + virtualRoot.size() + 1u));
            }
            else{
                if(virtualRoot.size() > Limit<usize>::s_Max - 1u || layout.byteCount > Limit<usize>::s_Max - virtualRoot.size() - 1u){
                    NWB_LOGGER_ERROR(FOUNDATION_TEXT("Assets: derived asset virtual path size overflows for '{}'"), PathToString<tchar>(sourceOrMetaPath));
                    return MakeUnexpected(Failure{});
                }
                virtualPathSize = virtualRoot.size() + 1u + layout.byteCount;
            }
        }

        if(!writeOutput){
            virtualPath.resize(virtualPathSize);
            if(!virtualRoot.empty())
                FOUNDATION_MEMCPY(virtualPath.data(), virtualPathSize, virtualRoot.data(), virtualRoot.size());
            virtualPath[virtualRoot.size()] = '/';
        }
    }
    return virtualPath;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename RootArenaT, typename SourceArenaT>
[[nodiscard]] inline Expected<Name> BuildDerivedAssetVirtualPath(const ::Path<RootArenaT>& assetRoot, const AStringView virtualRoot, const ::Path<SourceArenaT>& sourceOrMetaPath, Alloc::ScratchArena& scratchArena){
    const auto virtualPathText = AssetPathsDetail::BuildDerivedAssetVirtualPathText(assetRoot, virtualRoot, sourceOrMetaPath, scratchArena, scratchArena);
    if(!virtualPathText)
        return MakeUnexpected(Failure{});

    const auto virtualPath = Name::FromText(AStringView(*virtualPathText));
    if(!virtualPath){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Assets: failed to derive asset name from '{}'"), PathToString<tchar>(sourceOrMetaPath));
        return MakeUnexpected(Failure{});
    }

    return *virtualPath;
}
[[nodiscard]] b8 HasReservedAssetVirtualRoot(AStringView virtualPath, Alloc::ScratchArena& scratchArena);
[[nodiscard]] Expected<Path> ResolveVirtualAssetPath(Span<const ResolvedAssetRoot> assetRoots, AStringView virtualPath, AssetArena& arena, Alloc::ScratchArena& scratchArena);
[[nodiscard]] Expected<AssetString> ResolvePairedSourcePathFromMetadata(const Path& nwbFilePath, AssetArena& arena, Alloc::ScratchArena& scratchArena);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

