// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "internal.h"

#include <curl/curl.h>

#if defined(NWB_PLATFORM_WINDOWS)
#include <dbghelp.h>
#include <windows.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Detail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_DumpArenaPayloadSize = 512u * 1024u;
inline constexpr usize s_UnsignedTextBufferCapacity = 32u;
inline constexpr usize s_ManifestReserveBytes = 2048u;
inline constexpr usize s_LinuxProcPathTextCapacity = 128u;
inline constexpr usize s_AuthorizationBearerPrefixLength = sizeof("Authorization: Bearer ") - 1u;
inline constexpr long s_CurlOptionEnabled = 1L;
inline constexpr long s_CrashUploadConnectTimeoutMilliseconds = 1000L;
inline constexpr long s_CrashUploadTimeoutMilliseconds = 5000L;
inline constexpr long s_HttpSuccessStatusBegin = 200L;
inline constexpr long s_HttpSuccessStatusEnd = 300L;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Alloc::PersistentArena& DumpArena(){
    static Alloc::PersistentArena s_Arena(
        Alloc::PersistentArena::StructureAlignedSize(s_DumpArenaPayloadSize),
        "NWB::Core::Crash::DumpArena"
    );
    return s_Arena;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
static ::Path<ArenaT> PendingDirectory(const ::Path<ArenaT>& spoolDirectory){
    return spoolDirectory / PackageNames::s_PendingDirectoryName;
}

template<typename ArenaT>
static ::Path<ArenaT> UploadedDirectory(const ::Path<ArenaT>& spoolDirectory){
    return spoolDirectory / PackageNames::s_UploadedDirectoryName;
}

template<typename ArenaT>
static ::Path<ArenaT> UploadingDirectory(const ::Path<ArenaT>& spoolDirectory){
    return spoolDirectory / PackageNames::s_UploadingDirectoryName;
}

template<typename ArenaT>
static ::Path<ArenaT> FailedDirectory(const ::Path<ArenaT>& spoolDirectory){
    return spoolDirectory / PackageNames::s_FailedDirectoryName;
}

template<typename ArenaT>
static ::Path<ArenaT> RequestPendingDirectory(ArenaT& arena, const CrashRequest& request){
    return ::Path<ArenaT>(arena, request.spoolDirectory) / PackageNames::s_PendingDirectoryName / request.crashId;
}

template<typename ArenaT>
bool EnsureCrashSpoolDirectories(const ::Path<ArenaT>& spoolDirectory){
    ErrorCode error;
    static_cast<void>(EnsureDirectories(PendingDirectory(spoolDirectory), error));
    if(error)
        return false;

    error.clear();
    static_cast<void>(EnsureDirectories(UploadedDirectory(spoolDirectory), error));
    if(error)
        return false;

    error.clear();
    static_cast<void>(EnsureDirectories(UploadingDirectory(spoolDirectory), error));
    if(error)
        return false;

    error.clear();
    static_cast<void>(EnsureDirectories(FailedDirectory(spoolDirectory), error));
    return !error;
}

template bool EnsureCrashSpoolDirectories(const ::Path<Alloc::PersistentArena>& spoolDirectory);


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
static void AppendJsonEscaped(CrashStringT<ArenaT>& out, const char* text){
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

template<typename ArenaT>
static CrashStringT<ArenaT> BuildManifest(ArenaT& arena, const CrashRequest& request){
    CrashStringT<ArenaT> manifest{arena};
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
    manifest += ",\n  \"gpu_dumps_enabled\": ";
    manifest += request.enableGpuDumps ? "true" : "false";
    manifest += ",\n  \"artifact_strategy\": ";
    AppendJsonEscaped(manifest, ArtifactStrategyName(request));
    manifest += ",\n  \"handler_lifetime\": ";
    AppendJsonEscaped(manifest, "client_ipc_lifetime");
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

static bool HasTriggerContext(const CrashRequest& request){
    return
        request.triggerCategory[0] != 0
        || request.triggerExpression[0] != 0
        || request.triggerMessage[0] != 0
        || request.triggerFile[0] != 0
        || request.triggerLine != 0u
    ;
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
static CrashStringT<ArenaT> BuildTriggerText(ArenaT& arena, const CrashRequest& request){
    CrashStringT<ArenaT> text{arena};
    text += "category=";
    text += request.triggerCategory;
    text += "\nexpression=";
    text += request.triggerExpression;
    text += "\nmessage=";
    text += request.triggerMessage;
    text += "\nfile=";
    text += request.triggerFile;
    text += "\nline=";
    AppendUnsignedText(text, request.triggerLine);
    text += "\n";
    return text;
}

template<typename ArenaT>
static CrashStringT<ArenaT> BuildArtifactStrategyText(ArenaT& arena, const CrashRequest& request){
    CrashStringT<ArenaT> text{arena};
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

template<typename ArenaT>
static bool WriteCrashPackageBasics(ArenaT& arena, const CrashRequest& request){
    const ::Path<ArenaT> packageDirectory = RequestPendingDirectory(arena, request);
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
    if(HasTriggerContext(request) && !WriteCrashTextFile(packageDirectory / PackageNames::s_TriggerFileName, BuildTriggerText(arena, request)))
        return false;
    return true;
}

template<typename ArenaT>
static void WriteGpuCrashAttachments(ArenaT& arena, const CrashRequest& request){
    if(!request.enableGpuDumps)
        return;

    const ::Path<ArenaT> packageDirectory = RequestPendingDirectory(arena, request);
    const ::Path<ArenaT> gpuDirectory = packageDirectory / PackageNames::s_GpuDirectoryName;
    ErrorCode error;
    static_cast<void>(EnsureDirectories(gpuDirectory, error));
    if(error)
        return;

    CrashStringT<ArenaT> status{arena};
    for(usize i = 0u; i < g_State.gpuProviderCount; ++i){
        const GpuCrashProvider& provider = g_State.gpuProviders[i];
        if(!provider.writeAttachment)
            continue;

        status += "provider_index=";
        AppendUnsignedText(status, i);
        status += " status=";
        bool written = false;
        try{
            written = provider.writeAttachment(provider.userData, packageDirectory, AStringView(request.crashId));
        }
        catch(...){
            status += "exception\n";
            continue;
        }
        status += written
            ? "written\n"
            : "failed\n"
        ;
    }

    if(status.empty())
        status = "no gpu crash providers registered in this process\n";

    static_cast<void>(WriteCrashTextFile(packageDirectory / PackageNames::s_GpuAttachmentsFileName, status));
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
static void CopyFileToPackage(ArenaT& arena, const CrashRequest& request, const char* sourcePath, const char* outputName){
    InputFileStream input(sourcePath, s_FileOpenBinary);
    if(!input.is_open())
        return;

    OutputFileStream output((RequestPendingDirectory(arena, request) / outputName).c_str(), s_FileOpenBinary | s_FileOpenTruncate);
    if(output.is_open())
        output << input.rdbuf();
}

template<typename ArenaT>
static void CopyProcFile(ArenaT& arena, const CrashRequest& request, const char* procName, const char* outputName){
    char procPath[s_LinuxProcPathTextCapacity] = {};
    CopyFixedBuffer(procPath, PackageNames::s_LinuxProcRootPath);
    AppendUnsignedToFixedBuffer(procPath, request.processId);
    AppendFixedBuffer(procPath, "/");
    AppendFixedBuffer(procPath, procName);

    CopyFileToPackage(arena, request, procPath, outputName);
}
#endif

template<typename ArenaT>
static bool WriteCrashPackageWithArena(ArenaT& arena, const CrashRequest& request){
    if(request.magic != s_RequestMagic || request.version != s_RequestVersion)
        return false;
    if(!WriteCrashPackageBasics(arena, request))
        return false;

    WriteGpuCrashAttachments(arena, request);

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


static bool IsSafePackageName(Alloc::GlobalArena& arena, const Path& path){
    const CrashString name = PathToString<char>(arena, path.filename());
    if(name.empty() || name == "." || name == "..")
        return false;

    for(const char ch : name){
        const bool ok =
            (ch >= 'a' && ch <= 'z')
            || (ch >= 'A' && ch <= 'Z')
            || (ch >= '0' && ch <= '9')
            || ch == '-'
            || ch == '_'
            || ch == '.'
        ;
        if(!ok)
            return false;
    }
    return true;
}

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

static bool BuildPackageArchive(Alloc::GlobalArena& arena, const Path& packageDirectory, CrashBytes& outArchive){
    outArchive.clear();
    AppendArchiveText(outArchive, PackageNames::s_ArchiveHeaderText);

    ErrorCode error;
    RecursiveDirectoryIterator directory(packageDirectory, error);
    if(error)
        return false;

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
    }

    return true;
}

static CrashString CrashUploadUrl(Alloc::GlobalArena& arena, const char* logServerUrl){
    CrashString url{arena};
    if(logServerUrl)
        url += logServerUrl;
    if(url.empty())
        return url;

    constexpr AStringView suffix(PackageNames::s_CrashUploadEndpoint);
    if(url.size() >= suffix.size() && AStringView(url.data() + url.size() - suffix.size(), suffix.size()) == suffix)
        return url;
    if(!url.empty() && url.back() == '/')
        url += PackageNames::s_CrashUploadEndpointName;
    else
        url += PackageNames::s_CrashUploadEndpoint;
    return url;
}

static bool UploadPackage(Alloc::GlobalArena& arena, const CrashString& url, const CrashBytes& archiveBytes, const AStringView crashUploadToken){
    CURL* curl = curl_easy_init();
    if(!curl)
        return false;

    curl_slist* headers = nullptr;
    CrashString authorizationHeader{arena};
    if(!crashUploadToken.empty()){
        authorizationHeader.reserve(crashUploadToken.size() + s_AuthorizationBearerPrefixLength);
        authorizationHeader += "Authorization: Bearer ";
        authorizationHeader += crashUploadToken;
        headers = curl_slist_append(headers, authorizationHeader.c_str());
        if(!headers){
            curl_easy_cleanup(curl);
            return false;
        }
    }

    bool ok = true;
    ok = ok && curl_easy_setopt(curl, CURLOPT_URL, url.c_str()) == CURLE_OK;
    ok = ok && curl_easy_setopt(curl, CURLOPT_POST, s_CurlOptionEnabled) == CURLE_OK;
    ok = ok && curl_easy_setopt(curl, CURLOPT_NOSIGNAL, s_CurlOptionEnabled) == CURLE_OK;
    ok = ok && curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, s_CrashUploadConnectTimeoutMilliseconds) == CURLE_OK;
    ok = ok && curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, s_CrashUploadTimeoutMilliseconds) == CURLE_OK;
    ok = ok && curl_easy_setopt(curl, CURLOPT_POSTFIELDS, reinterpret_cast<const char*>(archiveBytes.data())) == CURLE_OK;
    ok = ok && curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(archiveBytes.size())) == CURLE_OK;
    if(headers)
        ok = ok && curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers) == CURLE_OK;

    if(ok)
        ok = curl_easy_perform(curl) == CURLE_OK;

    long responseCode = 0;
    if(ok)
        ok = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode) == CURLE_OK
            && responseCode >= s_HttpSuccessStatusBegin
            && responseCode < s_HttpSuccessStatusEnd
        ;

    if(headers)
        curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return ok;
}

static bool WriteUploadAttemptText(Alloc::GlobalArena& arena, const Path& packageDirectory, const char* state){
    CrashString text{arena};
    text += "state=";
    text += state ? state : PackageNames::s_UploadAttemptUnknownState;
    text += "\n";
    return WriteTextFile(packageDirectory / PackageNames::s_UploadAttemptFileName, AStringView(text.data(), text.size()));
}

static Path RequestBucketDirectory(Alloc::GlobalArena& arena, const CrashRequest& request, const char* bucketName){
    return Path(arena, request.spoolDirectory) / bucketName / request.crashId;
}

static bool ApplyRetentionToDirectory(
    Alloc::GlobalArena& arena,
    const Path& directory,
    const usize maxEntries,
    const AStringView protectedPackageName = AStringView()
){
    if(maxEntries == 0u)
        return true;

    ErrorCode error;
    const bool exists = IsDirectory(directory, error);
    if(error)
        return false;
    if(!exists)
        return true;

    Vector<Path, Alloc::GlobalArena> entries{arena};
    DirectoryIterator directoryIt(directory, error);
    if(error)
        return false;

    for(const auto& entry : directoryIt){
        ErrorCode entryError;
        if(!IsDirectory(entry.path(), entryError) || entryError || !IsSafePackageName(arena, entry.path()))
            continue;

        const CrashString packageName = PathToString<char>(arena, entry.path().filename());
        if(!protectedPackageName.empty() && AStringView(packageName.data(), packageName.size()) == protectedPackageName)
            continue;

        entries.emplace_back(arena, entry.path());
    }

    if(entries.size() <= maxEntries)
        return true;

    Sort(entries.begin(), entries.end());

    bool ok = true;
    const usize removeCount = entries.size() - maxEntries;
    for(usize i = 0u; i < removeCount; ++i){
        error.clear();
        if(!RemoveAllIfExists(entries[i], error))
            ok = false;
    }
    return ok;
}

bool ApplyCrashSpoolRetention(
    Alloc::GlobalArena& arena,
    const Path& spoolDirectory,
    const CrashSpoolRetentionConfig& retention,
    const AStringView protectedPendingPackageName
){
    bool ok = true;
    ok = ApplyRetentionToDirectory(
        arena,
        PendingDirectory(spoolDirectory),
        retention.maxPendingPackages,
        protectedPendingPackageName
    ) && ok;
    ok = ApplyRetentionToDirectory(arena, UploadedDirectory(spoolDirectory), retention.maxUploadedPackages) && ok;
    ok = ApplyRetentionToDirectory(arena, FailedDirectory(spoolDirectory), retention.maxFailedPackages) && ok;
    ok = ApplyRetentionToDirectory(arena, UploadingDirectory(spoolDirectory), retention.maxUploadingPackages) && ok;
    return ok;
}

CrashDumpResult CrashPackageResult(const CrashRequest& request){
    if(request.magic != s_RequestMagic || request.version != s_RequestVersion)
        return CrashDumpResult{ CrashDumpStatus::RequestQueued };
    if(request.spoolDirectory[0] == 0 || request.crashId[0] == 0)
        return CrashDumpResult{ CrashDumpStatus::RequestQueued };

    Alloc::GlobalArena arena("NWB::Core::Crash::PackageStatus");
    if(PathIsDirectory(RequestBucketDirectory(arena, request, PackageNames::s_UploadedDirectoryName)))
        return CrashDumpResult{ CrashDumpStatus::Uploaded };
    if(PathIsDirectory(RequestBucketDirectory(arena, request, PackageNames::s_FailedDirectoryName)))
        return CrashDumpResult{ CrashDumpStatus::UploadFailed };
    if(PathIsDirectory(RequestBucketDirectory(arena, request, PackageNames::s_UploadingDirectoryName)))
        return CrashDumpResult{ CrashDumpStatus::PackageWritten };
    if(PathIsDirectory(RequestBucketDirectory(arena, request, PackageNames::s_PendingDirectoryName)))
        return CrashDumpResult{ CrashDumpStatus::PackageWritten };

    return CrashDumpResult{ CrashDumpStatus::RequestQueued };
}

static bool UploadPackageDirectory(
    Alloc::GlobalArena& arena,
    const Path& spoolDirectory,
    const Path& packageDirectory,
    const CrashString& url,
    const AStringView crashUploadToken
){
    if(url.empty())
        return false;

    const Path uploadingPackageDirectory = UploadingDirectory(spoolDirectory) / packageDirectory.filename();
    if(!::MovePathToDirectory(packageDirectory, UploadingDirectory(spoolDirectory)))
        return false;

    static_cast<void>(WriteUploadAttemptText(arena, uploadingPackageDirectory, PackageNames::s_UploadAttemptUploadingState));

    CrashBytes archiveBytes{arena};
    if(!BuildPackageArchive(arena, uploadingPackageDirectory, archiveBytes)){
        static_cast<void>(::MovePathToDirectory(uploadingPackageDirectory, FailedDirectory(spoolDirectory)));
        return false;
    }

    if(UploadPackage(arena, url, archiveBytes, crashUploadToken)){
        static_cast<void>(WriteUploadAttemptText(arena, uploadingPackageDirectory, PackageNames::s_UploadAttemptUploadedState));
        return ::MovePathToDirectory(uploadingPackageDirectory, UploadedDirectory(spoolDirectory));
    }

    static_cast<void>(WriteUploadAttemptText(arena, uploadingPackageDirectory, PackageNames::s_UploadAttemptRetryPendingState));
    static_cast<void>(::MovePathToDirectory(uploadingPackageDirectory, PendingDirectory(spoolDirectory)));
    return false;
}

static bool RecoverUploadingPackageDirectories(Alloc::GlobalArena& arena, const Path& spoolDirectory){
    const Path uploadingDirectory = UploadingDirectory(spoolDirectory);
    ErrorCode error;
    if(!IsDirectory(uploadingDirectory, error) || error)
        return !error;

    DirectoryIterator directory(uploadingDirectory, error);
    if(error)
        return false;

    bool ok = true;
    for(const auto& entry : directory){
        ErrorCode entryError;
        if(!IsDirectory(entry.path(), entryError) || entryError || !IsSafePackageName(arena, entry.path()))
            continue;

        if(!WriteUploadAttemptText(arena, entry.path(), PackageNames::s_UploadAttemptRetryInterruptedState))
            ok = false;
        if(!::MovePathToDirectory(entry.path(), PendingDirectory(spoolDirectory)))
            ok = false;
    }

    return ok;
}

bool UploadCrashPackage(const CrashRequest& request){
    if(request.magic != s_RequestMagic || request.version != s_RequestVersion)
        return false;
    if(request.spoolDirectory[0] == 0)
        return false;

    Alloc::GlobalArena arena("NWB::Core::Crash::PackageUpload");
    const Path spoolDirectory(arena, request.spoolDirectory);
    const bool recoveryOk = RecoverUploadingPackageDirectories(arena, spoolDirectory);

    const CrashString url = CrashUploadUrl(arena, request.logServerUrl);
    if(url.empty())
        return false;

    if(curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK)
        return false;

    const Path packageDirectory = RequestPendingDirectory(arena, request);
    const CrashString packageName = PathToString<char>(arena, packageDirectory.filename());
    const bool uploaded = UploadPackageDirectory(arena, spoolDirectory, packageDirectory, url, AStringView(request.crashUploadToken));
    static_cast<void>(ApplyCrashSpoolRetention(
        arena,
        spoolDirectory,
        request.spoolRetention,
        AStringView(packageName.data(), packageName.size())
    ));
    return uploaded && recoveryOk;
}

#if defined(NWB_PLATFORM_ANDROID)
static void WriteAndroidCollectionNote(Alloc::PersistentArena& arena, const CrashRequest& request){
    CrashStringT<Alloc::PersistentArena> text{arena};
    text += "application_exit_info=not_collected_by_native_layer\n";
    text += "detail=Java/Kotlin host should attach ApplicationExitInfo tombstone data on next launch\n";
    static_cast<void>(WriteCrashTextFile(RequestPendingDirectory(arena, request) / PackageNames::s_AndroidCollectionFileName, text));
}

static void CollectAndroidEmergencyRecord(const CrashUploadSnapshot& snapshot){
    Alloc::PersistentArena& dumpArena = DumpArena();
    const ::Path<Alloc::PersistentArena> recordPath =
        ::Path<Alloc::PersistentArena>(dumpArena, snapshot.spoolDirectory) / PackageNames::s_AndroidEmergencyRequestFileName
    ;

    CrashBytesT<Alloc::PersistentArena> bytes{dumpArena};
    ErrorCode readError;
    if(!ReadBinaryFile(recordPath, bytes, readError) || bytes.size() < sizeof(CrashRequest))
        return;

    CrashRequest request;
    const usize offset = bytes.size() - sizeof(CrashRequest);
    NWB_MEMCPY(&request, sizeof(request), bytes.data() + offset, sizeof(request));
    if(request.magic == s_RequestMagic && request.version == s_RequestVersion){
        if(WriteCrashPackage(request))
            WriteAndroidCollectionNote(dumpArena, request);
    }

    CrashBytesT<Alloc::PersistentArena> empty{dumpArena};
    static_cast<void>(WriteBinaryFile(recordPath, empty));
}
#else
static void CollectAndroidEmergencyRecord(const CrashUploadSnapshot& snapshot){
    static_cast<void>(snapshot);
}
#endif

bool FlushPendingCrashReportsImpl(Alloc::GlobalArena& arena, const CrashUploadSnapshot& snapshot){
    CollectAndroidEmergencyRecord(snapshot);

    if(snapshot.spoolDirectory[0] == 0)
        return false;

    const Path spoolDirectory(arena, snapshot.spoolDirectory);
    const bool recoveryOk = RecoverUploadingPackageDirectories(arena, spoolDirectory);
    bool retentionOk = ApplyCrashSpoolRetention(arena, spoolDirectory, snapshot.spoolRetention);

    const CrashString url = CrashUploadUrl(arena, snapshot.logServerUrl);
    if(url.empty())
        return false;

    if(curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK)
        return false;

    bool allUploaded = true;
    const Path pendingDirectory = PendingDirectory(spoolDirectory);
    ErrorCode error;
    if(!IsDirectory(pendingDirectory, error) || error)
        return !error && recoveryOk && retentionOk;

    DirectoryIterator directory(pendingDirectory, error);
    if(error)
        return false;

    for(const auto& entry : directory){
        ErrorCode entryError;
        if(!IsDirectory(entry.path(), entryError) || entryError || !IsSafePackageName(arena, entry.path()))
            continue;

        if(!UploadPackageDirectory(arena, spoolDirectory, entry.path(), url, AStringView(snapshot.crashUploadToken)))
            allUploaded = false;
    }

    retentionOk = ApplyCrashSpoolRetention(arena, spoolDirectory, snapshot.spoolRetention) && retentionOk;
    return allUploaded && recoveryOk && retentionOk;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

