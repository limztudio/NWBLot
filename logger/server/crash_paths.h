
#pragma once


#include <logger/common.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr const char* s_CrashRawDirectoryName = "raw";
inline constexpr const char* s_CrashInvalidDirectoryName = "invalid";
inline constexpr const char* s_CrashExtractedDirectoryName = "packages";
inline constexpr const char* s_CrashInboxDirectoryName = "inbox";
inline constexpr const char* s_CrashSymbolStoreDirectoryName = "symbols";
inline constexpr const char* s_CrashUploadArchiveFilePrefix = "crash_";
inline constexpr const char* s_CrashUploadArchiveFileExtension = ".nwbcrashpkg";
inline constexpr const char* s_ServerSymbolicationFileName = "server_symbolication.txt";

[[nodiscard]] Path CrashDefaultRootDirectory(LogArena& arena);
[[nodiscard]] Path CrashStorageDirectory(LogArena& arena, const Path& configuredStorageDirectory);
[[nodiscard]] Path CrashRawDirectory(LogArena& arena, const Path& configuredStorageDirectory);
[[nodiscard]] Path CrashInvalidDirectory(LogArena& arena, const Path& configuredStorageDirectory);
[[nodiscard]] Path CrashExtractedDirectory(LogArena& arena, const Path& configuredStorageDirectory);
[[nodiscard]] Path CrashExtractedPackageDirectory(LogArena& arena, const Path& configuredStorageDirectory, const Path& archivePath);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

