// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "volume_build.h"
#include "volume_file_system.h"
#include "volume_staging_detail.h"
#include "arena_names.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool BuildVolume(const Path& outputDirectory, const VolumeBuildConfig& config, const VolumeBuildFileMap& files, VolumeBuildInfo& outBuildInfo){
    outBuildInfo = {};

    const StagedDirectoryPaths stagedVolumePaths = FilesystemVolumeStagingDetail::BuildStagedVolumePaths(outputDirectory, config.volumeName.view());
    if(!EnsureEmptyStagedDirectory(stagedVolumePaths.stageDirectory, FilesystemVolumeStagingDetail::s_VolumePublishLogPrefix, "stage directory"))
        return false;
    StagedDirectoryCleanupGuard stageDirectoryCleanup(stagedVolumePaths.stageDirectory, FilesystemVolumeStagingDetail::s_VolumePublishLogPrefix);
    if(!RemoveStagedDirectoryIfPresent(stagedVolumePaths.backupDirectory, FilesystemVolumeStagingDetail::s_VolumePublishLogPrefix, "backup directory"))
        return false;

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
        if(!filesystem.mountVolume(mountDesc))
            return false;
        filesystem.reserveFileCapacity(files.size());

        for(const auto& [virtualPath, payloadBytes] : files){
            if(virtualPath.empty()){
                NWB_LOGGER_ERROR(NWB_TEXT("BuildVolume: virtual path is empty"));
                return false;
            }
            if(!filesystem.writeFileDeferred(Name(AStringView(virtualPath.data(), virtualPath.size())), payloadBytes))
                return false;
        }
        if(!filesystem.flush())
            return false;

        outBuildInfo.fileCount = filesystem.fileCount();
        outBuildInfo.segmentCount = static_cast<u64>(volumeStorage.segmentCount());
        if(!filesystem.unmountVolume())
            return false;
    }

    if(
        !FilesystemVolumeStagingDetail::PromoteStagedVolume(
            stagedVolumePaths,
            outputDirectory,
            config.volumeName.view(),
            static_cast<usize>(outBuildInfo.segmentCount)
        )
    ){
        return false;
    }
    stageDirectoryCleanup.dismiss();

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

