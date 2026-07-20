// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "volume_build.h"
#include "volume_session.h"
#include "volume_staging_detail.h"
#include "arena_names.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool BuildVolume(const Path& outputDirectory, const VolumeBuildConfig& config, const VolumeBuildFileMap& files, VolumeBuildInfo& outBuildInfo){
    outBuildInfo = {};

    const StagedDirectoryPaths stagedVolumePaths = __hidden_filesystem_staging::BuildStagedVolumePaths(outputDirectory, config.volumeName.view());
    if(!EnsureEmptyStagedDirectory(stagedVolumePaths.stageDirectory, __hidden_filesystem_staging::s_VolumePublishLogPrefix, "stage directory"))
        return false;
    StagedDirectoryCleanupGuard stageDirectoryCleanup(stagedVolumePaths.stageDirectory, __hidden_filesystem_staging::s_VolumePublishLogPrefix);
    if(!RemoveStagedDirectoryIfPresent(stagedVolumePaths.backupDirectory, __hidden_filesystem_staging::s_VolumePublishLogPrefix, "backup directory"))
        return false;

    Alloc::GlobalArena arena(FilesystemArenaScope::s_BuildVolumeArena);

    {
        VolumeSession volumeSession(arena);
        if(!volumeSession.create(stagedVolumePaths.stageDirectory, config))
            return false;
        volumeSession.reserveFileCapacity(files.size());

        for(const auto& [virtualPath, payloadBytes] : files){
            if(!volumeSession.pushDataDeferred(virtualPath, payloadBytes))
                return false;
        }
        if(!volumeSession.flush())
            return false;

        outBuildInfo.fileCount = volumeSession.fileCount();
        outBuildInfo.segmentCount = static_cast<u64>(volumeSession.segmentCount());
    }

    if(
        !__hidden_filesystem_staging::PromoteStagedVolume(
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
