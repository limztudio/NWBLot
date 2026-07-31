// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "package_internal.h"

#if defined(NWB_PLATFORM_WINDOWS)
#include <dbghelp.h>
#include <windows.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Detail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_UnsignedTextBufferCapacity = 32u;
inline constexpr usize s_ManifestReserveBytes = 2048u;
inline constexpr usize s_LinuxProcPathTextCapacity = 128u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
static void AppendUnsignedText(CrashStringT<ArenaT>& out, const u64 value){
    char buffer[s_UnsignedTextBufferCapacity] = {};
    AppendUnsignedToFixedBuffer(buffer, value);
    out += buffer;
}

template<typename ArenaT>
static bool WriteCrashTextFile(const ::Path<ArenaT>& path, const CrashStringT<ArenaT>& text){
    return WriteTextFile(path, AStringView(text.data(), text.size()));
}

template<typename ArenaT>
static void AppendManifestPropertyPrefix(CrashStringT<ArenaT>& out, const StringView key, const bool isFirst){
    if(!isFirst)
        out += ",\n";
    out += "  \"";
    out += key;
    out += "\": ";
}

static const char* ArtifactStrategyName(const CrashRequest& request){
    switch(request.platform){
    case PlatformKind::Windows:
        return "windows_minidump_external_handler";
    case PlatformKind::Linux:
        return "linux_os_core_policy_plus_proc_snapshot";
    case PlatformKind::Android:
        return "android_tombstone_next_launch";
    default:
        return "native_platform_artifact";
    }
}

template<typename ArenaT>
static CrashStringT<ArenaT> BuildManifest(ArenaT& arena, const CrashRequest& request){
    CrashStringT<ArenaT> manifest{arena};
    manifest.reserve(s_ManifestReserveBytes);
    manifest += "{\n";
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestFormatKey, true);
    manifest += '"';
    manifest += PackageNames::s_ManifestFormatValue;
    manifest += '"';
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestCrashIdKey, false);
    AppendJsonQuotedText(manifest, request.crashId);
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestApplicationKey, false);
    AppendJsonQuotedText(manifest, request.applicationName);
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestVersionKey, false);
    AppendJsonQuotedText(manifest, request.versionText);
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestBuildIdKey, false);
    AppendJsonQuotedText(manifest, request.buildId);
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestAbiKey, false);
    AppendJsonQuotedText(manifest, request.abi);
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestPlatformKey, false);
    AppendJsonQuotedText(manifest, PlatformKindName(request.platform));
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestReasonKindKey, false);
    AppendJsonQuotedText(manifest, ReasonKindName(request.reasonKind));
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestReasonCodeKey, false);
    AppendUnsignedText(manifest, request.reasonCode);
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestProcessIdKey, false);
    AppendUnsignedText(manifest, request.processId);
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestThreadIdKey, false);
    AppendUnsignedText(manifest, request.threadId);
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestHasExceptionContextKey, false);
    manifest += request.exceptionPointers ? "true" : "false";
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestFaultAddressKey, false);
    AppendUnsignedText(manifest, request.faultAddress);
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestInstructionPointerKey, false);
    AppendUnsignedText(manifest, request.instructionPointer);
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestStackPointerKey, false);
    AppendUnsignedText(manifest, request.stackPointer);
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestFramePointerKey, false);
    AppendUnsignedText(manifest, request.framePointer);
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestEventKey, false);
    AppendJsonQuotedText(manifest, request.event);
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestTriggerCategoryKey, false);
    AppendJsonQuotedText(manifest, request.triggerCategory);
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestTriggerExpressionKey, false);
    AppendJsonQuotedText(manifest, request.triggerExpression);
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestTriggerMessageKey, false);
    AppendJsonQuotedText(manifest, request.triggerMessage);
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestTriggerFileKey, false);
    AppendJsonQuotedText(manifest, request.triggerFile);
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestTriggerLineKey, false);
    AppendUnsignedText(manifest, request.triggerLine);
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestDumpDetailModeKey, false);
    AppendJsonQuotedText(
        manifest,
        request.dumpDetailMode == DumpDetailMode::Full
            ? PackageNames::s_ManifestDumpDetailModeFullValue
            : PackageNames::s_ManifestDumpDetailModeSmallValue
    );
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestArtifactStrategyKey, false);
    AppendJsonQuotedText(manifest, ArtifactStrategyName(request));
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestHandlerLifetimeKey, false);
    AppendJsonQuotedText(manifest, PackageNames::s_ManifestHandlerLifetimeClientIpcValue);
    manifest += "\n}\n";
    return manifest;
}

template<typename ArenaT>
static CrashStringT<ArenaT> BuildMetadataText(ArenaT& arena, const CrashRequest& request){
    CrashStringT<ArenaT> text{arena};
    for(u32 i = 0u; i < request.metadataCount; ++i){
        text += request.metadata[i].key;
        text += '=';
        text += request.metadata[i].value;
        text += '\n';
    }
    return text;
}

