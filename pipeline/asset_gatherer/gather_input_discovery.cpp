// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "gather_input_discovery.h"

#include <core/assets/input_list.h>
#include <core/assets/volume/built_asset.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSET_GATHERER_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Assets = Core::Assets;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Assets::AssetVector<Assets::AssetString>> AssetGatherInputDiscovery::Collect(const AssetGatherOptions& options, Core::Alloc::ScratchArena& scratchArena){
    Assets::AssetArena& arena = options.inputs.get_allocator().arena();
    Assets::AssetVector<Assets::AssetString> files(arena);
    files.reserve(options.inputs.size());
    for(const Assets::AssetString& input : options.inputs){
        const auto absolutePath = AbsolutePath(Path(arena, input));
        if(!absolutePath){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetGatherer: failed to resolve input '{}'"), ConvertString(input));
            return MakeUnexpected(Failure{});
        }
        const Path path = absolutePath->lexicallyNormal();
        const auto directory = IsDirectory(path);
        if(!directory){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetGatherer: failed to inspect input '{}'"), ConvertString(input));
            return MakeUnexpected(Failure{});
        }
        if(!*directory){
            files.emplace_back(PathToString(arena, path));
            continue;
        }

        const Path manifest = path / Assets::BuiltAssetDetail::s_ManifestFilename;
        const auto manifestExists = FileExists(manifest);
        if(!manifestExists){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetGatherer: failed to inspect build manifest '{}'"), PathToString<tchar>(manifest));
            return MakeUnexpected(Failure{});
        }
        if(*manifestExists){
            if(!Assets::ReadAssetInputList(manifest, scratchArena, files, true))
                return MakeUnexpected(Failure{});
            continue;
        }
        const auto entries = RecursiveDirectoryIterator<Assets::AssetArena>::Create(path);
        if(!entries){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetGatherer: failed to scan input '{}'"), ConvertString(input));
            return MakeUnexpected(Failure{});
        }
        for(const auto& entry : *entries){
            const auto regular = entry.isRegularFile();
            if(!regular){
                NWB_LOGGER_ERROR(NWB_TEXT("AssetGatherer: failed to inspect input entry '{}'"), PathToString<tchar>(entry.path()));
                return MakeUnexpected(Failure{});
            }
            if(*regular && PathToString(scratchArena, entry.path().extensionView()) == Assets::BuiltAssetDetail::s_Extension)
                files.emplace_back(PathToString(arena, entry.path()));
        }
    }
    Sort(files.begin(), files.end());
    files.erase(Unique(files.begin(), files.end()), files.end());
    return files;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSET_GATHERER_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

