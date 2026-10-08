// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "gather.h"

#include <core/assets/input_list.h>
#include <core/assets/volume/asset_volume_writer.h>
#include <core/assets/volume/built_asset.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSET_GATHERER_BEGIN

static constexpr AStringView s_DefaultGatherConfig = "default";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Assets = Core::Assets;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gather{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<Assets::AssetVector<Assets::AssetString>> CollectBuiltFiles(const AssetGatherOptions& options){
    Assets::AssetArena& arena = options.inputs.get_allocator().arena();
    Assets::AssetVector<Assets::AssetString> files(arena);
    for(const Assets::AssetString& input : options.inputs){
        const auto absolutePath = AbsolutePath(Path(arena, input));
        if(!absolutePath){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetGatherer: failed to resolve input '{}'"), StringConvert(input));
            return MakeUnexpected(Failure{});
        }
        const Path path = absolutePath->lexicallyNormal();
        const auto directory = IsDirectory(path);
        if(!directory){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetGatherer: failed to inspect input '{}'"), StringConvert(input));
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
            if(!Assets::ReadAssetInputList(manifest, files, true))
                return MakeUnexpected(Failure{});
            continue;
        }
        const auto entries = RecursiveDirectoryIterator<Assets::AssetArena>::Create(path);
        if(!entries){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetGatherer: failed to scan input '{}'"), StringConvert(input));
            return MakeUnexpected(Failure{});
        }
        for(const auto& entry : *entries){
            const auto regular = entry.isRegularFile();
            if(!regular){
                NWB_LOGGER_ERROR(NWB_TEXT("AssetGatherer: failed to inspect input entry '{}'"), PathToString<tchar>(entry.path()));
                return MakeUnexpected(Failure{});
            }
            if(*regular && PathToString(arena, entry.path().extension()) == Assets::BuiltAssetDetail::s_Extension)
                files.emplace_back(PathToString(arena, entry.path()));
        }
    }
    Sort(files.begin(), files.end());
    files.erase(Unique(files.begin(), files.end()), files.end());
    return files;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GatherAssets(const AssetGatherOptions& options){
    Assets::AssetArena& arena = options.inputs.get_allocator().arena();
    Core::Alloc::ScratchArena scratchArena(Name("assets/gather"));
    if(options.outputDirectory.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetGatherer: output directory is empty"));
        return false;
    }

    const auto filesResult = __hidden_gather::CollectBuiltFiles(options);
    if(!filesResult)
        return false;
    const auto& files = *filesResult;
    Assets::AssetsVolumeCookDetail::AssetVolumePackManifest manifest(arena);
    if(!Assets::AssetsVolumeCookDetail::ReserveAssetVolumePackManifest(manifest, files.size()))
        return false;

    HashMap<Name, usize, Core::Alloc::ScratchArena> entryIndices(scratchArena);
    entryIndices.reserve(files.size());
    Assets::AssetBytes bytes(arena);
    for(const Assets::AssetString& file : files){
        const auto payload = Assets::BuiltAssetDetail::ReadBuiltAsset(Path(arena, file), bytes);
        if(!payload)
            return false;
        const Name& virtualPath = payload->virtualPath;
        const usize payloadOffset = payload->payloadOffset;
        const auto inserted = entryIndices.try_emplace(virtualPath, manifest.entries.size());
        if(!inserted.second){
            auto& existing = manifest.entries[inserted.first.value()];
            const usize payloadSize = bytes.size() - payloadOffset;
            const bool identical = existing.payloadBytes.size() == payloadSize
                && NWB_MEMCMP(existing.payloadBytes.data(), bytes.data() + payloadOffset, payloadSize) == 0
            ;
            if(!identical){
                if(
                    !options.mergePayloads
                    || !options.mergePayloads(virtualPath, existing.payloadBytes, bytes.data() + payloadOffset, payloadSize)
                ){
                    NWB_LOGGER_ERROR(NWB_TEXT("AssetGatherer: conflicting built asset identity '{}'"), StringConvert(virtualPath.resolvedText()));
                    return false;
                }
                existing.identity.payloadSize = existing.payloadBytes.size();
                existing.identity.payloadHash = ComputeFnv64Bytes(existing.payloadBytes.data(), existing.payloadBytes.size());
            }
            --manifest.plannedFileCount;
            continue;
        }
        if(!Assets::AssetsVolumeCookDetail::AppendPayloadBytesToManifest(manifest, virtualPath, static_cast<const void*>(bytes.data() + payloadOffset), bytes.size() - payloadOffset, 0u))
            return false;
    }

    Assets::ResolvedCookPaths paths(arena);
    const auto outputDirectory = AbsolutePath(Path(arena, options.outputDirectory));
    if(!outputDirectory){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetGatherer: failed to resolve output '{}'"), StringConvert(options.outputDirectory));
        return false;
    }
    paths.outputDirectory = outputDirectory->lexicallyNormal();
    Assets::CookString configuration = BuildCanonicalSafeCacheName(arena, options.configuration.view());
    if(configuration.empty())
        configuration = s_DefaultGatherConfig;

    const auto result = Assets::AssetsVolumeCookDetail::WriteAssetVolume(arena, paths, configuration, manifest, scratchArena);
    if(!result)
        return false;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("AssetGatherer: gathered {} assets into '{}'"), result->fileCount, StringConvert(options.outputDirectory));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSET_GATHERER_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

