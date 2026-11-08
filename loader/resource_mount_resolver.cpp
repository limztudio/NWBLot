// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "resource_mount_resolver.h"

#include <core/filesystem/volume_naming.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


namespace LoaderDetail{


Expected<Path, ResourceMountResolver::Error> ResourceMountResolver::Resolve(NWB::Core::Alloc::GlobalArena& arena, NWB::Core::Alloc::ScratchArena& scratchArena){
    const auto currentDirectory = GetCurrentPath(scratchArena);
    if(!currentDirectory)
        return MakeUnexpected(Error{ currentDirectory.error(), "current directory", ::Path<NWB::Core::Alloc::ScratchArena>(scratchArena) });
    const auto currentResDirectory = *currentDirectory / s_ResourceDirectoryName;
    const auto currentExists = NWB::Core::Filesystem::VolumeSegmentExists(currentResDirectory, s_GraphicsVolumeName);
    if(!currentExists)
        return MakeUnexpected(Error{ currentExists.error(), "resource directory", ::Path<NWB::Core::Alloc::ScratchArena>(scratchArena, currentResDirectory) });
    if(*currentExists)
        return Path(arena, currentResDirectory);

    const auto executableDirectory = ExecutablePaths(scratchArena).directory();
    if(!executableDirectory)
        return MakeUnexpected(Error{ executableDirectory.error(), "executable directory", ::Path<NWB::Core::Alloc::ScratchArena>(scratchArena) });

    const auto executableResDirectory = *executableDirectory / s_ResourceDirectoryName;
    const auto executableExists = NWB::Core::Filesystem::VolumeSegmentExists(executableResDirectory, s_GraphicsVolumeName);
    if(!executableExists)
        return MakeUnexpected(Error{ executableExists.error(), "resource directory", ::Path<NWB::Core::Alloc::ScratchArena>(scratchArena, executableResDirectory) });
    if(*executableExists)
        return Path(arena, executableResDirectory);

    const auto parentDirectory = executableDirectory->parentPath();
    if(parentDirectory.empty())
        return Path(arena, s_ResourceDirectoryName);

    const auto parentResDirectory = parentDirectory / s_ResourceDirectoryName;
    const auto parentExists = NWB::Core::Filesystem::VolumeSegmentExists(parentResDirectory, s_GraphicsVolumeName);
    if(!parentExists)
        return MakeUnexpected(Error{ parentExists.error(), "resource directory", ::Path<NWB::Core::Alloc::ScratchArena>(scratchArena, parentResDirectory) });
    if(*parentExists)
        return Path(arena, parentResDirectory);

    return Path(arena, s_ResourceDirectoryName);
}



};


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
