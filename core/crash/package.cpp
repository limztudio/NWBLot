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


static void AppendUnsignedText(CrashString& out, const u64 value){
    char buffer[s_UnsignedTextBufferCapacity] = {};
    AppendUnsignedToFixedBuffer(buffer, value);
    out += buffer;
}

static bool WriteCrashTextFile(const CrashPath& path, const CrashString& text){
    return WriteTextFile(path, AStringView(text.data(), text.size()));
}

static void AppendJsonEscaped(CrashString& out, const char* text){
    out.push_back('"');
    if(text){
        for(const char* p = text; *p; ++p){
            switch(*p){
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out.push_back(*p);
                break;
            }
        }
    }
    out.push_back('"');
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

static CrashString BuildManifest(CrashArena& arena, const CrashRequest& request){
    CrashString manifest{arena};
    manifest.reserve(s_ManifestReserveBytes);
    manifest += "{\n";
    manifest += "  \"format\": \"";
    manifest += PackageNames::s_ManifestFormatValue;
    manifest += "\",\n";
    manifest += "  \"crash_id\": ";
    AppendJsonEscaped(manifest, request.crashId);
    manifest += ",\n  \"application\": ";
    AppendJsonEscaped(manifest, request.applicationName);
    manifest += ",\n  \"version\": ";
    AppendJsonEscaped(manifest, request.versionText);
    manifest += ",\n  \"build_id\": ";
    AppendJsonEscaped(manifest, request.buildId);
    manifest += ",\n  \"abi\": ";
    AppendJsonEscaped(manifest, request.abi);
    manifest += ",\n  \"platform\": ";
    AppendJsonEscaped(manifest, PlatformKindName(request.platform));
    manifest += ",\n  \"reason_kind\": ";
    AppendJsonEscaped(manifest, ReasonKindName(request.reasonKind));
    manifest += ",\n  \"reason_code\": ";
    AppendUnsignedText(manifest, request.reasonCode);
    manifest += ",\n  \"process_id\": ";
    AppendUnsignedText(manifest, request.processId);
    manifest += ",\n  \"thread_id\": ";
    AppendUnsignedText(manifest, request.threadId);
    manifest += ",\n  \"has_exception_context\": ";
    manifest += request.exceptionPointers ? "true" : "false";
    manifest += ",\n  \"fault_address\": ";
    AppendUnsignedText(manifest, request.faultAddress);
    manifest += ",\n  \"instruction_pointer\": ";
    AppendUnsignedText(manifest, request.instructionPointer);
    manifest += ",\n  \"stack_pointer\": ";
    AppendUnsignedText(manifest, request.stackPointer);
    manifest += ",\n  \"frame_pointer\": ";
    AppendUnsignedText(manifest, request.framePointer);
    manifest += ",\n  \"event\": ";
    AppendJsonEscaped(manifest, request.event);
    manifest += ",\n  \"trigger_category\": ";
    AppendJsonEscaped(manifest, request.triggerCategory);
    manifest += ",\n  \"trigger_expression\": ";
    AppendJsonEscaped(manifest, request.triggerExpression);
    manifest += ",\n  \"trigger_message\": ";
    AppendJsonEscaped(manifest, request.triggerMessage);
    manifest += ",\n  \"trigger_file\": ";
    AppendJsonEscaped(manifest, request.triggerFile);
    manifest += ",\n  \"trigger_line\": ";
    AppendUnsignedText(manifest, request.triggerLine);
    manifest += ",\n  \"dump_detail_mode\": ";
    AppendJsonEscaped(manifest, request.dumpDetailMode == DumpDetailMode::Full ? "full" : "small");
    manifest += ",\n  \"artifact_strategy\": ";
    AppendJsonEscaped(manifest, ArtifactStrategyName(request));
    manifest += ",\n  \"handler_lifetime\": ";
    AppendJsonEscaped(manifest, "client_ipc_lifetime");
    manifest += "\n}\n";
    return manifest;
}

static CrashString BuildMetadataText(CrashArena& arena, const CrashRequest& request){
    CrashString text{arena};
    for(u32 i = 0u; i < request.metadataCount; ++i){
        text += request.metadata[i].key;
        text += '=';
        text += request.metadata[i].value;
        text += '\n';
    }
    return text;
}

static CrashString BuildBreadcrumbText(CrashArena& arena, const CrashRequest& request){
    CrashString text{arena};
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

static CrashString BuildEmergencyText(CrashArena& arena, const CrashRequest& request){
    CrashString text{arena};
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

static CrashString BuildCpuContextText(CrashArena& arena, const CrashRequest& request){
    CrashString text{arena};
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

static CrashString BuildCallstackText(CrashArena& arena, const CrashRequest& request){
    CrashString text{arena};
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

static CrashString BuildArtifactStrategyText(CrashArena& arena, const CrashRequest& request){
    CrashString text{arena};
    text += "strategy=";
    text += ArtifactStrategyName(request);
    text += "\nhandler_lifetime=client_ipc_lifetime\n";
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

static bool WriteCrashPackageBasics(CrashArena& arena, const CrashRequest& request){
    const CrashPath packageDirectory = RequestPendingDirectory(arena, request);
    ErrorCode error;
    static_cast<void>(EnsureDirectories(packageDirectory, error));
    if(error)
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
static bool WriteWindowsMinidump(CrashArena& arena, const CrashRequest& request){
    const CrashPath dumpPath = RequestPendingDirectory(arena, request) / PackageNames::s_ProcessDumpFileName;

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
static void CopyFileToPackage(CrashArena& arena, const CrashRequest& request, const char* sourcePath, const char* outputName){
    InputFileStream input(sourcePath, s_FileOpenBinary);
    if(!input.is_open())
        return;

    OutputFileStream output((RequestPendingDirectory(arena, request) / outputName).c_str(), s_FileOpenBinary | s_FileOpenTruncate);
    if(output.is_open())
        output << input.rdbuf();
}

static void CopyProcFile(CrashArena& arena, const CrashRequest& request, const char* procName, const char* outputName){
    char procPath[s_LinuxProcPathTextCapacity] = {};
    CopyFixedBuffer(procPath, PackageNames::s_LinuxProcRootPath);
    AppendUnsignedToFixedBuffer(procPath, request.processId);
    AppendFixedBuffer(procPath, "/");
    AppendFixedBuffer(procPath, procName);

    CopyFileToPackage(arena, request, procPath, outputName);
}
#endif

bool WriteCrashPackage(const CrashRequest& request){
    CrashArena& arena = DumpArena();

    if(request.magic != s_RequestMagic || request.version != s_RequestVersion)
        return false;
    if(!WriteCrashPackageBasics(arena, request))
        return false;

#if defined(NWB_PLATFORM_WINDOWS)
    if(request.platform == PlatformKind::Windows){
        const bool dumpWritten = WriteWindowsMinidump(arena, request);
        if(!WriteCrashTextFile(
            RequestPendingDirectory(arena, request) / PackageNames::s_SymbolicationFileName,
            CrashString(
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
            CrashString("linux crash package captured; OS core policy remains authoritative; server-side DWARF symbolication uses reachable module symbols\n", arena)
        ))
            return false;
    }
#else
    if(!WriteCrashTextFile(
        RequestPendingDirectory(arena, request) / PackageNames::s_SymbolicationFileName,
        CrashString("native platform crash artifact expected; server-side symbolication pending\n", arena)
    ))
        return false;
#endif

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void AppendArchiveText(CrashBytes& out, const AStringView text){
    for(const char ch : text)
        out.push_back(static_cast<u8>(ch));
}

static void AppendArchiveText(CrashBytes& out, const char* text){
    if(text)
        AppendArchiveText(out, AStringView(text));
}

static void AppendArchiveUnsigned(CrashBytes& out, const u64 value){
    char buffer[s_UnsignedTextBufferCapacity] = {};
    AppendUnsignedToFixedBuffer(buffer, value);
    AppendArchiveText(out, buffer);
}

bool BuildPackageArchive(
    Alloc::PersistentArena& arena,
    const ::Path<Alloc::PersistentArena>& packageDirectory,
    CrashBytes& outArchive
){
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

        CrashBytes fileBytes{arena};
        ErrorCode readError;
        if(!ReadBinaryFile(entry.path(), fileBytes, readError))
            return false;

        const CrashString pathText = PathToGenericString<char>(arena, entry.path().lexically_relative(packageDirectory));
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

