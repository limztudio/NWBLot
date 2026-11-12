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
private:
    using PathValue = typename Path::value_type;
    using NativeViewFn = BasicStringView<PathValue>(*)(const void*)noexcept;
    using CStrFn = const PathValue*(*)(const void*)noexcept;

    template<typename ArenaT>
    [[nodiscard]] static FOUNDATION_INLINE BasicStringView<PathValue> ReadNativeView(const void* const ptr)noexcept{
        return static_cast<const ::Path<ArenaT>*>(ptr)->native();
    }

    template<typename ArenaT>
    [[nodiscard]] static FOUNDATION_INLINE const PathValue* ReadCStr(const void* const ptr)noexcept{
        return static_cast<const ::Path<ArenaT>*>(ptr)->c_str();
    }


public:
    // The path and backing text must outlive this guard.
    template<typename ArenaT>
    StagedDirectoryCleanupGuard(const ::Path<ArenaT>& directoryPath, const AStringView operationName, const AStringView label = s_StageDirectoryLabel)noexcept
        : m_directoryPath(&directoryPath)
        , m_nativeViewFn(&ReadNativeView<ArenaT>)
        , m_cstrFn(&ReadCStr<ArenaT>)
        , m_operationName(operationName)
        , m_label(label)
    {}
    template<typename ArenaT>
    StagedDirectoryCleanupGuard(::Path<ArenaT>&&, AStringView, AStringView = s_StageDirectoryLabel) = delete;
    ~StagedDirectoryCleanupGuard()noexcept;


public:
    void dismiss()noexcept{ m_active = false; }


private:
    const void* m_directoryPath;
    NativeViewFn m_nativeViewFn;
    CStrFn m_cstrFn;
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
b8 PublishStagedVolume(const ::StagedDirectoryPaths<Alloc::ScratchArena>& stagedPaths, const ::Path<Alloc::ScratchArena>& outputDirectory, AStringView volumeName, usize segmentCount, Alloc::ScratchArena& scratchArena, VolumePublicationObserver observer = {}, StagedDirectoryCleanupGuard* stageCleanup = nullptr, StagedDirectoryCleanupGuard* backupCleanup = nullptr);

template<typename StagedArenaT, typename OutArenaT>
    requires (
        (!IsSame_V<StagedArenaT, Alloc::GlobalArena> || !IsSame_V<OutArenaT, Alloc::GlobalArena>)
        && (!IsSame_V<StagedArenaT, Alloc::ScratchArena> || !IsSame_V<OutArenaT, Alloc::ScratchArena>)
    )
FOUNDATION_INLINE b8 PublishStagedVolume(const ::StagedDirectoryPaths<StagedArenaT>& stagedPaths, const ::Path<OutArenaT>& outputDirectory, const AStringView volumeName, const usize segmentCount, Alloc::ScratchArena& scratchArena, const VolumePublicationObserver observer = {}, StagedDirectoryCleanupGuard* const stageCleanup = nullptr, StagedDirectoryCleanupGuard* const backupCleanup = nullptr){
    const ::StagedDirectoryPaths<Alloc::ScratchArena> scratchStaged(scratchArena, stagedPaths);
    const ::Path<Alloc::ScratchArena> scratchOutput(scratchArena, outputDirectory);
    return PublishStagedVolume(scratchStaged, scratchOutput, volumeName, segmentCount, scratchArena, observer, stageCleanup, backupCleanup);
}
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

