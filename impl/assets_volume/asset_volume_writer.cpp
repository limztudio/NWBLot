// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset_volume_writer.h"

#include "cook_paths.h"

#include <core/assets/module.h>
#include <core/filesystem/module.h>

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_asset_volume_writer{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsVolumeCookDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool WriteAssetVolume(
    Core::Alloc::GlobalArena& arena,
    const ResolvedCookPaths& resolvedPaths,
    const AStringView configurationSafeName,
    const u64 plannedFileCount,
    const AssetVolumeExternalWriterVector& externalWriters,
    ParsedAssetMetadata& parsedMetadata,
    AssetVolumeWriteResult& outResult,
    ScratchArena& scratchArena
){
    outResult = {};

    VirtualPathHashSet seenVirtualPathHashes{arena};
    if(plannedFileCount <= static_cast<u64>(Limit<usize>::s_Max))
        seenVirtualPathHashes.reserve(static_cast<usize>(plannedFileCount));

    Core::Filesystem::VolumeBuildConfig volumeConfig;
    if(!__hidden_asset_volume_writer::ConfigureVolumeSizing(plannedFileCount, volumeConfig))
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
        Core::Filesystem::VolumeSession volumeSession(arena);
        if(!volumeSession.create(stagedVolumePaths.stageDirectory, volumeConfig)){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: failed to create staged volume session"));
            return false;
        }

        for(const AssetVolumeExternalWriter& externalWriter : externalWriters){
            if(!externalWriter(volumeSession, seenVirtualPathHashes, scratchArena))
                return false;
        }

        CookEntryWriteContext cookEntryWriteContext{
            volumeSession,
            seenVirtualPathHashes
        };
        if(!parsedMetadata.entryRegistry.writeAll(cookEntryWriteContext))
            return false;
        if(!volumeSession.flush()){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: failed to flush staged volume metadata"));
            return false;
        }

        stagedFileCount = volumeSession.fileCount();
        stagedSegmentCount = volumeSession.segmentCount();
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


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

