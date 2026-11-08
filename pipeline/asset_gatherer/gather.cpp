// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "gather.h"
#include "gather_input_discovery.h"

#include <core/assets/volume/asset_volume_writer.h>
#include <core/assets/volume/built_asset.h>
#include <core/common/log.h>

#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSET_GATHERER_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Assets = Core::Assets;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 GatherAssets(const AssetGatherOptions& options, Core::Alloc::ScratchArena& scratchArena){
    Assets::AssetArena& arena = options.inputs.get_allocator().arena();
    if(options.outputDirectory.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetGatherer: output directory is empty"));
        return false;
    }

    const auto filesResult = AssetGatherInputDiscovery::Collect(options, scratchArena);
    if(!filesResult)
        return false;
    const auto& files = *filesResult;
    Assets::AssetsVolumeCookDetail::AssetVolumePackManifest manifest(arena);
    if(!Assets::AssetsVolumeCookDetail::ReserveAssetVolumePackManifest(files.size(), manifest))
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
        const Span<const u8> payloadBytes = Span<const u8>(bytes).subspan(payloadOffset);
        const auto inserted = entryIndices.try_emplace(virtualPath, manifest.entries.size());
        if(!inserted.second){
            auto& existing = manifest.entries[inserted.first.value()];
            const b8 identical = AreGatheredAssetPayloadsIdentical(existing.payloadBytes, payloadBytes);
            if(!identical){
                if(!options.mergePayloads || !options.mergePayloads(virtualPath, payloadBytes, scratchArena, existing.payloadBytes)){
                    NWB_LOGGER_ERROR(NWB_TEXT("AssetGatherer: conflicting built asset identity '{}'"), ConvertString(virtualPath.resolvedText()));
                    return false;
                }
                existing.identity.payloadSize = existing.payloadBytes.size();
                existing.identity.payloadHash = ComputeFnv64Bytes(existing.payloadBytes.data(), existing.payloadBytes.size());
            }
            --manifest.plannedFileCount;
            continue;
        }
        if(!Assets::AssetsVolumeCookDetail::AppendPayloadBytesToManifest(virtualPath, payloadBytes, manifest, 0u))
            return false;
    }

    Assets::ResolvedCookPaths paths(arena);
    const auto outputDirectory = AbsolutePath(Path(arena, options.outputDirectory));
    if(!outputDirectory){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetGatherer: failed to resolve output '{}'"), ConvertString(options.outputDirectory));
        return false;
    }
    paths.outputDirectory = outputDirectory->lexicallyNormal();
    Assets::CookString configuration = BuildCanonicalSafeCacheName(arena, options.configuration.view());
    if(configuration.empty())
        configuration = s_DefaultGatherConfig;

    const auto result = Assets::AssetsVolumeCookDetail::WriteAssetVolume(paths, configuration, manifest, arena, scratchArena);
    if(!result)
        return false;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("AssetGatherer: gathered {} assets into '{}'"), result->fileCount, ConvertString(options.outputDirectory));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSET_GATHERER_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

