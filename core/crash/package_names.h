// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace PackageNames{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr StringView s_DefaultRootDirectoryName = "crashes";
inline constexpr StringView s_PendingDirectoryName = "pending";
inline constexpr StringView s_UploadedDirectoryName = "uploaded";
inline constexpr StringView s_UploadingDirectoryName = "uploading";
inline constexpr StringView s_FailedDirectoryName = "failed";
inline constexpr StringView s_CrashIdPrefix = "crash-";

inline constexpr StringView s_ManifestFileName = "manifest.json";
inline constexpr StringView s_MetadataFileName = "metadata.txt";
inline constexpr StringView s_BreadcrumbsFileName = "breadcrumbs.txt";
inline constexpr StringView s_EmergencyFileName = "emergency.txt";
inline constexpr StringView s_ArtifactStrategyFileName = "artifact_strategy.txt";
inline constexpr StringView s_CpuContextFileName = "cpu_context.txt";
inline constexpr StringView s_CallstackFileName = "callstack.txt";
inline constexpr StringView s_GpuCrashReportFileName = "gpu_crash.txt";
inline constexpr StringView s_GpuDetectiveCaptureFileName = "gpu_crash.rgd"; // canonical in-package AMD Radeon GPU Detective capture (decoded server-side through the vendored RGD backend)
inline constexpr StringView s_AftermathGpuDumpFileName = "gpu_crash.nv-gpudmp"; // canonical in-package NVIDIA Nsight Aftermath GPU crash dump (emitted by the device on device-lost, decoded server-side via the Aftermath SDK when present)
inline constexpr StringView s_SymbolicationFileName = "symbolication.txt";
inline constexpr StringView s_ProcessDumpFileName = "process.dmp";
inline constexpr StringView s_UploadAttemptFileName = "upload_attempt.txt";
inline constexpr StringView s_AndroidCollectionFileName = "android_collection.txt";
inline constexpr StringView s_AndroidTombstoneFileName = "android_tombstone.txt";
inline constexpr StringView s_AndroidEmergencyRequestFileName = "last_android_native_crash_request.bin";

inline constexpr StringView s_ProcAuxvFileName = "proc_auxv.bin";
inline constexpr StringView s_ProcCmdlineFileName = "proc_cmdline.bin";
inline constexpr StringView s_ProcCoredumpFilterFileName = "proc_coredump_filter.txt";
inline constexpr StringView s_ProcEnvironFileName = "proc_environ.bin";
inline constexpr StringView s_ProcLimitsFileName = "proc_limits.txt";
inline constexpr StringView s_ProcMapsFileName = "proc_maps.txt";
inline constexpr StringView s_ProcStatFileName = "proc_stat.txt";
inline constexpr StringView s_ProcStatusFileName = "proc_status.txt";
inline constexpr StringView s_LinuxCorePatternFileName = "linux_core_pattern.txt";
inline constexpr StringView s_LinuxCoreUsesPidFileName = "linux_core_uses_pid.txt";

inline constexpr StringView s_LinuxProcRootPath = "/proc/";
inline constexpr StringView s_ProcAuxvName = "auxv";
inline constexpr StringView s_ProcCmdlineName = "cmdline";
inline constexpr StringView s_ProcCoredumpFilterName = "coredump_filter";
inline constexpr StringView s_ProcEnvironName = "environ";
inline constexpr StringView s_ProcLimitsName = "limits";
inline constexpr StringView s_ProcMapsName = "maps";
inline constexpr StringView s_ProcStatName = "stat";
inline constexpr StringView s_ProcStatusName = "status";
inline constexpr StringView s_LinuxCorePatternPath = "/proc/sys/kernel/core_pattern";
inline constexpr StringView s_LinuxCoreUsesPidPath = "/proc/sys/kernel/core_uses_pid";

inline constexpr StringView s_LinuxCoreFileName = "core";

inline constexpr StringView s_ArchiveHeaderLine = "NWBCRASHPKG 1";
inline constexpr StringView s_ArchiveHeaderText = "NWBCRASHPKG 1\n";
inline constexpr StringView s_ArchiveFileHeaderPrefix = "FILE ";
inline constexpr StringView s_ArchiveEntryEndLine = "END";
inline constexpr StringView s_ArchiveEntryEndText = "\nEND\n";
inline constexpr StringView s_CrashUploadEndpoint = "/crash";
inline constexpr StringView s_CrashUploadEndpointName = "crash";
inline constexpr StringView s_UploadAttemptUnknownState = "unknown";
inline constexpr StringView s_UploadAttemptUploadingState = "uploading";
inline constexpr StringView s_UploadAttemptUploadedState = "uploaded";
inline constexpr StringView s_UploadAttemptRetryPendingState = "retry_pending";
inline constexpr StringView s_UploadAttemptRetryInterruptedState = "retry_pending_after_interrupted_upload";

inline constexpr StringView s_ManifestFormatKey = "format";
inline constexpr StringView s_ManifestCrashIdKey = "crash_id";
inline constexpr StringView s_ManifestApplicationKey = "application";
inline constexpr StringView s_ManifestVersionKey = "version";
inline constexpr StringView s_ManifestBuildIdKey = "build_id";
inline constexpr StringView s_ManifestAbiKey = "abi";
inline constexpr StringView s_ManifestPlatformKey = "platform";
inline constexpr StringView s_ManifestReasonKindKey = "reason_kind";
inline constexpr StringView s_ManifestReasonCodeKey = "reason_code";
inline constexpr StringView s_ManifestProcessIdKey = "process_id";
inline constexpr StringView s_ManifestThreadIdKey = "thread_id";
inline constexpr StringView s_ManifestHasExceptionContextKey = "has_exception_context";
inline constexpr StringView s_ManifestFaultAddressKey = "fault_address";
inline constexpr StringView s_ManifestInstructionPointerKey = "instruction_pointer";
inline constexpr StringView s_ManifestStackPointerKey = "stack_pointer";
inline constexpr StringView s_ManifestFramePointerKey = "frame_pointer";
inline constexpr StringView s_ManifestEventKey = "event";
inline constexpr StringView s_ManifestTriggerCategoryKey = "trigger_category";
inline constexpr StringView s_ManifestTriggerExpressionKey = "trigger_expression";
inline constexpr StringView s_ManifestTriggerMessageKey = "trigger_message";
inline constexpr StringView s_ManifestTriggerFileKey = "trigger_file";
inline constexpr StringView s_ManifestTriggerLineKey = "trigger_line";
inline constexpr StringView s_ManifestDumpDetailModeKey = "dump_detail_mode";
inline constexpr StringView s_ManifestArtifactStrategyKey = "artifact_strategy";
inline constexpr StringView s_ManifestHandlerLifetimeKey = "handler_lifetime";
inline constexpr StringView s_ManifestFormatValue = "nwb-crash-package-v1";
inline constexpr StringView s_ManifestDumpDetailModeFullValue = "full";
inline constexpr StringView s_ManifestDumpDetailModeSmallValue = "small";
inline constexpr StringView s_ManifestHandlerLifetimeClientIpcValue = "client_ipc_lifetime";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

