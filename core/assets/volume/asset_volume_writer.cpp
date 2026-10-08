// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset_volume_writer.h"

#include "cooked_object_cache.h"
#include "cook_paths.h"

#include <core/filesystem/volume_build.h>
#include <core/filesystem/volume_file_system.h>
#include <core/filesystem/volume_staging.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_asset_volume_writer{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ScratchArena = AssetsVolumeCookDetail::ScratchArena;

static constexpr u64 s_DefaultSegmentSize = 512ull * 1024ull * 1024ull;
static constexpr u64 s_DefaultMetadataSize = 512ull * 1024ull;
static constexpr u64 s_SegmentGrowthFactor = 2ull;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<u64> EstimateRequiredMetadataBytes(const u64 fileCount){
    if(fileCount == 0)
        return s_DefaultMetadataSize;

    const auto metadataBytes = Core::Filesystem::ComputeVolumeMetadataRequirement(fileCount);
    if(!metadataBytes)
        return MakeUnexpected(Failure{});
    u64 totalBytes = *metadataBytes;

    constexpr u64 s_MetadataPaddingBytes = 4ull * 1024ull;
    if(totalBytes <= Limit<u64>::s_Max - s_MetadataPaddingBytes)
        totalBytes += s_MetadataPaddingBytes;

    return Max(totalBytes, s_DefaultMetadataSize);
}

static Expected<Core::Filesystem::VolumeBuildConfig> ConfigureVolumeSizing(const u64 plannedFileCount){
    Core::Filesystem::VolumeBuildConfig config;
    if(!config.volumeName.assign(AssetsVolumeCookDetail::s_AssetVolumeName)){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetGatherer: volume name '{}' exceeds ACompactString capacity"), StringConvert(AssetsVolumeCookDetail::s_AssetVolumeName));
        return MakeUnexpected(Failure{});
    }
    const auto metadataSize = EstimateRequiredMetadataBytes(plannedFileCount);
    if(!metadataSize || *metadataSize == Limit<u64>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetGatherer: metadata size overflow while planning volume"));
        return MakeUnexpected(Failure{});
    }

    config.metadataSize = *metadataSize;
    config.segmentSize = s_DefaultSegmentSize;
    while(config.segmentSize <= config.metadataSize){
        if(config.segmentSize > Limit<u64>::s_Max / s_SegmentGrowthFactor){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetGatherer: segment size overflow while planning volume"));
            return MakeUnexpected(Failure{});
        }
        config.segmentSize *= s_SegmentGrowthFactor;
    }

    return config;
}

static bool PushManifestObjectFilePayloadToVolume(
    const AssetsVolumeCookDetail::AssetVolumePackEntry& entry,
    Core::Assets::AssetBytes& objectBytes,
    Core::Filesystem::IFilesystem& filesystem
){
    const auto payload = AssetsVolumeCookDetail::ReadCookedObjectPayload(entry.objectPath, entry.virtualPath, objectBytes);
    if(!payload)
        return false;
    if(
        payload->identity.payloadSize != entry.identity.payloadSize
        || payload->identity.payloadHash != entry.identity.payloadHash
        || payload->identity.cookKeyHash != entry.identity.cookKeyHash
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetGatherer: object cache identity mismatch '{}' for '{}'")
            , PathToString<tchar>(entry.objectPath)
            , StringConvert(entry.virtualPath.resolvedText())
        );
        return false;
    }

    if(filesystem.writeFileDeferred(entry.virtualPath, payload->data, payload->size))
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("AssetGatherer: failed to push cached asset '{}'"), StringConvert(entry.virtualPath.resolvedText()));
    return false;
}

static bool PushManifestEntryToVolume(
    const AssetsVolumeCookDetail::AssetVolumePackEntry& entry,
    Core::Assets::AssetBytes& objectBytes,
    Core::Filesystem::IFilesystem& filesystem
){
    switch(entry.source){
    case AssetsVolumeCookDetail::AssetVolumePackEntrySource::PayloadBytes:
        if(
            entry.identity.payloadSize != static_cast<u64>(entry.payloadBytes.size())
            || entry.identity.payloadHash != ComputeFnv64Bytes(entry.payloadBytes.data(), entry.payloadBytes.size())
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetGatherer: manifest payload identity mismatch '{}'"), StringConvert(entry.virtualPath.resolvedText()));
            return false;
        }
        if(filesystem.writeFileDeferred(entry.virtualPath, entry.payloadBytes))
            return true;

        NWB_LOGGER_ERROR(NWB_TEXT("AssetGatherer: failed to push manifest payload '{}'"), StringConvert(entry.virtualPath.resolvedText()));
        return false;
    case AssetsVolumeCookDetail::AssetVolumePackEntrySource::ObjectFilePayload:
        return PushManifestObjectFilePayloadToVolume(entry, objectBytes, filesystem);
    default:
        break;
    }

    NWB_LOGGER_ERROR(NWB_TEXT("AssetGatherer: manifest entry '{}' has an unknown source"), StringConvert(entry.virtualPath.resolvedText()));
    return false;
}

