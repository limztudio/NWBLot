// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <logger/common.h>

#include <core/alloc/scratch.h>
#include <core/crash/package_names.h>
#include <logger/server/crash_path_names.adhd.h>

#include <global/filesystem/executable_paths.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CrashScratchPath = ::Path<Core::Alloc::ScratchArena>;

inline constexpr StringView s_CrashRawDirectoryName = NWB::Log::CrashPathNames::s_CrashRawDirectoryName;
inline constexpr StringView s_CrashInvalidDirectoryName = NWB::Log::CrashPathNames::s_CrashInvalidDirectoryName;
inline constexpr StringView s_CrashExtractedDirectoryName = NWB::Log::CrashPathNames::s_CrashExtractedDirectoryName;
inline constexpr StringView s_CrashInboxDirectoryName = NWB::Log::CrashPathNames::s_CrashInboxDirectoryName;
inline constexpr StringView s_CrashSymbolStoreDirectoryName = NWB::Log::CrashPathNames::s_CrashSymbolStoreDirectoryName;
inline constexpr StringView s_CrashUploadArchiveFilePrefix = NWB::Log::CrashPathNames::s_CrashUploadArchiveFilePrefix;
inline constexpr StringView s_CrashUploadArchiveFileExtension = NWB::Log::CrashPathNames::s_CrashUploadArchiveFileExtension;
inline constexpr StringView s_ServerSymbolicationFileName = NWB::Log::CrashPathNames::s_ServerSymbolicationFileName;

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

