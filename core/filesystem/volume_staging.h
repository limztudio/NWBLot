// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr AStringView s_StageDirectoryLabel = "stage directory";
inline constexpr AStringView s_BackupDirectoryLabel = "backup directory";

b8 RemoveStagedDirectoryIfPresent(const Path& directoryPath, AStringView operationName, AStringView label);
void CleanupStagedDirectoryBestEffort(const Path& directoryPath, AStringView operationName, AStringView label);
b8 EnsureEmptyStagedDirectory(const Path& directoryPath, AStringView operationName, AStringView label);

class StagedDirectoryCleanupGuard : NoCopy{
public:
    // The operation and label backing text must outlive this guard.
    StagedDirectoryCleanupGuard(const Path& directoryPath, AStringView operationName, AStringView label = s_StageDirectoryLabel);
    ~StagedDirectoryCleanupGuard()noexcept;


public:
    void dismiss()noexcept{ m_active = false; }


private:
    Path m_directoryPath;
    AStringView m_operationName;
    AStringView m_label;
    b8 m_active = true;
};

b8 PublishStagedVolume(const StagedDirectoryPaths& stagedPaths, const Path& outputDirectory, AStringView volumeName, usize segmentCount, Alloc::ScratchArena& scratchArena);
b8 RemoveVolumeSegments(const Path& outputDirectory, AStringView volumeName, Alloc::ScratchArena& scratchArena);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

