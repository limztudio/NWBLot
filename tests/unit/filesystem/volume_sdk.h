// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <engine_namespace.h>

#include <core/filesystem/factory.h>
#include <core/filesystem/volume_file_system.h>
#include <core/filesystem/volume_staging.h>
#include <core/filesystem/volume_storage_detail.h>
#include <tests/common/memory_sdk.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::FilesystemSdk{
using namespace Core::Filesystem;

inline Expected<usize> ReadCursor(const IFilesystem& filesystem, FileCursor& cursor, Span<u8> bytes){ return filesystem.readFile(cursor, bytes.data(), bytes.size()); }
inline b8 WriteNull(IFilesystem& filesystem, const Name path, const usize count){ return filesystem.writeFile(path, nullptr, count); }
inline Expected<usize> ReadNull(const IFilesystem& filesystem, const Name path, const u64 offset, const usize count){ return filesystem.readFile(path, offset, nullptr, count); }

template<typename DirectoryArena>
void StagedPaths(HeapStorage& storage, const ::Path<DirectoryArena>& directory, AStringView name, ScratchStorage& scratch, ::StagedDirectoryPaths<::ArenaBase>& output){
    const auto paths = FilesystemVolumeStagingDetail::BuildStagedVolumePaths(Path(storage.arena, directory), name, scratch.arena);
    output = ::StagedDirectoryPaths<::ArenaBase>(output.stageDirectory.arena(), paths);
}

template<typename Observer>
VolumePublicationObserver BindObserver(Observer& value)noexcept{
    return { &value, nullptr, [](void* context)noexcept{ static_cast<Observer*>(context)->rollbackFailed(); } };
}

template<typename DirectoryArena, typename StagedArena>
b8 Publish(const ::StagedDirectoryPaths<StagedArena>& paths, const ::Path<DirectoryArena>& directory, AStringView name, usize count, ScratchStorage& scratch, VolumePublicationObserver observer, StagedDirectoryCleanupGuard& stage, StagedDirectoryCleanupGuard& backup){
    return PublishStagedVolume(paths, directory, name, count, scratch.arena, observer, &stage, &backup);
}
template<typename Factory>
Expected<UniquePtr<IFilesystem>> CreateWithFactory(HeapStorage& owner, const VolumeMountDesc& mount){
    return CreateFilesystem(owner.arena, mount, [](Core::Alloc::GlobalArena&, const VolumeMountDesc&){ return Factory{}.create(); });
}


#if FOUNDATION_OCCUR_WARNING
inline constexpr b8 s_WarningsEnabled = true;
#else
inline constexpr b8 s_WarningsEnabled = false;
#endif
};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

