// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RemoveStagedDirectoryIfPresent(const Path& directoryPath, AStringView operationName, AStringView label);
void CleanupStagedDirectoryBestEffort(const Path& directoryPath, AStringView operationName, AStringView label);
bool EnsureEmptyStagedDirectory(const Path& directoryPath, AStringView operationName, AStringView label);

class StagedDirectoryCleanupGuard : NoCopy{
public:
    // The operation and label backing text must outlive this guard.
    StagedDirectoryCleanupGuard(const Path& directoryPath, AStringView operationName, AStringView label = "stage directory");
    ~StagedDirectoryCleanupGuard();


public:
    void dismiss()noexcept;

private:
    Path m_directoryPath;
    AStringView m_operationName;
    AStringView m_label;
    bool m_active = true;
};

bool PublishStagedVolume(
    const StagedDirectoryPaths& stagedPaths,
    const Path& outputDirectory,
    AStringView volumeName,
    usize segmentCount
);
bool RemoveVolumeSegments(const Path& outputDirectory, AStringView volumeName);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

