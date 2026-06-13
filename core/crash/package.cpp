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
    return spoolDirectory / "pending";
}

template<typename ArenaT>
static ::Path<ArenaT> UploadedDirectory(const ::Path<ArenaT>& spoolDirectory){
    return spoolDirectory / "uploaded";
}

template<typename ArenaT>
static ::Path<ArenaT> FailedDirectory(const ::Path<ArenaT>& spoolDirectory){
    return spoolDirectory / "failed";
}

template<typename ArenaT>
static ::Path<ArenaT> RequestPendingDirectory(ArenaT& arena, const CrashRequest& request){
    return ::Path<ArenaT>(arena, request.spoolDirectory) / "pending" / request.crashId;
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
    static_cast<void>(EnsureDirectories(FailedDirectory(spoolDirectory), error));
    return !error;
}

template bool EnsureCrashSpoolDirectories(const ::Path<Alloc::PersistentArena>& spoolDirectory);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
static void AppendUnsignedText(CrashStringT<ArenaT>& out, const u64 value){
    char buffer[32] = {};
    AppendUnsignedToFixedBuffer(buffer, value);
    out += buffer;
}

template<typename ArenaT>
static void WriteCrashTextFile(const ::Path<ArenaT>& path, const CrashStringT<ArenaT>& text){
    static_cast<void>(WriteTextFile(path, AStringView(text.data(), text.size())));
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

template<typename ArenaT>
static CrashStringT<ArenaT> BuildManifest(ArenaT& arena, const CrashRequest& request){
    CrashStringT<ArenaT> manifest{arena};
    manifest.reserve(2048u);
    manifest += "{\n";
    manifest += "  \"format\": \"nwb-crash-package-v1\",\n";
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
    manifest += ",\n  \"dump_detail_mode\": ";
    AppendJsonEscaped(manifest, request.dumpDetailMode == DumpDetailMode::Full ? "full" : "small");
    manifest += ",\n  \"gpu_dumps_enabled\": ";
    manifest += request.enableGpuDumps ? "true" : "false";
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
    text += "\n";
    return text;
}

template<typename ArenaT>
static bool WriteCrashPackageBasics(ArenaT& arena, const CrashRequest& request){
    const ::Path<ArenaT> packageDirectory = RequestPendingDirectory(arena, request);
    ErrorCode error;
    static_cast<void>(EnsureDirectories(packageDirectory, error));
    if(error)
        return false;

    WriteCrashTextFile(packageDirectory / "manifest.json", BuildManifest(arena, request));
    WriteCrashTextFile(packageDirectory / "metadata.txt", BuildMetadataText(arena, request));
    WriteCrashTextFile(packageDirectory / "breadcrumbs.txt", BuildBreadcrumbText(arena, request));
    WriteCrashTextFile(packageDirectory / "emergency.txt", BuildEmergencyText(arena, request));
    return true;
}

template<typename ArenaT>
static void WriteGpuCrashAttachments(ArenaT& arena, const CrashRequest& request){
    if(!request.enableGpuDumps)
        return;

    const ::Path<ArenaT> packageDirectory = RequestPendingDirectory(arena, request);
    const ::Path<ArenaT> gpuDirectory = packageDirectory / "gpu";
    ErrorCode error;
    static_cast<void>(EnsureDirectories(gpuDirectory, error));
    if(error)
        return;

    CrashStringT<ArenaT> status{arena};
    for(usize i = 0u; i < g_State.gpuProviderCount; ++i){
        const GpuCrashProvider& provider = g_State.gpuProviders[i];
        if(!provider.writeAttachment)
            continue;

        status += "provider=unavailable_in_external_handler\n";
    }

    if(status.empty())
        status = "no gpu crash providers registered in this process\n";

    WriteCrashTextFile(packageDirectory / "gpu_attachments.txt", status);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_WINDOWS)
template<typename ArenaT>
static bool WriteWindowsMinidump(ArenaT& arena, const CrashRequest& request){
    const ::Path<ArenaT> dumpPath = RequestPendingDirectory(arena, request) / "process.dmp";

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

    const BOOL ok = MiniDumpWriteDump(process, request.processId, dumpFile, dumpType, nullptr, nullptr, nullptr);

    CloseHandle(dumpFile);
    CloseHandle(process);
    return ok == TRUE;
}
#endif

#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
template<typename ArenaT>
static void CopyProcFile(ArenaT& arena, const CrashRequest& request, const char* procName, const char* outputName){
    char procPath[128] = {};
    CopyFixedBuffer(procPath, "/proc/");
    AppendUnsignedToFixedBuffer(procPath, request.processId);
    AppendFixedBuffer(procPath, "/");
    AppendFixedBuffer(procPath, procName);

    InputFileStream input(procPath, s_FileOpenBinary);
    if(!input.is_open())
        return;

    OutputFileStream output((RequestPendingDirectory(arena, request) / outputName).c_str(), s_FileOpenBinary | s_FileOpenTruncate);
    if(output.is_open())
        output << input.rdbuf();
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
        WriteCrashTextFile(
            RequestPendingDirectory(arena, request) / "symbolication.txt",
            CrashStringT<ArenaT>(
                dumpWritten
                    ? "minidump captured; server-side PDB symbolication pending\n"
                    : "minidump capture failed; metadata package only\n",
                arena
            )
        );
    }
#elif defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
    if(request.platform == PlatformKind::Linux){
        CopyProcFile(arena, request, "maps", "proc_maps.txt");
        CopyProcFile(arena, request, "status", "proc_status.txt");
        WriteCrashTextFile(
            RequestPendingDirectory(arena, request) / "symbolication.txt",
            CrashStringT<ArenaT>("linux crash package captured; OS core/tombstone equivalent and DWARF symbolication pending\n", arena)
        );
    }
#else
    WriteCrashTextFile(
        RequestPendingDirectory(arena, request) / "symbolication.txt",
        CrashStringT<ArenaT>("native platform crash artifact expected; server-side symbolication pending\n", arena)
    );
#endif

    return true;
}

