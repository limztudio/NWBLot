// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset_file_discovery.h"
#include "cook_metadata_internal.h"

#include <core/task/cpu/scheduler.h>

#include <global/filesystem/operations.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_asset_file_discovery{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static b8 ScanAssetRoot(
    const ResolvedAssetRoot& assetRoot,
    const AStringView expectedExtension,
    CookArena& cookArena,
    ScratchArena& scratchArena,
    DiscoveredNwbFileVector& outFiles
){
    const auto directory = IsDirectory(assetRoot.path);
    if(!directory || !*directory){
        if(!directory){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetCook: failed to query asset root '{}': {}"), PathToString<tchar>(assetRoot.path), ConvertString(directory.error().message()));
            return false;
        }

        NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetCook: asset root is not a directory: '{}'"), PathToString<tchar>(assetRoot.path));
        return false;
    }

    const ::Path<ScratchArena> scratchRoot(scratchArena, assetRoot.path);
    const auto entries = RecursiveDirectoryIterator<ScratchArena>::Create(scratchRoot);
    if(!entries){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetCook: error scanning asset root '{}': {}"), PathToString<tchar>(assetRoot.path), ConvertString(entries.error().message()));
        return false;
    }

    outFiles.reserve(AddSize(outFiles.size(), static_cast<usize>(entries->end() - entries->begin())));
    for(const auto& dirEntry : *entries){
        const auto isRegularFile = dirEntry.isRegularFile();
        if(!isRegularFile){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetCook: failed to inspect '{}' while scanning '{}': {}"), PathToString<tchar>(dirEntry.path()), PathToString<tchar>(assetRoot.path), ConvertString(isRegularFile.error().message()));
            return false;
        }
        if(!*isRegularFile)
            continue;

        const ::Path<ScratchArena>& filePath = dirEntry.path();
        if(!AssetPathsDetail::CanonicalNativeTextEquals(filePath.extensionView(), expectedExtension, scratchArena))
            continue;

        ScratchString canonicalPath = PathToString(scratchArena, filePath.lexicallyNormal());
        CanonicalizeTextInPlace(canonicalPath);
        outFiles.emplace_back(cookArena, assetRoot.path, filePath, canonicalPath, assetRoot.virtualRoot);
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<DiscoveredNwbFileVector> AssetFileDiscovery::Discover(const Span<const ResolvedAssetRoot> assetRoots, const AStringView expectedExtension, CookArena& cookArena, ScratchArena& scratchArena){
    DiscoveredNwbFileVector files(cookArena);
    if(assetRoots.empty())
        return files;

    struct DiscoveredRootFiles final{
        DiscoveredNwbFileVector files;
        b8 ok = false;

        explicit DiscoveredRootFiles(CookArena& arena) : files(arena){}
    };

    Vector<DiscoveredRootFiles, CookArena> rootResults(cookArena);
    rootResults.reserve(assetRoots.size());
    for(usize i = 0u; i < assetRoots.size(); ++i)
        rootResults.emplace_back(cookArena);

    b8 scanned = false;
    Core::CpuTaskScheduler& scheduler = Core::SharedCpuTaskScheduler();
    if(assetRoots.size() > 1u && scheduler.isParallelEnabled()){
        const auto scanWork = scheduler.parallelFor(usize{ 0u }, assetRoots.size(), [&](const usize rootIndex){
            Alloc::ScratchArena workerScratch(scratchArena.allocationLog());
            rootResults[rootIndex].ok = __hidden_asset_file_discovery::ScanAssetRoot(assetRoots[rootIndex], expectedExtension, cookArena, workerScratch, rootResults[rootIndex].files);
        });
        scanned = scanWork.has_value();
        if(!scanned){
            for(DiscoveredRootFiles& entry : rootResults){
                entry.files.clear();
                entry.ok = false;
            }
        }
    }

    if(!scanned){
        for(usize rootIndex = 0u; rootIndex < assetRoots.size(); ++rootIndex){
            Alloc::ScratchArena::Scope rootScratchScope(scratchArena);
            rootResults[rootIndex].ok = __hidden_asset_file_discovery::ScanAssetRoot(assetRoots[rootIndex], expectedExtension, cookArena, scratchArena, rootResults[rootIndex].files);
            if(!rootResults[rootIndex].ok)
                return MakeUnexpected(Failure{});
        }
    }

    usize totalDiscovered = 0u;
    for(const DiscoveredRootFiles& entry : rootResults){
        if(!entry.ok)
            return MakeUnexpected(Failure{});
        totalDiscovered = AddSize(totalDiscovered, entry.files.size());
    }

    files.reserve(totalDiscovered);
    CookMetadataDetail::DiscoveredPathSet seenPaths(0, Hasher<AStringView>(), EqualTo<>(), scratchArena);
    seenPaths.reserve(totalDiscovered);
    for(DiscoveredRootFiles& entry : rootResults){
        for(DiscoveredNwbFile& file : entry.files){
            ScratchString normalizedPath = PathToString(scratchArena, file.filePath.lexicallyNormal());
#if defined(FOUNDATION_PLATFORM_WINDOWS)
            CanonicalizeTextInPlace(normalizedPath);
#endif
            if(!seenPaths.insert(Move(normalizedPath)).second)
                continue;
            files.push_back(Move(file));
        }
    }

    Sort(files.begin(), files.end(), [](const DiscoveredNwbFile& lhs, const DiscoveredNwbFile& rhs)noexcept{
        return lhs.normalizedPathText < rhs.normalizedPathText;
    });

    return files;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

