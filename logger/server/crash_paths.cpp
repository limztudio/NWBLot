// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "crash_paths.h"

#include <global/core/crash/package_names.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Path CrashDefaultRootDirectory(LogArena& arena){
    Path executableDirectory(arena);
    if(GetExecutableDirectory(executableDirectory))
        return executableDirectory / Core::Crash::PackageNames::s_DefaultRootDirectoryName;

    return Path(arena, Core::Crash::PackageNames::s_DefaultRootDirectoryName);
}

Path CrashStorageDirectory(LogArena& arena, const Path& configuredStorageDirectory){
    if(!configuredStorageDirectory.empty())
        return Path(arena, configuredStorageDirectory);

    return CrashDefaultRootDirectory(arena);
}

Path CrashRawDirectory(LogArena& arena, const Path& configuredStorageDirectory){
    return CrashStorageDirectory(arena, configuredStorageDirectory) / s_CrashRawDirectoryName;
}

Path CrashInvalidDirectory(LogArena& arena, const Path& configuredStorageDirectory){
    return CrashStorageDirectory(arena, configuredStorageDirectory) / s_CrashInvalidDirectoryName;
}

Path CrashExtractedDirectory(LogArena& arena, const Path& configuredStorageDirectory){
    return CrashStorageDirectory(arena, configuredStorageDirectory) / s_CrashExtractedDirectoryName;
}

Path CrashExtractedPackageDirectory(LogArena& arena, const Path& configuredStorageDirectory, const Path& archivePath){
    return CrashExtractedDirectory(arena, configuredStorageDirectory) / archivePath.stem();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