bool WriteCrashPackage(const CrashRequest& request){
    return WriteCrashPackageWithArena(DumpArena(), request);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static CrashString GenericPathText(Alloc::GlobalArena& arena, const Path& path){
    CrashString text = PathToString<char>(arena, path);
    for(char& ch : text){
        if(ch == '\\')
            ch = '/';
    }
    return text;
}

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
    char buffer[32] = {};
    AppendUnsignedToFixedBuffer(buffer, value);
    AppendArchiveText(out, buffer);
}

static bool BuildPackageArchive(Alloc::GlobalArena& arena, const Path& packageDirectory, CrashBytes& outArchive){
    outArchive.clear();
    AppendArchiveText(outArchive, "NWBCRASHPKG 1\n");

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

        const CrashString pathText = GenericPathText(arena, entry.path().lexically_relative(packageDirectory));
        AppendArchiveText(outArchive, "FILE ");
        AppendArchiveText(outArchive, AStringView(pathText.data(), pathText.size()));
        AppendArchiveText(outArchive, " ");
        AppendArchiveUnsigned(outArchive, fileBytes.size());
        AppendArchiveText(outArchive, "\n");
        outArchive.insert(outArchive.end(), fileBytes.begin(), fileBytes.end());
        AppendArchiveText(outArchive, "\nEND\n");
    }

    return true;
}

static CrashString CrashUploadUrl(Alloc::GlobalArena& arena, const char* logServerUrl){
    CrashString url{arena};
    if(logServerUrl)
        url += logServerUrl;
    if(url.empty())
        return url;

    constexpr AStringView suffix("/crash");
    if(url.size() >= suffix.size() && AStringView(url.data() + url.size() - suffix.size(), suffix.size()) == suffix)
        return url;
    if(!url.empty() && url.back() == '/')
        url += "crash";
    else
        url += "/crash";
    return url;
}

static CrashString CrashUploadUrl(Alloc::GlobalArena& arena){
    return CrashUploadUrl(arena, g_State.logServerUrl);
}

static bool UploadPackage(const CrashString& url, const CrashBytes& archiveBytes){
    CURL* curl = curl_easy_init();
    if(!curl)
        return false;

    bool ok = true;
    ok = ok && curl_easy_setopt(curl, CURLOPT_URL, url.c_str()) == CURLE_OK;
    ok = ok && curl_easy_setopt(curl, CURLOPT_POST, 1L) == CURLE_OK;
    ok = ok && curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L) == CURLE_OK;
    ok = ok && curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 1000L) == CURLE_OK;
    ok = ok && curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 5000L) == CURLE_OK;
    ok = ok && curl_easy_setopt(curl, CURLOPT_POSTFIELDS, reinterpret_cast<const char*>(archiveBytes.data())) == CURLE_OK;
    ok = ok && curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(archiveBytes.size())) == CURLE_OK;

    if(ok)
        ok = curl_easy_perform(curl) == CURLE_OK;

    long responseCode = 0;
    if(ok)
        ok = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode) == CURLE_OK && responseCode >= 200 && responseCode < 300;

    curl_easy_cleanup(curl);
    return ok;
}

