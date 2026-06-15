// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace PackageNames{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr const char* s_DefaultRootDirectoryName = "crashes";
inline constexpr const char* s_PendingDirectoryName = "pending";
inline constexpr const char* s_UploadedDirectoryName = "uploaded";
inline constexpr const char* s_UploadingDirectoryName = "uploading";
inline constexpr const char* s_FailedDirectoryName = "failed";
inline constexpr const char* s_GpuDirectoryName = "gpu";
inline constexpr const char* s_CrashIdPrefix = "crash-";

inline constexpr const char* s_CurrentBreadcrumbsFileName = "breadcrumbs_current.txt";
inline constexpr const char* s_ManifestFileName = "manifest.json";
inline constexpr const char* s_MetadataFileName = "metadata.txt";
inline constexpr const char* s_BreadcrumbsFileName = "breadcrumbs.txt";
inline constexpr const char* s_EmergencyFileName = "emergency.txt";
inline constexpr const char* s_ArtifactStrategyFileName = "artifact_strategy.txt";
inline constexpr const char* s_CpuContextFileName = "cpu_context.txt";
inline constexpr const char* s_CallstackFileName = "callstack.txt";
inline constexpr const char* s_TriggerFileName = "trigger.txt";
inline constexpr const char* s_SymbolicationFileName = "symbolication.txt";
inline constexpr const char* s_GpuAttachmentsFileName = "gpu_attachments.txt";
inline constexpr const char* s_ProcessDumpFileName = "process.dmp";
inline constexpr const char* s_UploadAttemptFileName = "upload_attempt.txt";
inline constexpr const char* s_AndroidCollectionFileName = "android_collection.txt";
inline constexpr const char* s_AndroidTombstoneFileName = "android_tombstone.txt";
inline constexpr const char* s_AndroidEmergencyRequestFileName = "last_android_native_crash_request.bin";

inline constexpr const char* s_ProcAuxvFileName = "proc_auxv.bin";
inline constexpr const char* s_ProcCmdlineFileName = "proc_cmdline.bin";
inline constexpr const char* s_ProcCoredumpFilterFileName = "proc_coredump_filter.txt";
inline constexpr const char* s_ProcEnvironFileName = "proc_environ.bin";
inline constexpr const char* s_ProcLimitsFileName = "proc_limits.txt";
inline constexpr const char* s_ProcMapsFileName = "proc_maps.txt";
inline constexpr const char* s_ProcStatFileName = "proc_stat.txt";
inline constexpr const char* s_ProcStatusFileName = "proc_status.txt";
inline constexpr const char* s_LinuxCorePatternFileName = "linux_core_pattern.txt";
inline constexpr const char* s_LinuxCoreUsesPidFileName = "linux_core_uses_pid.txt";

inline constexpr const char* s_LinuxProcRootPath = "/proc/";
inline constexpr const char* s_ProcAuxvName = "auxv";
inline constexpr const char* s_ProcCmdlineName = "cmdline";
inline constexpr const char* s_ProcCoredumpFilterName = "coredump_filter";
inline constexpr const char* s_ProcEnvironName = "environ";
inline constexpr const char* s_ProcLimitsName = "limits";
inline constexpr const char* s_ProcMapsName = "maps";
inline constexpr const char* s_ProcStatName = "stat";
inline constexpr const char* s_ProcStatusName = "status";
inline constexpr const char* s_LinuxCorePatternPath = "/proc/sys/kernel/core_pattern";
inline constexpr const char* s_LinuxCoreUsesPidPath = "/proc/sys/kernel/core_uses_pid";

inline constexpr const char* s_LinuxCoreFileName = "core";

inline constexpr const char* s_ArchiveHeaderLine = "NWBCRASHPKG 1";
inline constexpr const char* s_ArchiveHeaderText = "NWBCRASHPKG 1\n";
inline constexpr const char* s_ArchiveFileHeaderPrefix = "FILE ";
inline constexpr const char* s_ArchiveEntryEndLine = "END";
inline constexpr const char* s_ArchiveEntryEndText = "\nEND\n";
inline constexpr const char* s_CrashUploadEndpoint = "/crash";
inline constexpr const char* s_CrashUploadEndpointName = "crash";
inline constexpr const char* s_UploadAttemptUnknownState = "unknown";
inline constexpr const char* s_UploadAttemptUploadingState = "uploading";
inline constexpr const char* s_UploadAttemptUploadedState = "uploaded";
inline constexpr const char* s_UploadAttemptRetryPendingState = "retry_pending";
inline constexpr const char* s_UploadAttemptRetryInterruptedState = "retry_pending_after_interrupted_upload";

inline constexpr const char* s_ManifestFormatKey = "format";
inline constexpr const char* s_ManifestCrashIdKey = "crash_id";
inline constexpr const char* s_ManifestApplicationKey = "application";
inline constexpr const char* s_ManifestVersionKey = "version";
inline constexpr const char* s_ManifestBuildIdKey = "build_id";
inline constexpr const char* s_ManifestAbiKey = "abi";
inline constexpr const char* s_ManifestPlatformKey = "platform";
inline constexpr const char* s_ManifestReasonKindKey = "reason_kind";
inline constexpr const char* s_ManifestReasonCodeKey = "reason_code";
inline constexpr const char* s_ManifestProcessIdKey = "process_id";
inline constexpr const char* s_ManifestThreadIdKey = "thread_id";
inline constexpr const char* s_ManifestHasExceptionContextKey = "has_exception_context";
inline constexpr const char* s_ManifestFaultAddressKey = "fault_address";
inline constexpr const char* s_ManifestInstructionPointerKey = "instruction_pointer";
inline constexpr const char* s_ManifestStackPointerKey = "stack_pointer";
inline constexpr const char* s_ManifestFramePointerKey = "frame_pointer";
inline constexpr const char* s_ManifestEventKey = "event";
inline constexpr const char* s_ManifestTriggerCategoryKey = "trigger_category";
inline constexpr const char* s_ManifestTriggerExpressionKey = "trigger_expression";
inline constexpr const char* s_ManifestTriggerMessageKey = "trigger_message";
inline constexpr const char* s_ManifestTriggerFileKey = "trigger_file";
inline constexpr const char* s_ManifestTriggerLineKey = "trigger_line";
inline constexpr const char* s_ManifestDumpDetailModeKey = "dump_detail_mode";
inline constexpr const char* s_ManifestGpuDumpsEnabledKey = "gpu_dumps_enabled";
inline constexpr const char* s_ManifestArtifactStrategyKey = "artifact_strategy";
inline constexpr const char* s_ManifestHandlerLifetimeKey = "handler_lifetime";
inline constexpr const char* s_ManifestFormatValue = "nwb-crash-package-v1";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

