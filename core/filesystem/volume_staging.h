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
    StagedDirectoryCleanupGuard(const Path& directoryPath, AStringView operationName, AStringView label = "stage directory");
    ~StagedDirectoryCleanupGuard();

    void dismiss();

private:
    Path m_directoryPath;
    ACompactString m_operationName;
    ACompactString m_label;
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