template<typename ArenaT>
static CrashStringT<ArenaT> BuildBreadcrumbText(ArenaT& arena, const CrashRequest& request){
    CrashStringT<ArenaT> text{arena};
    for(u32 i = 0u; i < request.breadcrumbCount; ++i){
        AppendUnsignedText(text, request.breadcrumbs[i].order);
        text += " [";
        text += request.breadcrumbs[i].category;
        text += "] ";
        text += request.breadcrumbs[i].message;
        text += '\n';
    }
    return text;
}

template<typename ArenaT>
static CrashStringT<ArenaT> BuildEmergencyText(ArenaT& arena, const CrashRequest& request){
    CrashStringT<ArenaT> text{arena};
    text += "reason=";
    text += ReasonKindName(request.reasonKind);
    text += "\ncode=";
    AppendUnsignedText(text, request.reasonCode);
    text += "\npid=";
    AppendUnsignedText(text, request.processId);
    text += "\ntid=";
    AppendUnsignedText(text, request.threadId);
    text += "\nexception_context=";
    AppendUnsignedText(text, request.exceptionPointers);
    text += "\nfault_address=";
    AppendUnsignedText(text, request.faultAddress);
    text += "\ninstruction_pointer=";
    AppendUnsignedText(text, request.instructionPointer);
    text += "\nstack_pointer=";
    AppendUnsignedText(text, request.stackPointer);
    text += "\nframe_pointer=";
    AppendUnsignedText(text, request.framePointer);
    text += "\nevent=";
    text += request.event;
    text += "\ntrigger_category=";
    text += request.triggerCategory;
    text += "\ntrigger_expression=";
    text += request.triggerExpression;
    text += "\ntrigger_message=";
    text += request.triggerMessage;
    text += "\ntrigger_file=";
    text += request.triggerFile;
    text += "\ntrigger_line=";
    AppendUnsignedText(text, request.triggerLine);
    text += "\n";
    return text;
}

static bool HasCpuContext(const CrashRequest& request){
    return request.faultAddress != 0u
        || request.instructionPointer != 0u
        || request.stackPointer != 0u
        || request.framePointer != 0u
    ;
}

static bool HasCallstack(const CrashRequest& request){
    return request.callstackFrameCount != 0u;
}

template<typename ArenaT>
static CrashStringT<ArenaT> BuildCpuContextText(ArenaT& arena, const CrashRequest& request){
    CrashStringT<ArenaT> text{arena};
    text += "fault_address=";
    AppendUnsignedText(text, request.faultAddress);
    text += "\ninstruction_pointer=";
    AppendUnsignedText(text, request.instructionPointer);
    text += "\nstack_pointer=";
    AppendUnsignedText(text, request.stackPointer);
    text += "\nframe_pointer=";
    AppendUnsignedText(text, request.framePointer);
    text += "\n";
    return text;
}

template<typename ArenaT>
static CrashStringT<ArenaT> BuildCallstackText(ArenaT& arena, const CrashRequest& request){
    CrashStringT<ArenaT> text{arena};
    const u32 frameCount = request.callstackFrameCount > s_MaxCallstackFrames
        ? static_cast<u32>(s_MaxCallstackFrames)
        : request.callstackFrameCount
    ;
    for(u32 i = 0u; i < frameCount; ++i){
        text += '#';
        AppendUnsignedText(text, i);
        text += " 0x";
        text += FormatHex64A(arena, request.callstackFrames[i]);
        text += '\n';
    }
    return text;
}

template<typename ArenaT>
static CrashStringT<ArenaT> BuildArtifactStrategyText(ArenaT& arena, const CrashRequest& request){
    CrashStringT<ArenaT> text{arena};
    text += "strategy=";
    text += ArtifactStrategyName(request);
    text += '\n';
    text += PackageNames::s_ManifestHandlerLifetimeKey;
    text += '=';
    text += PackageNames::s_ManifestHandlerLifetimeClientIpcValue;
    text += '\n';
    switch(request.platform){
    case PlatformKind::Windows:
        text += "detail=external handler writes a Windows minidump from outside the crashing process\n";
        break;
    case PlatformKind::Linux:
        text += "detail=external handler captures metadata/proc files, then fatal signals are re-raised so OS core policy can produce the core artifact\n";
        break;
    case PlatformKind::Android:
        text += "detail=next launch collects native tombstone information through Android system crash reporting\n";
        break;
    default:
        text += "detail=native platform artifact is expected outside the generic package writer\n";
        break;
    }
    return text;
}

