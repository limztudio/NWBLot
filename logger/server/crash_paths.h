// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <logger/common.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr StringView s_CrashRawDirectoryName = "raw";
inline constexpr StringView s_CrashInvalidDirectoryName = "invalid";
inline constexpr StringView s_CrashExtractedDirectoryName = "packages";
inline constexpr StringView s_CrashInboxDirectoryName = "inbox";
inline constexpr StringView s_CrashSymbolStoreDirectoryName = "symbols";
inline constexpr StringView s_CrashUploadArchiveFilePrefix = "crash_";
inline constexpr StringView s_CrashUploadArchiveFileExtension = ".nwbcrashpkg";
inline constexpr StringView s_ServerSymbolicationFileName = "server_symbolication.txt";

[[nodiscard]] Path CrashDefaultRootDirectory(LogArena& arena);
[[nodiscard]] Path CrashStorageDirectory(LogArena& arena, const Path& configuredStorageDirectory);
[[nodiscard]] Path CrashRawDirectory(LogArena& arena, const Path& configuredStorageDirectory);
[[nodiscard]] Path CrashInvalidDirectory(LogArena& arena, const Path& configuredStorageDirectory);
[[nodiscard]] Path CrashExtractedDirectory(LogArena& arena, const Path& configuredStorageDirectory);
[[nodiscard]] Path CrashExtractedPackageDirectory(LogArena& arena, const Path& configuredStorageDirectory, const Path& archivePath);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