static bool PushManifestToVolume(
    Core::Assets::AssetArena& arena,
    const AssetsVolumeCookDetail::AssetVolumePackManifest& manifest,
    Core::Filesystem::IFilesystem& filesystem
){
    Core::Assets::AssetBytes objectBytes(arena);
    for(const AssetsVolumeCookDetail::AssetVolumePackEntry& entry : manifest.entries){
        if(!PushManifestEntryToVolume(entry, objectBytes, filesystem))
            return false;
    }

    return true;
}

static bool ValidateManifestEntryCount(const AssetsVolumeCookDetail::AssetVolumePackManifest& manifest){
    if(static_cast<u64>(manifest.entries.size()) == manifest.plannedFileCount)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("AssetGatherer: manifest file count mismatch, planned {} but produced {}")
        , manifest.plannedFileCount
        , manifest.entries.size()
    );
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsVolumeCookDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<AssetVolumeWriteResult> WriteAssetVolume(
    Core::Alloc::GlobalArena& arena,
    const ResolvedCookPaths& resolvedPaths,
    const AStringView configurationSafeName,
    const AssetVolumePackManifest& manifest,
    ScratchArena& scratchArena
){
    AssetVolumeWriteResult result;

    if(!__hidden_asset_volume_writer::ValidateManifestEntryCount(manifest))
        return MakeUnexpected(Failure{});

    const auto volumeConfig = __hidden_asset_volume_writer::ConfigureVolumeSizing(manifest.plannedFileCount);
    if(!volumeConfig)
        return MakeUnexpected(Failure{});

    const StagedVolumePaths stagedVolumePaths = BuildStagedVolumePaths(
        resolvedPaths.outputDirectory,
        volumeConfig->volumeName,
        configurationSafeName,
        scratchArena
    );
    if(!Core::Filesystem::EnsureEmptyStagedDirectory(
        stagedVolumePaths.stageDirectory,
        s_AssetGathererLogPrefix,
        "stage directory"
    ))
        return MakeUnexpected(Failure{});
    Core::Filesystem::StagedDirectoryCleanupGuard stageDirectoryCleanup(
        stagedVolumePaths.stageDirectory,
        s_AssetGathererLogPrefix
    );
    if(!Core::Filesystem::RemoveStagedDirectoryIfPresent(
        stagedVolumePaths.backupDirectory,
        s_AssetGathererLogPrefix,
        "backup directory"
    ))
        return MakeUnexpected(Failure{});

    u64 stagedFileCount = 0;
    usize stagedSegmentCount = 0;
    {
        Core::Filesystem::VolumeFileSystem volumeStorage(arena);
        Core::Filesystem::IFilesystem& filesystem = volumeStorage;
        Core::Filesystem::VolumeMountDesc mountDesc(arena);
        mountDesc.volumeName = volumeConfig->volumeName;
        mountDesc.mountDirectory = stagedVolumePaths.stageDirectory;
        mountDesc.segmentSize = volumeConfig->segmentSize;
        mountDesc.metadataSize = volumeConfig->metadataSize;
        mountDesc.createIfMissing = true;
        mountDesc.usage = Core::Filesystem::VolumeUsage::CookWrite;
        if(!filesystem.mount(mountDesc)){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetGatherer: failed to mount staged volume filesystem"));
            return MakeUnexpected(Failure{});
        }

        if(!__hidden_asset_volume_writer::PushManifestToVolume(arena, manifest, filesystem))
            return MakeUnexpected(Failure{});
        if(!filesystem.flush()){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetGatherer: failed to flush staged volume metadata"));
            return MakeUnexpected(Failure{});
        }

        stagedFileCount = filesystem.fileCount();
        stagedSegmentCount = volumeStorage.segmentCount();
        if(!filesystem.unmount())
            return MakeUnexpected(Failure{});
    }

    if(!Core::Filesystem::PublishStagedVolume(stagedVolumePaths, resolvedPaths.outputDirectory, volumeConfig->volumeName, stagedSegmentCount))
        return MakeUnexpected(Failure{});
    stageDirectoryCleanup.dismiss();

    if(!result.volumeName.assign(s_AssetVolumeName)){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetGatherer: volume name exceeds ACompactString capacity"));
        return MakeUnexpected(Failure{});
    }
    result.fileCount = stagedFileCount;
    result.segmentCount = static_cast<u64>(stagedSegmentCount);
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

