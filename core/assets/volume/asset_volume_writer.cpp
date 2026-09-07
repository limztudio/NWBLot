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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static u64 EstimateRequiredMetadataBytes(const u64 fileCount){
    if(fileCount == 0)
        return s_DefaultMetadataSize;

    u64 totalBytes = 0;
    if(!Core::Filesystem::ComputeVolumeMetadataRequirement(fileCount, totalBytes))
        return Limit<u64>::s_Max;

    constexpr u64 s_MetadataPaddingBytes = 4ull * 1024ull;
    if(totalBytes <= Limit<u64>::s_Max - s_MetadataPaddingBytes)
        totalBytes += s_MetadataPaddingBytes;

    return Max(totalBytes, s_DefaultMetadataSize);
}

static bool ConfigureVolumeSizing(const u64 plannedFileCount, Core::Filesystem::VolumeBuildConfig& outConfig){
    if(!outConfig.volumeName.assign(AssetsVolumeCookDetail::s_AssetVolumeName)){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: volume name '{}' exceeds ACompactString capacity"), StringConvert(AssetsVolumeCookDetail::s_AssetVolumeName));
        return false;
    }
    outConfig.metadataSize = EstimateRequiredMetadataBytes(plannedFileCount);
    if(outConfig.metadataSize == Limit<u64>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: metadata size overflow while planning volume"));
        return false;
    }

    outConfig.segmentSize = s_DefaultSegmentSize;
    while(outConfig.segmentSize <= outConfig.metadataSize){
        if(outConfig.segmentSize > Limit<u64>::s_Max / 2ull){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: segment size overflow while planning volume"));
            return false;
        }
        outConfig.segmentSize *= 2ull;
    }

    return true;
}

static bool PushManifestObjectFilePayloadToVolume(
    const AssetsVolumeCookDetail::AssetVolumePackEntry& entry,
    Core::Assets::AssetBytes& objectBytes,
    Core::Filesystem::IFilesystem& filesystem
){
    AssetsVolumeCookDetail::CookedObjectPayloadView payload;
    if(!AssetsVolumeCookDetail::ReadCookedObjectPayload(entry.objectPath, entry.virtualPath, objectBytes, payload))
        return false;
    if(
        payload.identity.payloadSize != entry.identity.payloadSize
        || payload.identity.payloadHash != entry.identity.payloadHash
        || payload.identity.cookKeyHash != entry.identity.cookKeyHash
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: object cache identity mismatch '{}' for '{}'")
            , PathToString<tchar>(entry.objectPath)
            , StringConvert(entry.virtualPath.c_str())
        );
        return false;
    }

    if(filesystem.writeFileDeferred(entry.virtualPath, payload.data, payload.size))
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: failed to push cached asset '{}'"), StringConvert(entry.virtualPath.c_str()));
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
            NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: manifest payload identity mismatch '{}'"), StringConvert(entry.virtualPath.c_str()));
            return false;
        }
        if(filesystem.writeFileDeferred(entry.virtualPath, entry.payloadBytes))
            return true;

        NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: failed to push manifest payload '{}'"), StringConvert(entry.virtualPath.c_str()));
        return false;
    case AssetsVolumeCookDetail::AssetVolumePackEntrySource::ObjectFilePayload:
        return PushManifestObjectFilePayloadToVolume(entry, objectBytes, filesystem);
    default:
        break;
    }

    NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: manifest entry '{}' has an unknown source"), StringConvert(entry.virtualPath.c_str()));
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

    NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: manifest file count mismatch, planned {} but produced {}")
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


bool WriteAssetVolume(
    Core::Alloc::GlobalArena& arena,
    const ResolvedCookPaths& resolvedPaths,
    const AStringView configurationSafeName,
    const AssetVolumePackManifest& manifest,
    AssetVolumeWriteResult& outResult,
    ScratchArena& scratchArena
){
    outResult = {};

    if(!__hidden_asset_volume_writer::ValidateManifestEntryCount(manifest))
        return false;

    Core::Filesystem::VolumeBuildConfig volumeConfig;
    if(!__hidden_asset_volume_writer::ConfigureVolumeSizing(manifest.plannedFileCount, volumeConfig))
        return false;

    const StagedVolumePaths stagedVolumePaths = BuildStagedVolumePaths(
        resolvedPaths.outputDirectory,
        volumeConfig.volumeName,
        configurationSafeName,
        scratchArena
    );
    if(!Core::Filesystem::EnsureEmptyStagedDirectory(
        stagedVolumePaths.stageDirectory,
        s_AssetVolumeCookerLogPrefix,
        "stage directory"
    ))
        return false;
    Core::Filesystem::StagedDirectoryCleanupGuard stageDirectoryCleanup(
        stagedVolumePaths.stageDirectory,
        s_AssetVolumeCookerLogPrefix
    );
    if(!Core::Filesystem::RemoveStagedDirectoryIfPresent(
        stagedVolumePaths.backupDirectory,
        s_AssetVolumeCookerLogPrefix,
        "backup directory"
    ))
        return false;

    u64 stagedFileCount = 0;
    usize stagedSegmentCount = 0;
    {
        Core::Filesystem::VolumeFileSystem volumeStorage(arena);
        Core::Filesystem::IFilesystem& filesystem = volumeStorage;
        Core::Filesystem::VolumeMountDesc mountDesc(arena);
        mountDesc.volumeName = volumeConfig.volumeName;
        mountDesc.mountDirectory = stagedVolumePaths.stageDirectory;
        mountDesc.segmentSize = volumeConfig.segmentSize;
        mountDesc.metadataSize = volumeConfig.metadataSize;
        mountDesc.createIfMissing = true;
        mountDesc.usage = Core::Filesystem::VolumeUsage::CookWrite;
        if(!filesystem.mountVolume(mountDesc)){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: failed to mount staged volume filesystem"));
            return false;
        }

        if(!__hidden_asset_volume_writer::PushManifestToVolume(arena, manifest, filesystem))
            return false;
        if(!filesystem.flush()){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: failed to flush staged volume metadata"));
            return false;
        }

        stagedFileCount = filesystem.fileCount();
        stagedSegmentCount = volumeStorage.segmentCount();
        if(!filesystem.unmountVolume())
            return false;
    }

    if(!Core::Filesystem::PublishStagedVolume(stagedVolumePaths, resolvedPaths.outputDirectory, volumeConfig.volumeName, stagedSegmentCount))
        return false;
    stageDirectoryCleanup.dismiss();

    if(!outResult.volumeName.assign(s_AssetVolumeName)){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: volume name exceeds ACompactString capacity"));
        return false;
    }
    outResult.fileCount = stagedFileCount;
    outResult.segmentCount = static_cast<u64>(stagedSegmentCount);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