template<typename ArenaT>
static bool WriteCrashPackageBasics(ArenaT& arena, const CrashRequest& request){
    const ::Path<ArenaT> packageDirectory = RequestPendingDirectory(arena, request);
    ErrorCode error;
    if(!EnsureDirectories(packageDirectory, error))
        return false;

    if(!WriteCrashTextFile(packageDirectory / PackageNames::s_ManifestFileName, BuildManifest(arena, request)))
        return false;
    if(!WriteCrashTextFile(packageDirectory / PackageNames::s_MetadataFileName, BuildMetadataText(arena, request)))
        return false;
    if(!WriteCrashTextFile(packageDirectory / PackageNames::s_BreadcrumbsFileName, BuildBreadcrumbText(arena, request)))
        return false;
    if(!WriteCrashTextFile(packageDirectory / PackageNames::s_EmergencyFileName, BuildEmergencyText(arena, request)))
        return false;
    if(!WriteCrashTextFile(packageDirectory / PackageNames::s_ArtifactStrategyFileName, BuildArtifactStrategyText(arena, request)))
        return false;
    if(HasCpuContext(request) && !WriteCrashTextFile(packageDirectory / PackageNames::s_CpuContextFileName, BuildCpuContextText(arena, request)))
        return false;
    if(HasCallstack(request) && !WriteCrashTextFile(packageDirectory / PackageNames::s_CallstackFileName, BuildCallstackText(arena, request)))
        return false;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_WINDOWS)
template<typename ArenaT>
static bool WriteWindowsMinidump(ArenaT& arena, const CrashRequest& request){
    const ::Path<ArenaT> dumpPath = RequestPendingDirectory(arena, request) / PackageNames::s_ProcessDumpFileName;

    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_DUP_HANDLE, FALSE, request.processId);
    if(!process)
        return false;

    HANDLE dumpFile = CreateFile(dumpPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if(dumpFile == INVALID_HANDLE_VALUE){
        CloseHandle(process);
        return false;
    }

    const MINIDUMP_TYPE dumpType = request.dumpDetailMode == DumpDetailMode::Full
        ? static_cast<MINIDUMP_TYPE>(MiniDumpWithFullMemory | MiniDumpWithHandleData | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules)
        : static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules)
    ;

    MINIDUMP_EXCEPTION_INFORMATION exceptionInformation = {};
    MINIDUMP_EXCEPTION_INFORMATION* exceptionInformationPointer = nullptr;
    if(request.exceptionPointers != 0u){
        exceptionInformation.ThreadId = static_cast<DWORD>(request.threadId);
        exceptionInformation.ExceptionPointers = reinterpret_cast<EXCEPTION_POINTERS*>(static_cast<usize>(request.exceptionPointers));
        exceptionInformation.ClientPointers = TRUE;
        exceptionInformationPointer = &exceptionInformation;
    }

    const BOOL ok = MiniDumpWriteDump(process, request.processId, dumpFile, dumpType, exceptionInformationPointer, nullptr, nullptr);

    CloseHandle(dumpFile);
    CloseHandle(process);
    return ok == TRUE;
}
#endif

#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
template<typename ArenaT>
static void CopyFileToPackage(ArenaT& arena, const CrashRequest& request, const AStringView sourcePath, const AStringView outputName){
    InputFileStream input(sourcePath.data(), s_FileOpenBinary);
    if(!input.is_open())
        return;

    OutputFileStream output((RequestPendingDirectory(arena, request) / outputName).c_str(), s_FileOpenBinary | s_FileOpenTruncate);
    if(output.is_open())
        output << input.rdbuf();
}

template<typename ArenaT>
static void CopyProcFile(ArenaT& arena, const CrashRequest& request, const AStringView procName, const AStringView outputName){
    char procPath[s_LinuxProcPathTextCapacity] = {};
    CopyFixedBuffer(procPath, PackageNames::s_LinuxProcRootPath);
    AppendUnsignedToFixedBuffer(procPath, request.processId);
    AppendFixedBuffer(procPath, "/");
    AppendFixedBuffer(procPath, procName.data());

    CopyFileToPackage(arena, request, procPath, outputName);
}
#endif

