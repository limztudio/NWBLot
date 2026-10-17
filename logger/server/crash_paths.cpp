// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "crash_paths.h"

#include <core/crash/package_names.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CrashScratchPath CrashDefaultRootDirectory(Core::Alloc::ScratchArena& arena){
    const auto executableDirectory = GetExecutableDirectory(arena);
    if(executableDirectory)
        return *executableDirectory / Core::Crash::PackageNames::s_DefaultRootDirectoryName;

    return CrashScratchPath(arena, Core::Crash::PackageNames::s_DefaultRootDirectoryName);
}

CrashScratchPath CrashStorageDirectory(Core::Alloc::ScratchArena& arena, const Path& configuredStorageDirectory){
    if(!configuredStorageDirectory.empty())
        return CrashScratchPath(arena, configuredStorageDirectory);

    return CrashDefaultRootDirectory(arena);
}

CrashScratchPath CrashRawDirectory(Core::Alloc::ScratchArena& arena, const Path& configuredStorageDirectory){
    return CrashStorageDirectory(arena, configuredStorageDirectory) / s_CrashRawDirectoryName;
}

CrashScratchPath CrashInvalidDirectory(Core::Alloc::ScratchArena& arena, const Path& configuredStorageDirectory){
    return CrashStorageDirectory(arena, configuredStorageDirectory) / s_CrashInvalidDirectoryName;
}

CrashScratchPath CrashExtractedDirectory(Core::Alloc::ScratchArena& arena, const Path& configuredStorageDirectory){
    return CrashStorageDirectory(arena, configuredStorageDirectory) / s_CrashExtractedDirectoryName;
}

CrashScratchPath CrashExtractedPackageDirectory(Core::Alloc::ScratchArena& arena, const Path& configuredStorageDirectory, const CrashScratchPath& archivePath){
    return CrashExtractedDirectory(arena, configuredStorageDirectory) / archivePath.stem();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

