// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


#include "asset_file_discovery.h"
#include "cook_metadata_internal.h"

#include <global/filesystem/operations.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


Expected<DiscoveredNwbFileVector> AssetFileDiscovery::Discover(const Span<const ResolvedAssetRoot> assetRoots, const AStringView expectedExtension, CookArena& cookArena, ScratchArena& scratchArena){
    DiscoveredNwbFileVector files(cookArena);
    CookMetadataDetail::DiscoveredPathSet seenPaths(0, Hasher<AStringView>(), EqualTo<>(), scratchArena);

    for(const ResolvedAssetRoot& assetRoot : assetRoots){
        const auto directory = IsDirectory(assetRoot.path);
        if(!directory || !*directory){
            if(!directory){
                NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetCook: failed to query asset root '{}': {}"), PathToString<tchar>(assetRoot.path), ConvertString(directory.error().message()));
                return MakeUnexpected(Failure{});
            }

            NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetCook: asset root is not a directory: '{}'"), PathToString<tchar>(assetRoot.path));
            return MakeUnexpected(Failure{});
        }

        const ::Path<ScratchArena> scratchRoot(scratchArena, assetRoot.path);
        const auto entries = RecursiveDirectoryIterator<ScratchArena>::Create(scratchRoot);
        if(!entries){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetCook: error scanning asset root '{}': {}"), PathToString<tchar>(assetRoot.path), ConvertString(entries.error().message()));
            return MakeUnexpected(Failure{});
        }
        files.reserve(AddSize(files.size(), static_cast<usize>(entries->end() - entries->begin())));
        seenPaths.reserve(AddSize(seenPaths.size(), static_cast<usize>(entries->end() - entries->begin())));
        for(const auto& dirEntry : *entries){
            const auto isRegularFile = dirEntry.isRegularFile();
            if(!isRegularFile){
                NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetCook: failed to inspect '{}' while scanning '{}': {}"), PathToString<tchar>(dirEntry.path()), PathToString<tchar>(assetRoot.path), ConvertString(isRegularFile.error().message()));
                return MakeUnexpected(Failure{});
            }
            if(!*isRegularFile)
                continue;

            const ::Path<ScratchArena>& filePath = dirEntry.path();
            if(!AssetPathsDetail::CanonicalNativeTextEquals(filePath.extensionView(), expectedExtension, scratchArena))
                continue;

            ScratchString normalizedPath = PathToString(scratchArena, filePath.lexicallyNormal());
#if defined(FOUNDATION_PLATFORM_WINDOWS)
            CanonicalizeTextInPlace(normalizedPath);
#endif
            const auto inserted = seenPaths.insert(Move(normalizedPath));
            if(!inserted.second)
                continue;
#if !defined(FOUNDATION_PLATFORM_WINDOWS)
            ScratchString canonicalPath(*inserted.first, scratchArena);
            CanonicalizeTextInPlace(canonicalPath);
            files.emplace_back(cookArena, assetRoot.path, filePath, canonicalPath, assetRoot.virtualRoot);
#else
            files.emplace_back(cookArena, assetRoot.path, filePath, *inserted.first, assetRoot.virtualRoot);
#endif
        }
    }

    Sort(files.begin(), files.end(), [](const DiscoveredNwbFile& lhs, const DiscoveredNwbFile& rhs)noexcept{
        return lhs.normalizedPathText < rhs.normalizedPathText;
    });

    return files;
}


NWB_ASSETS_END


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

