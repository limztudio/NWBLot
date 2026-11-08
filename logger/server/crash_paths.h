// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <logger/common.h>

#include <core/alloc/scratch.h>
#include <core/crash/package_names.h>

#include <global/filesystem/executable_paths.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CrashScratchPath = ::Path<Core::Alloc::ScratchArena>;

inline constexpr StringView s_CrashRawDirectoryName = "raw";
inline constexpr StringView s_CrashInvalidDirectoryName = "invalid";
inline constexpr StringView s_CrashExtractedDirectoryName = "packages";
inline constexpr StringView s_CrashInboxDirectoryName = "inbox";
inline constexpr StringView s_CrashSymbolStoreDirectoryName = "symbols";
inline constexpr StringView s_CrashUploadArchiveFilePrefix = "crash_";
inline constexpr StringView s_CrashUploadArchiveFileExtension = ".nwbcrashpkg";
inline constexpr StringView s_ServerSymbolicationFileName = "server_symbolication.txt";

[[nodiscard]] inline CrashScratchPath CrashDefaultRootDirectory(Core::Alloc::ScratchArena& arena){
    return ExecutablePaths(arena).defaultStorageDirectory(Core::Crash::PackageNames::s_DefaultRootDirectoryName);
}

[[nodiscard]] inline CrashScratchPath CrashStorageDirectory(Core::Alloc::ScratchArena& arena, const Path& configuredStorageDirectory){
    return ExecutablePaths(arena).storageDirectory(configuredStorageDirectory, Core::Crash::PackageNames::s_DefaultRootDirectoryName);
}

[[nodiscard]] inline CrashScratchPath CrashRawDirectory(Core::Alloc::ScratchArena& arena, const Path& configuredStorageDirectory){
    return CrashStorageDirectory(arena, configuredStorageDirectory) / s_CrashRawDirectoryName;
}

[[nodiscard]] inline CrashScratchPath CrashInvalidDirectory(Core::Alloc::ScratchArena& arena, const Path& configuredStorageDirectory){
    return CrashStorageDirectory(arena, configuredStorageDirectory) / s_CrashInvalidDirectoryName;
}

[[nodiscard]] inline CrashScratchPath CrashExtractedDirectory(Core::Alloc::ScratchArena& arena, const Path& configuredStorageDirectory){
    return CrashStorageDirectory(arena, configuredStorageDirectory) / s_CrashExtractedDirectoryName;
}

[[nodiscard]] inline CrashScratchPath CrashExtractedPackageDirectory(Core::Alloc::ScratchArena& arena, const Path& configuredStorageDirectory, const CrashScratchPath& archivePath){
    return CrashExtractedDirectory(arena, configuredStorageDirectory) / archivePath.stemView();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

