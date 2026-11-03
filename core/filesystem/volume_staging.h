// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr AStringView s_StageDirectoryLabel = "stage directory";
inline constexpr AStringView s_BackupDirectoryLabel = "backup directory";

b8 CreateNewStagedDirectory(const Path& directoryPath, AStringView operationName, AStringView label);

class StagedDirectoryCleanupGuard : NoCopy{
public:
    // The path and backing text must outlive this guard.
    StagedDirectoryCleanupGuard(const Path& directoryPath, const AStringView operationName, const AStringView label = s_StageDirectoryLabel)noexcept
        : m_directoryPath(directoryPath)
        , m_operationName(operationName)
        , m_label(label)
    {}
    StagedDirectoryCleanupGuard(Path&&, AStringView, AStringView = s_StageDirectoryLabel) = delete;
    ~StagedDirectoryCleanupGuard()noexcept;


public:
    void dismiss()noexcept{ m_active = false; }


private:
    const Path& m_directoryPath;
    AStringView m_operationName;
    AStringView m_label;
    b8 m_active = true;
};

// A synchronous no-throw owner publication occurs after all segment moves, before fallible cleanup observers.
struct VolumePublicationObserver{
    void* context = nullptr;
    void(*commit)(void*)noexcept = nullptr;
    void(*rollbackFailed)(void*)noexcept = nullptr;
};

b8 PublishStagedVolume(const StagedDirectoryPaths& stagedPaths, const Path& outputDirectory, AStringView volumeName, usize segmentCount, Alloc::ScratchArena& scratchArena, VolumePublicationObserver observer = {}, StagedDirectoryCleanupGuard* stageCleanup = nullptr, StagedDirectoryCleanupGuard* backupCleanup = nullptr);
b8 RemoveVolumeSegments(const Path& outputDirectory, AStringView volumeName, Alloc::ScratchArena& scratchArena);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FilesystemVolumeStagingDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr AStringView s_VolumePublishLogPrefix = "Filesystem volume publish";

StagedDirectoryPaths BuildStagedVolumePaths(const Path& outputDirectory, AStringView volumeName, Alloc::ScratchArena& scratchArena);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