template<typename ArenaT>
static bool WriteCrashPackageWithArena(ArenaT& arena, const CrashRequest& request){
    if(request.magic != s_RequestMagic || request.version != s_RequestVersion)
        return false;
    if(!WriteCrashPackageBasics(arena, request))
        return false;

#if defined(NWB_PLATFORM_WINDOWS)
    if(request.platform == PlatformKind::Windows){
        const bool dumpWritten = WriteWindowsMinidump(arena, request);
        if(!WriteCrashTextFile(
            RequestPendingDirectory(arena, request) / PackageNames::s_SymbolicationFileName,
            CrashStringT<ArenaT>(
                dumpWritten
                    ? "minidump captured; server-side PDB symbolication pending\n"
                    : "minidump capture failed; metadata package only\n",
                arena
            )
        ))
            return false;
    }
#elif defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
    if(request.platform == PlatformKind::Linux){
        CopyProcFile(arena, request, PackageNames::s_ProcAuxvName, PackageNames::s_ProcAuxvFileName);
        CopyProcFile(arena, request, PackageNames::s_ProcCmdlineName, PackageNames::s_ProcCmdlineFileName);
        CopyProcFile(arena, request, PackageNames::s_ProcCoredumpFilterName, PackageNames::s_ProcCoredumpFilterFileName);
        CopyProcFile(arena, request, PackageNames::s_ProcEnvironName, PackageNames::s_ProcEnvironFileName);
        CopyProcFile(arena, request, PackageNames::s_ProcLimitsName, PackageNames::s_ProcLimitsFileName);
        CopyProcFile(arena, request, PackageNames::s_ProcMapsName, PackageNames::s_ProcMapsFileName);
        CopyProcFile(arena, request, PackageNames::s_ProcStatName, PackageNames::s_ProcStatFileName);
        CopyProcFile(arena, request, PackageNames::s_ProcStatusName, PackageNames::s_ProcStatusFileName);
        CopyFileToPackage(arena, request, PackageNames::s_LinuxCorePatternPath, PackageNames::s_LinuxCorePatternFileName);
        CopyFileToPackage(arena, request, PackageNames::s_LinuxCoreUsesPidPath, PackageNames::s_LinuxCoreUsesPidFileName);
        if(!WriteCrashTextFile(
            RequestPendingDirectory(arena, request) / PackageNames::s_SymbolicationFileName,
            CrashStringT<ArenaT>("linux crash package captured; OS core policy remains authoritative; server-side DWARF symbolication uses reachable module symbols\n", arena)
        ))
            return false;
    }
#else
    if(!WriteCrashTextFile(
        RequestPendingDirectory(arena, request) / PackageNames::s_SymbolicationFileName,
        CrashStringT<ArenaT>("native platform crash artifact expected; server-side symbolication pending\n", arena)
    ))
        return false;
#endif

    return true;
}

bool WriteCrashPackage(const CrashRequest& request){
    return WriteCrashPackageWithArena(DumpArena(), request);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
static void AppendArchiveText(CrashBytesT<ArenaT>& out, const AStringView text){
    for(const char ch : text)
        out.push_back(static_cast<u8>(ch));
}

template<typename ArenaT>
static void AppendArchiveText(CrashBytesT<ArenaT>& out, const char* text){
    if(text)
        AppendArchiveText(out, AStringView(text));
}

template<typename ArenaT>
static void AppendArchiveUnsigned(CrashBytesT<ArenaT>& out, const u64 value){
    char buffer[s_UnsignedTextBufferCapacity] = {};
    AppendUnsignedToFixedBuffer(buffer, value);
    AppendArchiveText(out, buffer);
}

template<typename ArenaT>
bool BuildPackageArchive(ArenaT& arena, const ::Path<ArenaT>& packageDirectory, CrashBytesT<ArenaT>& outArchive){
    outArchive.clear();
    AppendArchiveText(outArchive, PackageNames::s_ArchiveHeaderText);

    ErrorCode error;
    RecursiveDirectoryIterator directory(packageDirectory, error);
    if(error)
        return false;

    bool wroteFile = false;
    for(const auto& entry : directory){
        ErrorCode entryError;
        if(!entry.is_regular_file(entryError) || entryError)
            continue;

        CrashBytesT<ArenaT> fileBytes{arena};
        ErrorCode readError;
        if(!ReadBinaryFile(entry.path(), fileBytes, readError))
            return false;

        const CrashStringT<ArenaT> pathText = PathToGenericString<char>(arena, entry.path().lexically_relative(packageDirectory));
        AppendArchiveText(outArchive, PackageNames::s_ArchiveFileHeaderPrefix);
        AppendArchiveText(outArchive, AStringView(pathText.data(), pathText.size()));
        AppendArchiveText(outArchive, " ");
        AppendArchiveUnsigned(outArchive, fileBytes.size());
        AppendArchiveText(outArchive, "\n");
        outArchive.insert(outArchive.end(), fileBytes.begin(), fileBytes.end());
        AppendArchiveText(outArchive, PackageNames::s_ArchiveEntryEndText);
        wroteFile = true;
    }

    return wroteFile;
}

template bool BuildPackageArchive(
    Alloc::GlobalArena& arena,
    const ::Path<Alloc::GlobalArena>& packageDirectory,
    CrashBytesT<Alloc::GlobalArena>& outArchive
);

template bool BuildPackageArchive(
    Alloc::PersistentArena& arena,
    const ::Path<Alloc::PersistentArena>& packageDirectory,
    CrashBytesT<Alloc::PersistentArena>& outArchive
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

