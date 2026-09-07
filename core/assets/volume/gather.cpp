// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "gather.h"
#include "built_asset.h"
#include "asset_volume_writer.h"

#include <core/assets/input_list.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gather{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool CollectBuiltFiles(const AssetGatherOptions& options, AssetVector<AssetString>& files){
    AssetArena& arena = files.get_allocator().arena();
    for(const AssetString& input : options.inputs){
        ErrorCode error;
        const Path path = AbsolutePath(Path(arena, input), error).lexically_normal();
        if(error){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetGatherer: failed to resolve input '{}'"), StringConvert(input));
            return false;
        }
        const bool directory = IsDirectory(path, error);
        if(error){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetGatherer: failed to inspect input '{}'"), StringConvert(input));
            return false;
        }
        if(!directory){
            files.emplace_back(PathToString(arena, path));
            continue;
        }

        const Path manifest = path / BuiltAssetDetail::s_ManifestFilename;
        if(FileExists(manifest, error)){
            if(!ReadAssetInputList(manifest, files, true))
                return false;
            continue;
        }
        if(error){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetGatherer: failed to inspect build manifest '{}'"), PathToString<tchar>(manifest));
            return false;
        }
        for(const auto& entry : RecursiveDirectoryIterator(path, error)){
            if(error){
                NWB_LOGGER_ERROR(NWB_TEXT("AssetGatherer: failed to scan input '{}'"), StringConvert(input));
                return false;
            }
            if(entry.is_regular_file(error) && PathToString(arena, entry.path().extension()) == BuiltAssetDetail::s_Extension)
                files.emplace_back(PathToString(arena, entry.path()));
            if(error){
                NWB_LOGGER_ERROR(NWB_TEXT("AssetGatherer: failed to inspect input entry '{}'"), PathToString<tchar>(entry.path()));
                return false;
            }
        }
        if(error){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetGatherer: failed to scan input '{}'"), StringConvert(input));
            return false;
        }
    }
    Sort(files.begin(), files.end());
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GatherAssets(const AssetGatherOptions& options){
    AssetArena& arena = options.inputs.get_allocator().arena();
    Alloc::ScratchArena scratchArena(Name("assets/gather"));
    if(options.outputDirectory.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetGatherer: output directory is empty"));
        return false;
    }

    AssetVector<AssetString> files(arena);
    if(!__hidden_gather::CollectBuiltFiles(options, files))
        return false;
    AssetsVolumeCookDetail::AssetVolumePackManifest manifest(arena);
    if(!AssetsVolumeCookDetail::ReserveAssetVolumePackManifest(manifest, files.size()))
        return false;

    CookEntryPathHashSet seenPaths(arena);
    seenPaths.reserve(files.size());
    AssetBytes bytes(arena);
    for(const AssetString& file : files){
        Name virtualPath;
        usize payloadOffset = 0u;
        if(!BuiltAssetDetail::ReadBuiltAsset(Path(arena, file), bytes, virtualPath, payloadOffset))
            return false;
        if(!seenPaths.insert(virtualPath.hash()).second){
            const auto existing = FindIf(manifest.entries.begin(), manifest.entries.end(), [&virtualPath](const auto& entry){ return entry.virtualPath == virtualPath; });
            const usize payloadSize = bytes.size() - payloadOffset;
            const bool identical = existing != manifest.entries.end()
                && existing->payloadBytes.size() == payloadSize
                && NWB_MEMCMP(existing->payloadBytes.data(), bytes.data() + payloadOffset, payloadSize) == 0
            ;
            if(!identical && (existing == manifest.entries.end() || !options.mergePayloads
                || !options.mergePayloads(virtualPath, existing->payloadBytes, bytes.data() + payloadOffset, payloadSize))){
                NWB_LOGGER_ERROR(NWB_TEXT("AssetGatherer: conflicting built asset identity '{}'"), StringConvert(virtualPath.c_str()));
                return false;
            }
            existing->identity.payloadSize = existing->payloadBytes.size();
            existing->identity.payloadHash = ComputeFnv64Bytes(existing->payloadBytes.data(), existing->payloadBytes.size());
            --manifest.plannedFileCount;
            continue;
        }
        if(!AssetsVolumeCookDetail::AppendPayloadBytesToManifest(manifest, virtualPath, static_cast<const void*>(bytes.data() + payloadOffset), bytes.size() - payloadOffset, 0u))
            return false;
    }

    ResolvedCookPaths paths(arena);
    ErrorCode error;
    paths.outputDirectory = AbsolutePath(Path(arena, options.outputDirectory), error).lexically_normal();
    if(error){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetGatherer: failed to resolve output '{}'"), StringConvert(options.outputDirectory));
        return false;
    }
    CookString configuration = BuildCanonicalSafeCacheName(arena, options.configuration.view());
    if(configuration.empty())
        configuration = "default";

    AssetsVolumeCookDetail::AssetVolumeWriteResult result;
    if(!AssetsVolumeCookDetail::WriteAssetVolume(arena, paths, configuration, manifest, result, scratchArena))
        return false;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("AssetGatherer: gathered {} assets into '{}'"), result.fileCount, StringConvert(options.outputDirectory));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

