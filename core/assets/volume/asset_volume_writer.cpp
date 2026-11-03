// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset_volume_writer.h"

#include <core/filesystem/volume_file_system.h>
#include <core/filesystem/volume_staging.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_asset_volume_writer{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr u64 s_DefaultSegmentSize = 512ull * 1024ull * 1024ull;
static constexpr u64 s_DefaultMetadataSize = 512ull * 1024ull;
static constexpr u64 s_SegmentGrowthFactor = 2ull;
static constexpr char s_StagedVolumePathTokenSeparator = '_';
static constexpr usize s_StagedVolumePathTokenSeparatorCount = 2u;
static constexpr usize s_StagedVolumePathHashDigits = sizeof(u64) * 2u;

static_assert(AssetsVolumeCookDetail::s_AssetVolumeName.size() <= ACompactString::s_MaxLength, "asset volume name must fit ACompactString");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Core::Filesystem::StagedDirectoryPaths BuildStagedVolumePaths(const Path& outputDirectory, const AStringView volumeName, const AStringView configurationSafeName, ScratchArena& scratchArena){
    const ScratchString volumeSafeName = BuildCanonicalSafeCacheName(scratchArena, volumeName);
    ScratchString stageToken{scratchArena};
    stageToken.reserve(volumeSafeName.size() + configurationSafeName.size() + s_StagedVolumePathTokenSeparatorCount + s_StagedVolumePathHashDigits);
    stageToken += volumeSafeName;
    stageToken += s_StagedVolumePathTokenSeparator;
    stageToken += configurationSafeName;
    stageToken += s_StagedVolumePathTokenSeparator;
    AppendHexU64(ComputeFnv64Text(PathToString(scratchArena, outputDirectory)), stageToken);
    return BuildStagedDirectoryPaths(scratchArena, outputDirectory, stageToken);
}

static Expected<u64> EstimateRequiredMetadataBytes(const u64 fileCount)noexcept{
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
    config.volumeName = ACompactString(AssetsVolumeCookDetail::s_AssetVolumeName);
    const auto metadataSize = EstimateRequiredMetadataBytes(plannedFileCount);
    if(!metadataSize || *metadataSize == Limit<u64>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetVolume: metadata size overflow while planning volume"));
        return MakeUnexpected(Failure{});
    }

    config.metadataSize = *metadataSize;
    config.segmentSize = s_DefaultSegmentSize;
    while(config.segmentSize <= config.metadataSize){
        if(config.segmentSize > Limit<u64>::s_Max / s_SegmentGrowthFactor){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetVolume: segment size overflow while planning volume"));
            return MakeUnexpected(Failure{});
        }
        config.segmentSize *= s_SegmentGrowthFactor;
    }

    return config;
}

static b8 PushManifestEntryToVolume(const AssetsVolumeCookDetail::AssetVolumePackEntry& entry, Core::Filesystem::IFilesystem& filesystem){
    if(entry.identity.cookKeyHash == 0u || entry.identity.payloadSize != static_cast<u64>(entry.payloadBytes.size()) || entry.identity.payloadHash != ComputeFnv64Bytes(entry.payloadBytes.data(), entry.payloadBytes.size())){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetVolume: manifest payload identity mismatch '{}'"), ConvertString(entry.virtualPath.resolvedText()));
        return false;
    }
    if(filesystem.writeFileDeferred(entry.virtualPath, entry.payloadBytes))
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("AssetVolume: failed to push manifest payload '{}'"), ConvertString(entry.virtualPath.resolvedText()));
    return false;
}

static b8 PushManifestToVolume(const AssetsVolumeCookDetail::AssetVolumePackManifest& manifest, Core::Filesystem::IFilesystem& filesystem){
    for(const AssetsVolumeCookDetail::AssetVolumePackEntry& entry : manifest.entries){
        if(!PushManifestEntryToVolume(entry, filesystem))
            return false;
    }

    return true;
}