static void MovePackage(const Path& from, const Path& toDirectory){
    ErrorCode error;
    static_cast<void>(EnsureDirectories(toDirectory, error));
    if(error)
        return;

    error.clear();
    const Path destination = toDirectory / from.filename();
    static_cast<void>(RemoveAllIfExists(destination, error));
    error.clear();
    static_cast<void>(RenamePath(from, destination, error));
}

static bool UploadPackageDirectory(Alloc::GlobalArena& arena, const Path& spoolDirectory, const Path& packageDirectory, const CrashString& url){
    if(url.empty())
        return false;

    CrashBytes archiveBytes{arena};
    if(!BuildPackageArchive(arena, packageDirectory, archiveBytes)){
        MovePackage(packageDirectory, FailedDirectory(spoolDirectory));
        return false;
    }

    if(UploadPackage(url, archiveBytes)){
        MovePackage(packageDirectory, UploadedDirectory(spoolDirectory));
        return true;
    }

    return false;
}

bool UploadCrashPackage(const CrashRequest& request){
    if(request.magic != s_RequestMagic || request.version != s_RequestVersion)
        return false;
    if(request.spoolDirectory[0] == 0)
        return false;

    Alloc::GlobalArena arena("NWB::Core::Crash::PackageUpload");
    const CrashString url = CrashUploadUrl(arena, request.logServerUrl);
    if(url.empty())
        return false;

    curl_global_init(CURL_GLOBAL_ALL);

    const Path spoolDirectory(arena, request.spoolDirectory);
    const Path packageDirectory = RequestPendingDirectory(arena, request);
    return UploadPackageDirectory(arena, spoolDirectory, packageDirectory, url);
}

#if defined(NWB_PLATFORM_ANDROID)
static void CollectAndroidEmergencyRecord(){
    Alloc::PersistentArena& dumpArena = DumpArena();
    const ::Path<Alloc::PersistentArena> recordPath =
        ::Path<Alloc::PersistentArena>(dumpArena, g_State.spoolDirectoryText) / "last_android_native_crash_request.bin"
    ;

    CrashBytesT<Alloc::PersistentArena> bytes{dumpArena};
    ErrorCode readError;
    if(!ReadBinaryFile(recordPath, bytes, readError) || bytes.size() < sizeof(CrashRequest))
        return;

    CrashRequest request;
    const usize offset = bytes.size() - sizeof(CrashRequest);
    NWB_MEMCPY(&request, sizeof(request), bytes.data() + offset, sizeof(request));
    if(request.magic == s_RequestMagic && request.version == s_RequestVersion)
        static_cast<void>(WriteCrashPackage(request));

    CrashBytesT<Alloc::PersistentArena> empty{dumpArena};
    static_cast<void>(WriteBinaryFile(recordPath, empty));
}
#else
static void CollectAndroidEmergencyRecord(){
}
#endif

bool FlushPendingCrashReportsImpl(Alloc::GlobalArena& arena){
    CollectAndroidEmergencyRecord();

    const CrashString url = CrashUploadUrl(arena);
    if(url.empty())
        return false;

    curl_global_init(CURL_GLOBAL_ALL);

    bool allUploaded = true;
    const Path spoolDirectory(arena, g_State.spoolDirectoryText);
    const Path pendingDirectory = PendingDirectory(spoolDirectory);
    ErrorCode error;
    if(!IsDirectory(pendingDirectory, error) || error)
        return true;

    DirectoryIterator directory(pendingDirectory, error);
    if(error)
        return false;

    for(const auto& entry : directory){
        ErrorCode entryError;
        if(!IsDirectory(entry.path(), entryError) || entryError || !IsSafePackageName(arena, entry.path()))
            continue;

        if(!UploadPackageDirectory(arena, spoolDirectory, entry.path(), url))
            allUploaded = false;
    }

    return allUploaded;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
