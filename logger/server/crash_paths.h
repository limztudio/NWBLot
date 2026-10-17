// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <logger/common.h>

#include <core/alloc/scratch.h>


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

[[nodiscard]] CrashScratchPath CrashDefaultRootDirectory(Core::Alloc::ScratchArena& arena);
[[nodiscard]] CrashScratchPath CrashStorageDirectory(Core::Alloc::ScratchArena& arena, const Path& configuredStorageDirectory);
[[nodiscard]] CrashScratchPath CrashRawDirectory(Core::Alloc::ScratchArena& arena, const Path& configuredStorageDirectory);
[[nodiscard]] CrashScratchPath CrashInvalidDirectory(Core::Alloc::ScratchArena& arena, const Path& configuredStorageDirectory);
[[nodiscard]] CrashScratchPath CrashExtractedDirectory(Core::Alloc::ScratchArena& arena, const Path& configuredStorageDirectory);
[[nodiscard]] CrashScratchPath CrashExtractedPackageDirectory(Core::Alloc::ScratchArena& arena, const Path& configuredStorageDirectory, const CrashScratchPath& archivePath);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