static b8 ValidateManifestEntryCount(const AssetsVolumeCookDetail::AssetVolumePackManifest& manifest){
    if(static_cast<u64>(manifest.entries.size()) == manifest.plannedFileCount)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("AssetVolume: manifest file count mismatch, planned {} but produced {}"), manifest.plannedFileCount, manifest.entries.size());
    return false;
}

static b8 ValidateUniqueManifestPaths(const AssetsVolumeCookDetail::AssetVolumePackManifest& manifest, ScratchArena& scratchArena){
    HashSet<Name, ScratchArena> paths(scratchArena);
    paths.reserve(manifest.entries.size());
    for(const auto& entry : manifest.entries){
        if(!paths.insert(entry.virtualPath).second){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetVolume: duplicate manifest identity '{}'"), ConvertString(entry.virtualPath.resolvedText()));
            return false;
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsVolumeCookDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<AssetVolumeWriteResult> WriteAssetVolume(const ResolvedCookPaths& resolvedPaths, const AStringView configurationSafeName, const AssetVolumePackManifest& manifest, Core::Alloc::GlobalArena& arena, ScratchArena& scratchArena){
    AssetVolumeWriteResult result;

    if(!__hidden_asset_volume_writer::ValidateManifestEntryCount(manifest) || !__hidden_asset_volume_writer::ValidateUniqueManifestPaths(manifest, scratchArena))
        return MakeUnexpected(Failure{});

    const auto volumeConfig = __hidden_asset_volume_writer::ConfigureVolumeSizing(manifest.plannedFileCount);
    if(!volumeConfig)
        return MakeUnexpected(Failure{});

    const Core::Filesystem::StagedDirectoryPaths stagedVolumePaths = __hidden_asset_volume_writer::BuildStagedVolumePaths(resolvedPaths.outputDirectory, volumeConfig->volumeName, configurationSafeName, scratchArena);
    if(!Core::Filesystem::CreateNewStagedDirectory(stagedVolumePaths.backupDirectory, s_AssetVolumeLogPrefix, Core::Filesystem::s_BackupDirectoryLabel))
        return MakeUnexpected(Failure{});
    Core::Filesystem::StagedDirectoryCleanupGuard backupDirectoryCleanup(stagedVolumePaths.backupDirectory, s_AssetVolumeLogPrefix, Core::Filesystem::s_BackupDirectoryLabel);
    if(!Core::Filesystem::CreateNewStagedDirectory(stagedVolumePaths.stageDirectory, s_AssetVolumeLogPrefix, Core::Filesystem::s_StageDirectoryLabel))
        return MakeUnexpected(Failure{});
    Core::Filesystem::StagedDirectoryCleanupGuard stageDirectoryCleanup(stagedVolumePaths.stageDirectory, s_AssetVolumeLogPrefix);

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
            NWB_LOGGER_ERROR(NWB_TEXT("AssetVolume: failed to mount staged volume filesystem"));
            return MakeUnexpected(Failure{});
        }

        if(!__hidden_asset_volume_writer::PushManifestToVolume(manifest, filesystem))
            return MakeUnexpected(Failure{});
        if(!filesystem.flush()){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetVolume: failed to flush staged volume metadata"));
            return MakeUnexpected(Failure{});
        }

        stagedFileCount = filesystem.fileCount();
        stagedSegmentCount = volumeStorage.segmentCount();
        if(!filesystem.unmount())
            return MakeUnexpected(Failure{});
    }

    if(!Core::Filesystem::PublishStagedVolume(stagedVolumePaths, resolvedPaths.outputDirectory, volumeConfig->volumeName, stagedSegmentCount, scratchArena, {}, &stageDirectoryCleanup, &backupDirectoryCleanup))
        return MakeUnexpected(Failure{});
    stageDirectoryCleanup.dismiss();
    backupDirectoryCleanup.dismiss();

    result.volumeName = volumeConfig->volumeName;
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

