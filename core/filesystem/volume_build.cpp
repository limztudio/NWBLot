// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "volume_build.h"
#include "volume_file_system.h"
#include "volume_staging_detail.h"
#include "arena_names.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<VolumeBuildInfo> BuildVolume(const Path& outputDirectory, const VolumeBuildConfig& config, const VolumeBuildFileMap& files){
    VolumeBuildInfo buildInfo;

    const StagedDirectoryPaths stagedVolumePaths = FilesystemVolumeStagingDetail::BuildStagedVolumePaths(outputDirectory, config.volumeName.view());
    if(!EnsureEmptyStagedDirectory(stagedVolumePaths.stageDirectory, FilesystemVolumeStagingDetail::s_VolumePublishLogPrefix, "stage directory"))
        return MakeUnexpected(Failure{});
    StagedDirectoryCleanupGuard stageDirectoryCleanup(stagedVolumePaths.stageDirectory, FilesystemVolumeStagingDetail::s_VolumePublishLogPrefix);
    if(!RemoveStagedDirectoryIfPresent(stagedVolumePaths.backupDirectory, FilesystemVolumeStagingDetail::s_VolumePublishLogPrefix, "backup directory"))
        return MakeUnexpected(Failure{});

    Alloc::GlobalArena arena(FilesystemArenaScope::s_BuildVolumeArena);

    {
        VolumeFileSystem volumeStorage(arena);
        IFilesystem& filesystem = volumeStorage;
        VolumeMountDesc mountDesc(arena);
        mountDesc.volumeName = config.volumeName;
        mountDesc.mountDirectory = stagedVolumePaths.stageDirectory;
        mountDesc.segmentSize = config.segmentSize;
        mountDesc.metadataSize = config.metadataSize;
        mountDesc.createIfMissing = true;
        mountDesc.usage = VolumeUsage::CookWrite;
        if(!filesystem.mount(mountDesc))
            return MakeUnexpected(Failure{});
        filesystem.reserveFileCapacity(files.size());

        for(const auto& [virtualPath, payloadBytes] : files){
            if(virtualPath.empty()){
                NWB_LOGGER_ERROR(NWB_TEXT("BuildVolume: virtual path is empty"));
                return MakeUnexpected(Failure{});
            }
            if(!filesystem.writeFileDeferred(Name(AStringView(virtualPath.data(), virtualPath.size())), payloadBytes))
                return MakeUnexpected(Failure{});
        }
        if(!filesystem.flush())
            return MakeUnexpected(Failure{});

        buildInfo.fileCount = filesystem.fileCount();
        buildInfo.segmentCount = static_cast<u64>(volumeStorage.segmentCount());
        if(!filesystem.unmount())
            return MakeUnexpected(Failure{});
    }

    if(
        !FilesystemVolumeStagingDetail::PromoteStagedVolume(
            stagedVolumePaths,
            outputDirectory,
            config.volumeName.view(),
            static_cast<usize>(buildInfo.segmentCount)
        )
    ){
        return MakeUnexpected(Failure{});
    }
    stageDirectoryCleanup.dismiss();

    return buildInfo;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

