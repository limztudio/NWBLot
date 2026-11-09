// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/integration/crash/crash_test_paths.h>

#include <logger/server/crash_ingest.h>
#include <logger/server/crash_paths.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace LoggerServerCrash{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CrashTestText = AString<Core::Alloc::GlobalArena>;
using CrashTestPath = ::Path<Core::Alloc::GlobalArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ManifestEventField{
    enum Enum : u8{
        Include,
        Omit,
    };
};

struct ManifestTriggerFields{
    AStringView category;
    AStringView expression;
    AStringView message;
    AStringView file;
    u64 line = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline CrashTestPath StorageDirectory(Core::Alloc::GlobalArena& arena, const AStringView testGroup){
    return CrashTestPaths::TestCaseDirectory(arena, testGroup) / "storage";
}
[[nodiscard]] inline CrashTestPath ArchiveInputDirectory(Core::Alloc::GlobalArena& arena, const AStringView testGroup){
    return CrashTestPaths::TestCaseDirectory(arena, testGroup) / "input";
}
[[nodiscard]] CrashTestPath ArchiveFileName(Core::Alloc::GlobalArena& arena, AStringView stem);
[[nodiscard]] inline CrashTestPath ArchivePath(Core::Alloc::GlobalArena& arena, const AStringView testGroup, const AStringView stem){
    return ArchiveInputDirectory(arena, testGroup) / ArchiveFileName(arena, stem);
}
[[nodiscard]] inline CrashTestPath ExtractedPackageDirectory(Core::Alloc::GlobalArena& arena, const AStringView testGroup, const AStringView stem){
    return StorageDirectory(arena, testGroup) / Log::s_CrashExtractedDirectoryName / stem;
}
[[nodiscard]] inline CrashTestPath RawArchivePath(Core::Alloc::GlobalArena& arena, const AStringView testGroup, const AStringView stem){
    return StorageDirectory(arena, testGroup) / Log::s_CrashRawDirectoryName / ArchiveFileName(arena, stem);
}
[[nodiscard]] inline CrashTestPath InvalidArchivePath(Core::Alloc::GlobalArena& arena, const AStringView testGroup, const AStringView stem){
    return StorageDirectory(arena, testGroup) / Log::s_CrashInvalidDirectoryName / ArchiveFileName(arena, stem);
}
void AppendArchiveFile(AStringView relativePath, AStringView content, CrashTestText& archive);
void BeginArchiveWithManifest(
    AStringView crashId,
    AStringView platform,
    AStringView event,
    AStringView reasonKind,
    u64 reasonCode,
    ManifestEventField::Enum eventField,
    const ManifestTriggerFields& trigger,
    Core::Alloc::GlobalArena& arena,
    CrashTestText& archive
);
[[nodiscard]] CrashTestText BuildManifest(
    AStringView crashId,
    AStringView platform,
    AStringView event,
    AStringView reasonKind,
    u64 reasonCode,
    ManifestEventField::Enum eventField,
    const ManifestTriggerFields& trigger,
    Core::Alloc::GlobalArena& arena
);
[[nodiscard]] b8 WriteArchive(Core::Alloc::GlobalArena& arena, AStringView testGroup, AStringView stem, AStringView archive);
[[nodiscard]] b8 WriteArchiveBytes(Core::Alloc::GlobalArena& arena, AStringView testGroup, AStringView stem, Span<const u8> archive);
[[nodiscard]] Expected<CrashTestText, ErrorCode> ReadServerSymbolication(Core::Alloc::GlobalArena& arena, AStringView testGroup, AStringView stem);
[[nodiscard]] Log::CrashIngestConfig MakeIngestConfig(Core::Alloc::GlobalArena& arena, AStringView testGroup);
[[nodiscard]] Expected<CrashTestPath, ErrorCode> WaitForTriggerPackage(Core::Alloc::GlobalArena& arena, const CrashTestPath& pendingDirectory, AStringView category, AStringView expression, AStringView message, AStringView file);
void BuildLinuxCrashArchive(AStringView crashId, Core::Alloc::GlobalArena& arena, CrashTestText& archive);
void PreserveObservedReport(Core::Alloc::GlobalArena& arena, AStringView report, AStringView suffix);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <core/crash/package_internal.h>
#include <core/crash/package_names.h>

#include <global/environment.h>
#include <global/error_code.h>
#include <global/filesystem/directory_iterator.h>
#include <global/filesystem/operations.h>
#include <global/thread.h>
#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace LoggerServerCrash{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core::Crash::PackageNames;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CrashTestPath ArchiveFileName(Core::Alloc::GlobalArena& arena, const AStringView stem){
    CrashTestPath fileName(arena, stem);
    fileName.replaceExtension(Log::s_CrashUploadArchiveFileExtension);
    return fileName;
}

void AppendArchiveFile(const AStringView relativePath, const AStringView content, CrashTestText& archive){
    Array<char, 32u> sizeBuffer = {};
    archive += s_ArchiveFileHeaderPrefix;
    archive += relativePath;
    archive += " ";
    archive += FormatDecimal(content.size(), sizeBuffer);
    archive += "\n";
    archive += content;
    archive += s_ArchiveEntryEndText;
}

void BeginArchiveWithManifest(
    const AStringView crashId,
    const AStringView platform,
    const AStringView event,
    const AStringView reasonKind,
    const u64 reasonCode,
    const ManifestEventField::Enum eventField,
    const ManifestTriggerFields& trigger,
    Core::Alloc::GlobalArena& arena,
    CrashTestText& archive
){
    archive += s_ArchiveHeaderText;
    const CrashTestText manifest = BuildManifest(crashId, platform, event, reasonKind, reasonCode, eventField, trigger, arena);
    AppendArchiveFile(s_ManifestFileName, AStringView(manifest.data(), manifest.size()), archive);
}

CrashTestText BuildManifest(
    const AStringView crashId,
    const AStringView platform,
    const AStringView event,
    const AStringView reasonKind,
    const u64 reasonCode,
    const ManifestEventField::Enum eventField,
    const ManifestTriggerFields& trigger,
    Core::Alloc::GlobalArena& arena
){
    CrashTestText manifest(arena);
    Array<char, 32u> reasonCodeBuffer = {};
    Array<char, 32u> triggerLineBuffer = {};
    manifest += "{\n";
    manifest += "  \"format\": \"";
    manifest += s_ManifestFormatValue;
    manifest += "\",\n";
    manifest += "  \"crash_id\": \"";
    manifest += crashId;
    manifest += "\",\n  \"application\": \"logger_server_test\",\n";
    manifest += "  \"version\": \"1\",\n";
    manifest += "  \"build_id\": \"unit-test\",\n";
    manifest += "  \"abi\": \"test-abi\",\n";
    manifest += "  \"platform\": \"";
    manifest += platform;
    manifest += "\",\n  \"reason_kind\": \"";
    manifest += reasonKind;
    manifest += "\",\n  \"reason_code\": ";
    manifest += FormatDecimal(static_cast<usize>(reasonCode), reasonCodeBuffer);
    manifest += ",\n  \"process_id\": 1,\n";
    manifest += "  \"thread_id\": 7,\n";
    manifest += "  \"has_exception_context\": false,\n";
    manifest += "  \"fault_address\": 0,\n";
    manifest += "  \"instruction_pointer\": 0,\n";
    manifest += "  \"stack_pointer\": 0,\n";
    manifest += "  \"frame_pointer\": 0,\n";
    if(eventField == ManifestEventField::Include){
        manifest += "  \"event\": \"";
        manifest += event;
        manifest += "\",\n";
    }
    manifest += "  \"trigger_category\": \"";
    manifest += trigger.category;
    manifest += "\",\n";
    manifest += "  \"trigger_expression\": \"";
    manifest += trigger.expression;
    manifest += "\",\n";
    manifest += "  \"trigger_message\": \"";
    manifest += trigger.message;
    manifest += "\",\n";
    manifest += "  \"trigger_file\": \"";
    manifest += trigger.file;
    manifest += "\",\n";
    manifest += "  \"trigger_line\": ";
    manifest += FormatDecimal(static_cast<usize>(trigger.line), triggerLineBuffer);
    manifest += ",\n";
    manifest += "  \"dump_detail_mode\": \"small\",\n";
    manifest += "  \"artifact_strategy\": \"unit_test\",\n";
    manifest += "  \"handler_lifetime\": \"unit_test\"\n";
    manifest += "}\n";
    return manifest;
}

b8 WriteArchive(Core::Alloc::GlobalArena& arena, const AStringView testGroup, const AStringView stem, const AStringView archive){
    if(!EnsureDirectories(ArchiveInputDirectory(arena, testGroup)))
        return false;

    return WriteTextFile(ArchivePath(arena, testGroup, stem), archive);
}

b8 WriteArchiveBytes(Core::Alloc::GlobalArena& arena, const AStringView testGroup, const AStringView stem, const Span<const u8> archive){
    if(!EnsureDirectories(ArchiveInputDirectory(arena, testGroup)))
        return false;

    return WriteBinaryFile(ArchivePath(arena, testGroup, stem), archive);
}

Expected<CrashTestText, ErrorCode> ReadServerSymbolication(Core::Alloc::GlobalArena& arena, const AStringView testGroup, const AStringView stem){
    CrashTestText report(arena);
    const auto result = ReadTextFile(ExtractedPackageDirectory(arena, testGroup, stem) / Log::s_ServerSymbolicationFileName, report);
    if(!result)
        return MakeUnexpected(result.error());
    return report;
}

Log::CrashIngestConfig MakeIngestConfig(Core::Alloc::GlobalArena& arena, const AStringView testGroup){
    Log::CrashIngestConfig config(arena);
    config.storageDirectory = StorageDirectory(arena, testGroup);
    return config;
}

static b8 ManifestContainsStringValue(Core::Alloc::GlobalArena& arena, const AStringView manifest, const AStringView key, const AStringView value){
    if(value.empty())
        return true;

    CrashTestText needle(arena);
    needle += "\"";
    needle += key;
    needle += "\": \"";
    needle += value;
    needle += "\"";
    return Contains(manifest, AStringView(needle.data(), needle.size()));
}

static b8 TriggerPackageContains(Core::Alloc::GlobalArena& arena, const CrashTestPath& packageDirectory, const AStringView category, const AStringView expression, const AStringView message, const AStringView file){
    CrashTestText manifest(arena);
    if(!ReadTextFile(packageDirectory / s_ManifestFileName, manifest))
        return false;

    if(!ManifestContainsStringValue(arena, manifest, s_ManifestTriggerCategoryKey, category))
        return false;
    if(!ManifestContainsStringValue(arena, manifest, s_ManifestTriggerExpressionKey, expression))
        return false;
    if(!ManifestContainsStringValue(arena, manifest, s_ManifestTriggerMessageKey, message))
        return false;
    if(!file.empty() && !Contains(manifest, file))
        return false;

    return true;
}

static Expected<CrashTestPath> FindTriggerPackage(Core::Alloc::GlobalArena& arena, const CrashTestPath& pendingDirectory, const AStringView category, const AStringView expression, const AStringView message, const AStringView file){
    const auto directory = DirectoryIterator<Core::Alloc::GlobalArena>::Create(pendingDirectory);
    if(!directory)
        return MakeUnexpected(Failure{});

    for(const auto& entry : *directory){
        const auto isDirectory = IsDirectory(entry.path());
        if(!isDirectory || !*isDirectory)
            continue;
        if(!TriggerPackageContains(arena, entry.path(), category, expression, message, file))
            continue;
        return entry.path();
    }
    return MakeUnexpected(Failure{});
}

Expected<CrashTestPath, ErrorCode> WaitForTriggerPackage(Core::Alloc::GlobalArena& arena, const CrashTestPath& pendingDirectory, const AStringView category, const AStringView expression, const AStringView message, const AStringView file){
    constexpr u32 s_TimeoutMilliseconds = 3000u;
    constexpr u32 s_PollMilliseconds = 10u;
    const Timer deadline = TimerAddMS(TimerNow(), s_TimeoutMilliseconds);
    for(;;){
        const auto directory = IsDirectory(pendingDirectory);
        if(!directory)
            return MakeUnexpected(directory.error());
        if(*directory){
            auto packageDirectory = FindTriggerPackage(arena, pendingDirectory, category, expression, message, file);
            if(packageDirectory)
                return Move(*packageDirectory);
        }
        const Timer now = TimerNow();
        if(now >= deadline)
            return MakeUnexpected(MakeErrorCode(ErrorCondition::timed_out));
        constexpr i32 s_MinimumRemainingMilliseconds = 1;
        const u32 remainingMilliseconds = static_cast<u32>(Max(DurationInMS<i64>(deadline, now), i64(s_MinimumRemainingMilliseconds)));
        SleepMS(Min(s_PollMilliseconds, remainingMilliseconds));
    }
}

void BuildLinuxCrashArchive(const AStringView crashId, Core::Alloc::GlobalArena& arena, CrashTestText& archive){
    BeginArchiveWithManifest(crashId, "linux", "crash", "signal", 11u, ManifestEventField::Include, ManifestTriggerFields{}, arena, archive);
    AppendArchiveFile(s_CpuContextFileName, "fault_address=0\ninstruction_pointer=4198964\nstack_pointer=0\nframe_pointer=0\n", archive);
    AppendArchiveFile(s_CallstackFileName, "#0 0x0000000000401234\n#1 0x0000000000401240\n", archive);
    AppendArchiveFile(s_ProcMapsFileName, "00400000-00452000 r-xp 00000000 08:01 123 /tmp/nwb_loader\n", archive);
}

static CrashTestPath ObservedReportPath(Core::Alloc::GlobalArena& arena, const CrashTestPath& basePath, const AStringView suffix){
    if(suffix.empty())
        return basePath;

    CrashTestText fileName(arena);
    const CrashTestText stem = PathToString<char>(arena, basePath.stemView());
    const CrashTestText extension = PathToString<char>(arena, basePath.extensionView());
    fileName += AStringView(stem.data(), stem.size());
    fileName += ".";
    fileName += suffix;
    fileName += AStringView(extension.data(), extension.size());
    return basePath.parentPath() / AStringView(fileName.data(), fileName.size());
}

void PreserveObservedReport(Core::Alloc::GlobalArena& arena, const AStringView report, const AStringView suffix){
    const auto outputPathText = ReadEnvironmentVariable(arena, "NWB_LOGSERVER_CRASH_TEST_OBSERVE_PATH");
    EXPECT_TRUE(outputPathText);
    if(!outputPathText || !*outputPathText || (*outputPathText)->empty())
        return;

    const CrashTestPath baseOutputPath(arena, AStringView((*outputPathText)->data(), (*outputPathText)->size()));
    const CrashTestPath outputPath = ObservedReportPath(arena, baseOutputPath, suffix);
    EXPECT_TRUE(EnsureDirectories(outputPath.parentPath()));
    EXPECT_TRUE(WriteTextFile(outputPath, report));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <logger/server/crash_report_composer.h>
#include <tests/common/test_context.h>
#include <tests/integration/logger_server/name_cases.adhd.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_crash_report_composer_tests{


using namespace NWB;


TEST(CrashReportComposerTests, TombstoneStackMovesBeforeDetailsWithoutBorrowingClearedText){
    Tests::TestArena<> arena;
    Core::Alloc::ScratchArena scratch{ Name(NWB::Tests::LoggerCrashCases::s_LoggerCrashReportComposerScratch) };
    Log::CrashPackageSummary summary(arena.arena);
    summary.triggerMessage = "fault";
    Log::CrashReportText details(arena.arena);
    details += "package=one\n[tombstone_callstack]\nframe-one\n[metadata]\nkey=value\n";

    const auto report = Log::LoggerCrashSymbolicateDetail::CrashReportComposer::BuildOrderedCrashReport(summary, scratch, Move(details));
    EXPECT_EQ(AStringView(report.data(), report.size()), AStringView("fault\n\ncallstack:\nframe-one\n\ndetails:\npackage=one\n[metadata]\nkey=value\n"));
}

TEST(CrashReportComposerTests, EmptyPrimaryStackTakesPrecedenceOverTombstone){
    Tests::TestArena<> arena;
    Core::Alloc::ScratchArena scratch{ Name(NWB::Tests::LoggerCrashCases::s_LoggerCrashReportComposerScratch) };
    Log::CrashPackageSummary summary(arena.arena);
    Log::CrashReportText details(arena.arena);
    details += "[callstack]\n\n[tombstone_callstack]\nfallback\n";

    const auto report = Log::LoggerCrashSymbolicateDetail::CrashReportComposer::BuildOrderedCrashReport(summary, scratch, Move(details));
    EXPECT_EQ(AStringView(report.data(), report.size()), AStringView("callstack:\n<unavailable>\n\ndetails:\n[tombstone_callstack]\nfallback\n"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <logger/client/module.h>

#include <microhttpd.h>

#include <global/scope_exit.h>
#include <global/timer.h>

#if defined(NWB_PLATFORM_LINUX)
#include <arpa/inet.h>
#include <netinet/in.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_logclient_shutdown_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_ShutdownMessageCount = 32u;
inline constexpr u32 s_FirstRequestDelayMilliseconds = 250u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ShutdownCaptureServer final : NoCopy{
private:
    static MHD_Result RequestCallback(void* serverContext, MHD_Connection* connection, const char* url, const char* method, const char* version, const char* uploadData, size_t* uploadDataSize, void** connectionContext)noexcept;


public:
    explicit ShutdownCaptureServer(const u32 firstStatus = MHD_HTTP_OK, const b8 repeatStatus = false, const b8 holdFirstRequest = false)noexcept
        : m_firstStatus(firstStatus), m_repeatStatus(repeatStatus), m_holdFirstRequest(holdFirstRequest){}
    ~ShutdownCaptureServer()noexcept;


public:
    [[nodiscard]] b8 start(u16& outPort)noexcept;

    [[nodiscard]] inline u32 requestCount()const noexcept{ return m_requestCount.load(MemoryOrder::acquire); }
    [[nodiscard]] inline b8 processingFirstRequest()const noexcept{ return m_delayedFirstRequest.load(MemoryOrder::acquire); }
    void releaseFirstRequest()noexcept{ m_releaseFirstRequest.store(true, MemoryOrder::release); }
    [[nodiscard]] b8 requestWasTelemetry(const u32 index)const noexcept{ return m_telemetryRequests[index]; }
    [[nodiscard]] usize firstPayloadSize()const noexcept{ return m_payloadOverflow ? 0u : m_payloadSizes[0]; }
    [[nodiscard]] b8 firstPayloadRetried()const noexcept{
        return !m_payloadOverflow && m_payloadSizes[0] != 0u && m_payloadSizes[0] == m_payloadSizes[1]
            && NWB_MEMCMP(m_payloads[0].data(), m_payloads[1].data(), m_payloadSizes[0]) == 0;
    }


private:
    Array<Array<char, 8192u>, 2u> m_payloads = {};
    Array<usize, 2u> m_payloadSizes = {};
    Array<b8, 2u> m_telemetryRequests = {};
    b8 m_payloadOverflow = false;
    u32 m_firstStatus = MHD_HTTP_OK;
    b8 m_repeatStatus = false;
    b8 m_holdFirstRequest = false;
    MHD_Daemon* m_daemon = nullptr;
    Atomic<u32> m_requestCount = 0u;
    Atomic<b8> m_delayedFirstRequest = false;
    Atomic<b8> m_releaseFirstRequest = false;
};


MHD_Result ShutdownCaptureServer::RequestCallback(
    void* const serverContext,
    MHD_Connection* const connection,
    const char* const url,
    const char* const method,
    const char* const,
    const char* const uploadData,
    size_t* const uploadDataSize,
    void** const connectionContext
)noexcept{

    if(!serverContext || !connection || !method || !uploadDataSize || !connectionContext)
        return MHD_NO;
    if(NWB_STRCMP(method, "POST") != 0)
        return MHD_NO;

    if(!*connectionContext){
        *connectionContext = serverContext;
        return MHD_YES;
    }
    auto& server = *MakeNotNull(static_cast<ShutdownCaptureServer*>(serverContext));
    if(*uploadDataSize != 0u){
        const u32 requestIndex = server.m_requestCount.load(MemoryOrder::relaxed);
        if(requestIndex < server.m_payloads.size()){
            usize& length = server.m_payloadSizes[requestIndex];
            if(*uploadDataSize > server.m_payloads[requestIndex].size() - length)
                server.m_payloadOverflow = true;
            else{
                NWB_MEMCPY(server.m_payloads[requestIndex].data() + length, server.m_payloads[requestIndex].size() - length, uploadData, *uploadDataSize);
                length += *uploadDataSize;
            }
        }
        *uploadDataSize = 0u;
        return MHD_YES;
    }

    if(!server.m_delayedFirstRequest.exchange(true, MemoryOrder::acq_rel)){
        if(server.m_holdFirstRequest){
            while(!server.m_releaseFirstRequest.load(MemoryOrder::acquire))
                SleepMS(1u);
        }
        else
            SleepMS(s_FirstRequestDelayMilliseconds);
    }
    const u32 requestIndex = server.m_requestCount.load(MemoryOrder::relaxed);
    if(requestIndex < server.m_telemetryRequests.size())
        server.m_telemetryRequests[requestIndex] = url && NWB_STRCMP(url, "/telemetry") == 0;
    server.m_requestCount.fetch_add(1u, MemoryOrder::release);

    static Array<char, 1u> s_EmptyResponse = {};
    MHD_Response* const response = MHD_create_response_from_buffer(0u, s_EmptyResponse.data(), MHD_RESPMEM_PERSISTENT);
    if(!response)
        return MHD_NO;

    const u32 status = requestIndex == 0u || server.m_repeatStatus ? server.m_firstStatus : MHD_HTTP_OK;
    const MHD_Result result = MHD_queue_response(connection, status, response);
    MHD_destroy_response(response);
    return result;
}


ShutdownCaptureServer::~ShutdownCaptureServer()noexcept{
    releaseFirstRequest();
    if(m_daemon){
        MHD_stop_daemon(m_daemon);
        m_daemon = nullptr;
    }
}


b8 ShutdownCaptureServer::start(u16& outPort)noexcept{
    if(m_daemon)
        return false;

    sockaddr_in listenAddress = {};
    listenAddress.sin_family = AF_INET;
    listenAddress.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    m_daemon = MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD, 0u, nullptr, nullptr, &ShutdownCaptureServer::RequestCallback, this, MHD_OPTION_SOCK_ADDR_LEN, static_cast<socklen_t>(sizeof(listenAddress)), &listenAddress, MHD_OPTION_END);
    if(!m_daemon)
        return false;

    const MHD_DaemonInfo* const daemonInfo = MHD_get_daemon_info(m_daemon, MHD_DAEMON_INFO_LISTEN_FD);
    sockaddr_in boundAddress = {};
    socklen_t boundAddressSize = static_cast<socklen_t>(sizeof(boundAddress));
    if(
        !daemonInfo
        || daemonInfo->listen_fd == MHD_INVALID_SOCKET
        || ::getsockname(daemonInfo->listen_fd, reinterpret_cast<sockaddr*>(&boundAddress), &boundAddressSize) != 0
        || boundAddressSize != static_cast<socklen_t>(sizeof(boundAddress))
        || boundAddress.sin_family != AF_INET
        || boundAddress.sin_port == 0u
    ){
        MHD_stop_daemon(m_daemon);
        m_daemon = nullptr;
        return false;
    }

    outPort = ntohs(boundAddress.sin_port);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(LogClientShutdown, MessageProgressesWhileTelemetryBacklogRemains){
    ShutdownCaptureServer server(MHD_HTTP_OK, false, true);
    u16 port = 0u;
    ASSERT_TRUE(server.start(port));
    {
        NWB::Log::Client client;
        ScopeExit releaseGate([&server]()noexcept{ server.releaseFirstRequest(); });
        const AString<NWB::Log::LogArena> url = StringFormat(client.arena(), "http://127.0.0.1:{}", port);
        ASSERT_TRUE(client.init(AStringView(url.data(), url.size())));
        constexpr Array<u8, 1u> s_Telemetry = { 1u };
        ASSERT_TRUE(client.enqueueTelemetry(s_Telemetry.data(), s_Telemetry.size()));
        for(u32 attempt = 0u; attempt < 500u && !server.processingFirstRequest(); ++attempt)
            SleepMS(1u);
        ASSERT_TRUE(server.processingFirstRequest());
        for(u32 index = 0u; index < 3u; ++index)
            ASSERT_TRUE(client.enqueueTelemetry(s_Telemetry.data(), s_Telemetry.size()));
        client.enqueue(TStringView(NWB_TEXT("queued message")));
        server.releaseFirstRequest();
        releaseGate.release();
        for(u32 attempt = 0u; attempt < 500u && server.requestCount() < 2u; ++attempt)
            SleepMS(10u);
        ASSERT_GE(server.requestCount(), 2u);
        EXPECT_TRUE(server.requestWasTelemetry(0u));
        EXPECT_FALSE(server.requestWasTelemetry(1u));
    }
}


TEST(LogClientShutdown, TelemetryProgressesWhileMessageBacklogRemains){
    ShutdownCaptureServer server(MHD_HTTP_OK, false, true);
    u16 port = 0u;
    ASSERT_TRUE(server.start(port));
    {
        NWB::Log::Client client;
        ScopeExit releaseGate([&server]()noexcept{ server.releaseFirstRequest(); });
        const AString<NWB::Log::LogArena> url = StringFormat(client.arena(), "http://127.0.0.1:{}", port);
        ASSERT_TRUE(client.init(AStringView(url.data(), url.size())));
        client.enqueue(TStringView(NWB_TEXT("first message")));
        for(u32 attempt = 0u; attempt < 500u && !server.processingFirstRequest(); ++attempt)
            SleepMS(1u);
        ASSERT_TRUE(server.processingFirstRequest());
        for(u32 index = 0u; index < 3u; ++index)
            client.enqueue(TStringView(NWB_TEXT("queued message")));
        constexpr Array<u8, 1u> s_Telemetry = { 1u };
        ASSERT_TRUE(client.enqueueTelemetry(s_Telemetry.data(), s_Telemetry.size()));
        server.releaseFirstRequest();
        releaseGate.release();
        for(u32 attempt = 0u; attempt < 500u && server.requestCount() < 2u; ++attempt)
            SleepMS(10u);
        ASSERT_GE(server.requestCount(), 2u);
        EXPECT_FALSE(server.requestWasTelemetry(0u));
        EXPECT_TRUE(server.requestWasTelemetry(1u));
    }
}


TEST(LogClientShutdown, RetriesRejectedPayloadBeforeSendingNextMessage){
    ShutdownCaptureServer server(MHD_HTTP_SERVICE_UNAVAILABLE);
    u16 port = 0u;
    ASSERT_TRUE(server.start(port));
    {
        NWB::Log::Client client;
        const AString<NWB::Log::LogArena> url = StringFormat(client.arena(), "http://127.0.0.1:{}", port);
        ASSERT_TRUE(client.init(AStringView(url.data(), url.size())));
        client.enqueue(TStringView(NWB_TEXT("first payload")));
        client.enqueue(TStringView(NWB_TEXT("second payload")));
        for(u32 attempt = 0u; attempt < 500u && server.requestCount() < 3u; ++attempt)
            SleepMS(10u);
        EXPECT_EQ(server.requestCount(), 3u);
    }
    EXPECT_TRUE(server.firstPayloadRetried());
}

TEST(LogClientShutdown, ShutdownInterruptsBackoffAfterRepeatedTemporaryFailures){
    ShutdownCaptureServer server(MHD_HTTP_SERVICE_UNAVAILABLE, true);
    u16 port = 0u;
    ASSERT_TRUE(server.start(port));
    Timer stopStarted;
    testing::internal::CaptureStderr();
    {
        NWB::Log::Client client;
        const AString<NWB::Log::LogArena> url = StringFormat(client.arena(), "http://127.0.0.1:{}", port);
        const b8 initialized = client.init(AStringView(url.data(), url.size()));
        EXPECT_TRUE(initialized);
        if(initialized){
            client.enqueue(TStringView(NWB_TEXT("retry payload")));
            for(u32 attempt = 0u; attempt < 1200u && server.requestCount() < 7u; ++attempt)
                SleepMS(10u);
            EXPECT_GE(server.requestCount(), 7u);
            SleepMS(50u);
        }
        stopStarted = TimerNow();
    }
    const auto stderrText = testing::internal::GetCapturedStderr();
    EXPECT_NE(stderrText.find("stopped with an unsent payload"), stderrText.npos);
    EXPECT_LT(TimerNow(), TimerAddMS(stopStarted, 3000));
}

TEST(LogClientShutdown, TransportFailureIsVisibleWithoutDiagnosticCallback){
    testing::internal::CaptureStderr();
    {
        NWB::Log::Client client;
        const b8 initialized = client.init("http://[invalid");
        EXPECT_TRUE(initialized);
        if(initialized){
            client.enqueue(TStringView(NWB_TEXT("unsent payload")));
            SleepMS(500u);
        }
    }
    const auto stderrText = testing::internal::GetCapturedStderr();
    EXPECT_NE(stderrText.find("[ERROR] Log client: sending payload failed"), stderrText.npos);
    EXPECT_NE(stderrText.find("stopped with an unsent payload"), stderrText.npos);
}

TEST(LogClientShutdown, PermanentBadRequestDoesNotBlockLaterMessages){
    ShutdownCaptureServer server(MHD_HTTP_BAD_REQUEST);
    u16 port = 0u;
    ASSERT_TRUE(server.start(port));
    {
        NWB::Log::Client client;
        const AString<NWB::Log::LogArena> url = StringFormat(client.arena(), "http://127.0.0.1:{}", port);
        ASSERT_TRUE(client.init(AStringView(url.data(), url.size())));
        client.enqueue(TStringView(NWB_TEXT("rejected payload")));
        client.enqueue(TStringView(NWB_TEXT("later payload")));
        for(u32 attempt = 0u; attempt < 500u && server.requestCount() < 2u; ++attempt)
            SleepMS(10u);
        EXPECT_EQ(server.requestCount(), 2u);
        EXPECT_EQ(client.rejectedPayloadCount(), 1u);
    }
    EXPECT_EQ(server.requestCount(), 2u);
    EXPECT_FALSE(server.firstPayloadRetried());
}

TEST(LogClientShutdown, OversizedMessageDoesNotBlockLaterMessages){
    ShutdownCaptureServer server;
    u16 port = 0u;
    ASSERT_TRUE(server.start(port));
    {
        NWB::Log::Client client;
        const AString<NWB::Log::LogArena> url = StringFormat(client.arena(), "http://127.0.0.1:{}", port);
        ASSERT_TRUE(client.init(AStringView(url.data(), url.size())));
        NWB::Log::LogString message(client.arena());
        message.resize(NWB::Log::s_MaxLogMessageUploadBytes / sizeof(tchar) + 1u, static_cast<tchar>('x'));
        client.enqueue(TStringView(message.data(), message.size()));
        client.enqueue(TStringView(NWB_TEXT("later payload")));
        for(u32 attempt = 0u; attempt < 500u && server.requestCount() < 2u; ++attempt)
            SleepMS(10u);
        EXPECT_EQ(server.requestCount(), 2u);
        EXPECT_EQ(client.rejectedPayloadCount(), 1u);
    }
    EXPECT_GT(server.firstPayloadSize(), 0u);
    EXPECT_LT(server.firstPayloadSize(), 1024u);
    EXPECT_FALSE(server.firstPayloadRetried());
}

TEST(LogClientShutdown, OversizedTelemetryIsRejectedBeforeReadingItsBuffer){
    NWB::Log::Client client;
    const u8 byte = 0u;
    EXPECT_FALSE(client.enqueueTelemetry(&byte, NWB::Log::s_MaxTelemetryUploadBytes + 1u));
    EXPECT_EQ(client.rejectedPayloadCount(), 1u);
}

TEST(LogClientShutdown, DrainsEveryQueuedMessageAfterStopBegins){
    ShutdownCaptureServer server;
    u16 port = 0u;
    ASSERT_TRUE(server.start(port));

    {
        NWB::Log::Client client;
        const AString<NWB::Log::LogArena> url = StringFormat(client.arena(), "http://127.0.0.1:{}", port);
        ASSERT_TRUE(client.init(AStringView(url.data(), url.size())));

        for(u32 messageIndex = 0u; messageIndex < s_ShutdownMessageCount; ++messageIndex){
            client.enqueue(StringFormat(client.arena(), NWB_TEXT("Log client shutdown payload {}"), messageIndex));
        }
    }

    EXPECT_EQ(server.requestCount(), s_ShutdownMessageCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/filesystem_helpers.h>
#include <tests/common/capturing_logger.h>
#include <tests/common/test_context.h>
#include <tests/integration/logger_server/name_cases.adhd.h>

#include <logger/common.h>

#include <core/crash/module.h>
#include <core/crash/package_internal.h>
#include <core/crash/package_archive.h>
#include <core/crash/package_names.h>
#include <core/common/log.h>

#include <global/atomic.h>
#include <global/sync.h>
#include <global/thread.h>
#include <global/assert.h>
#include <global/environment.h>
#include <global/filesystem/directory_iterator.h>
#include <global/filesystem/operations.h>
#include <global/process.h>
#include <global/process_execution.h>

#include <gtest/gtest.h>

#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_logserver_crash_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_TestSourceFilename = "tests/integration/logger_server/logserver_crash_tests.cpp";
constexpr AStringView s_ApplicationName = "logserver_crash_tests";
constexpr AStringView s_ApplicationVersion = "1";
constexpr AStringView s_WindowsExceptionCategory = "windows_exception";

static constexpr AStringView s_PlatformLinux = "platform=linux";
static constexpr AStringView s_Event = "[event]";
#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
static constexpr AStringView s_Callstack = "callstack:";
static constexpr AStringView s_StatusCallstackCaptured = "status=callstack_captured";
#endif
static constexpr AStringView s_Linux = "linux";
static constexpr AStringView s_Crash = "crash";
static constexpr AStringView s_Signal = "signal";
static constexpr AStringView s_ManualDump = "manual_dump";
static constexpr AStringView s_Windows = "windows";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestArena = NWB::Tests::TestArena<>;
using CapturingLogger = NWB::Tests::CapturingLogger;
using NWB::Tests::WaitForDirectory;
using namespace NWB::Tests::CrashTestPaths;
using namespace NWB::Tests::LoggerServerCrash;
using namespace NWB::Core::Crash::PackageNames;
inline constexpr AStringView s_InvalidArchiveHeader("NWBCRASHPKG 0\n");
inline constexpr Name s_ArchiveScratch = NWB::Tests::LoggerCrashCases::s_LoggerCrashTestNameTestsIntegrationLoggerServerArchiveScratch;
inline constexpr Name s_AssertChildInstallArena = NWB::Tests::LoggerCrashCases::s_LoggerCrashTestNameTestsIntegrationLoggerServerAssertChildInstall;
inline constexpr Name s_RecoverableErrorInstallArena = NWB::Tests::LoggerCrashCases::s_LoggerCrashTestNameTestsIntegrationLoggerServerRecoverableErrorInstall;


class LoggerServerCrash : public ::testing::Test{
public:
    LoggerServerCrash()
        : m_loggerRegistration(m_logger)
    {}


private:
    CapturingLogger m_logger;
    NWB::Core::Common::LoggerRegistrationGuard m_loggerRegistration;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)


static void AppendDecimalText(const u64 value, CrashTestText& outText){
    Array<char, 32u> buffer = {};
    outText += FormatDecimal(static_cast<usize>(value), buffer);
}


[[nodiscard]] static Expected<b8, ErrorCode> LinuxExternalSymbolizerAvailable(NWB::Core::Alloc::GlobalArena& arena){
    const auto pathText = ReadEnvironmentVariable(arena, "PATH");
    if(!pathText)
        return MakeUnexpected(pathText.error());
    if(!*pathText || (*pathText)->empty())
        return false;

    const AStringView searchPath((*pathText)->data(), (*pathText)->size());
    const auto llvmAvailable = ExecutableAvailableInPath(arena, searchPath, "llvm-symbolizer");
    if(!llvmAvailable)
        return MakeUnexpected(llvmAvailable.error());
    if(*llvmAvailable)
        return true;
    return ExecutableAvailableInPath(arena, searchPath, "addr2line");
}

static void LinuxSilenceExpectedCrashChildConsole(){
    const int nullFd = open("/dev/null", O_WRONLY);
    if(nullFd < 0)
        return;

    if(dup2(nullFd, STDOUT_FILENO) < 0){
        close(nullFd);
        return;
    }
    if(dup2(nullFd, STDERR_FILENO) < 0){
        close(nullFd);
        return;
    }
    if(nullFd > STDERR_FILENO)
        close(nullFd);
}

[[nodiscard]] static b8 PendingDirectoryContainsManifestTexts(NWB::Core::Alloc::GlobalArena& arena, const CrashTestPath& pendingDirectory, const AStringView firstNeedle, const AStringView secondNeedle){
    const auto directory = DirectoryIterator<NWB::Core::Alloc::GlobalArena>::Create(pendingDirectory);
    if(!directory)
        return false;

    for(const auto& entry : *directory){
        const auto isDirectory = IsDirectory(entry.path());
        if(!isDirectory || !*isDirectory)
            continue;

        CrashTestText manifest(arena);
        if(!ReadTextFile(entry.path() / s_ManifestFileName, manifest))
            continue;
        if(Contains(manifest, firstNeedle) && Contains(manifest, secondNeedle))
            return true;
    }

    return false;
}

[[nodiscard]] static AStringView LinuxObservableAssertCategory()noexcept{
#if NWB_OCCUR_ASSERT
    return DiagnosticEventCategory::s_Assert;
#else
    return DiagnosticEventCategory::s_FatalAssert;
#endif
}

NWB_NOINLINE static void LinuxForceAssertFalseForCrashObservation(){
#if NWB_OCCUR_ASSERT
    NWB_ASSERT(false);
#else
    NWB_FATAL_ASSERT(false);
#endif
    _exit(120);
}
#endif

// Guard this helper on Android where its caller test is disabled.
#if defined(NWB_PLATFORM_WINDOWS) || (defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID))
NWB_NOINLINE static void CaptureRecoverableErrorForCrashObservation(const AStringView message)noexcept{
    CaptureDiagnosticEvent(DiagnosticEventRecord{
        .event = DiagnosticEventName::s_Error,
        .category = NWB::Core::Common::LoggerDetail::s_DiagnosticEventCategoryError,
        .message = message,
        .file = s_TestSourceFilename,
        .line = __LINE__,
    });
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static NWB::Log::CrashIngestResult ProcessCrashArchive(NWB::Core::Alloc::GlobalArena& arena, const AStringView testGroup, const AStringView stem, const AStringView archive, const NWB::Log::CrashIngestConfig& config){
    EXPECT_TRUE(WriteArchive(arena, testGroup, stem, archive));
    return NWB::Log::ProcessCrashUpload(arena, ArchivePath(arena, testGroup, stem), config);
}

static NWB::Log::CrashIngestResult ProcessCrashArchive(NWB::Core::Alloc::GlobalArena& arena, const AStringView testGroup, const AStringView stem, const AStringView archive){
    const NWB::Log::CrashIngestConfig config = MakeIngestConfig(arena, testGroup);
    return ProcessCrashArchive(arena, testGroup, stem, archive, config);
}

static NWB::Log::CrashIngestResult ProcessCrashArchiveBytes(NWB::Core::Alloc::GlobalArena& arena, const AStringView testGroup, const AStringView stem, const Span<const u8> archive, const NWB::Log::CrashIngestConfig& config){
    EXPECT_TRUE(WriteArchiveBytes(arena, testGroup, stem, archive));
    return NWB::Log::ProcessCrashUpload(arena, ArchivePath(arena, testGroup, stem), config);
}

static NWB::Log::CrashIngestResult ProcessCrashArchiveBytes(NWB::Core::Alloc::GlobalArena& arena, const AStringView testGroup, const AStringView stem, const Span<const u8> archive){
    const NWB::Log::CrashIngestConfig config = MakeIngestConfig(arena, testGroup);
    return ProcessCrashArchiveBytes(arena, testGroup, stem, archive, config);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(LoggerServerCrash, LinuxAssertCrashProducesObservableLoggerReport){
#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_linux_assert_observe_test");
    constexpr AStringView s_Stem("linux_assert_observe_001");
    RemoveTestArtifacts(arena, s_Group);
    const AStringView expectedAssertCategory(LinuxObservableAssertCategory());

    const CrashTestPath spoolDirectory = SpoolDirectory(arena, s_Group);
    const pid_t childPid = fork();
    ASSERT_GE(childPid, 0);
    if(childPid == 0){
        NWB::Core::Alloc::PersistentArena installArena(s_AssertChildInstallArena, NWB::Core::Alloc::PersistentArena::StructureAlignedSize(64u * 1024u));
        NWB::Core::Crash::CrashConfigT<NWB::Core::Alloc::PersistentArena> config(installArena);
        config.applicationName = AStringView(s_ApplicationName);
        config.version = AStringView(s_ApplicationVersion);
        config.buildId = AStringView("linux-assert-observe-test");
        config.spoolDirectory = spoolDirectory;

        if(!NWB::Core::Crash::InstallCrashHandler(config))
            _exit(121);

        LinuxSilenceExpectedCrashChildConsole();
        LinuxForceAssertFalseForCrashObservation();
        _exit(122);
    }

    int status = 0;
    while(waitpid(childPid, &status, 0) < 0){
        if(errno == EINTR)
            continue;
        break;
    }

    EXPECT_TRUE(WIFSIGNALED(status));
    EXPECT_EQ(WTERMSIG(status), SIGABRT);

    const CrashTestPath pendingDirectory = spoolDirectory / s_PendingDirectoryName;
    {
        constexpr u32 s_PendingDirectoryReadyTimeoutMS = 3000u;
        const auto directoryReady = WaitForDirectory(pendingDirectory, s_PendingDirectoryReadyTimeoutMS);
        ASSERT_TRUE(directoryReady);
        EXPECT_TRUE(*directoryReady);
    }

    CrashTestText expectedAbortCode(arena);
    expectedAbortCode += "\"reason_code\": ";
    AppendDecimalText(static_cast<u64>(SIGABRT), expectedAbortCode);
    EXPECT_FALSE(PendingDirectoryContainsManifestTexts(arena, pendingDirectory, AStringView("\"reason_kind\": \"signal\""), AStringView(expectedAbortCode.data(), expectedAbortCode.size())));

    const auto assertPackageDirectory = WaitForTriggerPackage(arena, pendingDirectory, expectedAssertCategory, "false", AStringView(), s_TestSourceFilename);
    EXPECT_TRUE(assertPackageDirectory);

    ASSERT_TRUE(assertPackageDirectory);
    NWB::Core::Alloc::ScratchArena archiveScratch(s_ArchiveScratch);
    const auto archive = NWB::Core::Crash::CrashDetail::CrashPackageArchive::Build(arena, archiveScratch, *assertPackageDirectory);
    ASSERT_TRUE(archive);
    const NWB::Log::CrashIngestResult result = ProcessCrashArchiveBytes(arena, s_Group, s_Stem, *archive);
    EXPECT_TRUE(result.accepted);
    EXPECT_EQ(result.type, NWB::Core::Common::LogType::Assert);

    auto reportResult = ReadServerSymbolication(arena, s_Group, s_Stem);
    ASSERT_TRUE(reportResult);
    CrashTestText report = Move(*reportResult);
    EXPECT_TRUE(Contains(report, s_PlatformLinux));
    EXPECT_TRUE(Contains(report, "reason=manual_dump"));
    EXPECT_TRUE(Contains(report, s_Event));
    EXPECT_TRUE(Contains(report, "event=assert"));
    EXPECT_TRUE(Contains(report, s_StatusCallstackCaptured));
    EXPECT_TRUE(Contains(report, s_Callstack));
    EXPECT_EQ(report.find("false\nat "), 0u);
    EXPECT_TRUE(Contains(report, s_TestSourceFilename));
    const auto externalSymbolizer = LinuxExternalSymbolizerAvailable(arena);
    ASSERT_TRUE(externalSymbolizer);
    if(*externalSymbolizer){
        EXPECT_TRUE(Contains(report, "LinuxForceAssertFalseForCrashObservation"));
    }

    PreserveObservedReport(arena, report, "linux_assert");

    RemoveTestArtifacts(arena, s_Group);
#endif
}

TEST_F(LoggerServerCrash, RecoverableErrorDiagnosticProducesObservableLoggerReport){
#if defined(NWB_PLATFORM_WINDOWS) || (defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID))
    TestArena testArena;
    auto& arena = testArena.arena;
    NWB::Core::Alloc::PersistentArena installArena(s_RecoverableErrorInstallArena, NWB::Core::Alloc::PersistentArena::StructureAlignedSize(64u * 1024u));
    constexpr AStringView s_Group("logger_server_recoverable_error_observe_test");
    constexpr AStringView s_Stem("recoverable_error_observe_001");
    constexpr AStringView s_ErrorMessage("recoverable logger error observation");
    RemoveTestArtifacts(arena, s_Group);

    const CrashTestPath spoolDirectory = SpoolDirectory(arena, s_Group);
    NWB::Core::Crash::CrashConfigT<NWB::Core::Alloc::PersistentArena> crashConfig(installArena);
    crashConfig.applicationName = AStringView(s_ApplicationName);
    crashConfig.version = AStringView(s_ApplicationVersion);
    crashConfig.buildId = AStringView("recoverable-error-observe-test");
    crashConfig.spoolDirectory = spoolDirectory;

    const b8 installed = NWB::Core::Crash::InstallCrashHandler(crashConfig);
    EXPECT_TRUE(installed);

    b8 continuedAfterError = false;
    if(installed){
        CaptureRecoverableErrorForCrashObservation(s_ErrorMessage);
        continuedAfterError = true;
    }

    const CrashTestPath pendingDirectory = spoolDirectory / s_PendingDirectoryName;
    const auto errorPackageDirectory = WaitForTriggerPackage(arena, pendingDirectory, NWB::Core::Common::LoggerDetail::s_DiagnosticEventCategoryError, AStringView(), s_ErrorMessage, s_TestSourceFilename);
    EXPECT_TRUE(errorPackageDirectory);
    EXPECT_TRUE(continuedAfterError);
    NWB::Core::Crash::UninstallCrashHandler();

    ASSERT_TRUE(errorPackageDirectory);
    NWB::Core::Alloc::ScratchArena archiveScratch(s_ArchiveScratch);
    const auto archive = NWB::Core::Crash::CrashDetail::CrashPackageArchive::Build(arena, archiveScratch, *errorPackageDirectory);
    ASSERT_TRUE(archive);
    const NWB::Log::CrashIngestResult result = ProcessCrashArchiveBytes(arena, s_Group, s_Stem, *archive);
    EXPECT_TRUE(result.accepted);
    EXPECT_EQ(result.type, NWB::Core::Common::LogType::Error);

    auto reportResult = ReadServerSymbolication(arena, s_Group, s_Stem);
    ASSERT_TRUE(reportResult);
    CrashTestText report = Move(*reportResult);
    EXPECT_TRUE(Contains(report, s_Event));
    EXPECT_TRUE(Contains(report, "event=error"));
    EXPECT_TRUE(Contains(report, s_ErrorMessage));
    EXPECT_TRUE(Contains(report, s_TestSourceFilename));
#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
    EXPECT_TRUE(Contains(report, s_PlatformLinux));
    EXPECT_TRUE(Contains(report, s_StatusCallstackCaptured));
    EXPECT_TRUE(Contains(report, s_Callstack));
    const auto externalSymbolizer = LinuxExternalSymbolizerAvailable(arena);
    ASSERT_TRUE(externalSymbolizer);
    if(*externalSymbolizer)
        EXPECT_TRUE(Contains(report, "CaptureRecoverableErrorForCrashObservation"));
#elif defined(NWB_PLATFORM_WINDOWS)
    EXPECT_TRUE(Contains(report, "platform=windows"));
    EXPECT_TRUE(Contains(report, "resolver=windows_pdb_minidump"));
#endif

    PreserveObservedReport(arena, report, "recoverable_error");

    RemoveTestArtifacts(arena, s_Group);
#endif
}


TEST_F(LoggerServerCrash, LinuxCrashPackageReportsMissingProcMaps){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_linux_missing_maps_test");
    constexpr AStringView s_Stem("linux_missing_maps_001");
    RemoveTestArtifacts(arena, s_Group);

    CrashTestText archive(arena);
    BeginArchiveWithManifest("linux-missing-maps-test", s_Linux, s_Crash, s_Signal, 11u, ManifestEventField::Include, ManifestTriggerFields{}, arena, archive);
    AppendArchiveFile(s_CpuContextFileName, "instruction_pointer=4198964\n", archive);

    const NWB::Log::CrashIngestResult result = ProcessCrashArchive(arena, s_Group, s_Stem, archive);

    EXPECT_TRUE(result.accepted);

    auto reportResult = ReadServerSymbolication(arena, s_Group, s_Stem);
    ASSERT_TRUE(reportResult);
    CrashTestText report = Move(*reportResult);
    EXPECT_TRUE(Contains(report, s_PlatformLinux));
    EXPECT_TRUE(Contains(report, "proc_maps=missing"));
    EXPECT_TRUE(Contains(report, "proc maps missing for module lookup"));

    RemoveTestArtifacts(arena, s_Group);
}

TEST_F(LoggerServerCrash, LinuxCrashPackageReportsUnmappedInstructionPointer){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_linux_unmapped_ip_test");
    constexpr AStringView s_Stem("linux_unmapped_ip_001");
    RemoveTestArtifacts(arena, s_Group);

    CrashTestText archive(arena);
    BeginArchiveWithManifest("linux-unmapped-ip-test", s_Linux, s_Crash, s_Signal, 11u, ManifestEventField::Include, ManifestTriggerFields{}, arena, archive);
    AppendArchiveFile(s_CpuContextFileName, "instruction_pointer=7340032\n", archive);
    AppendArchiveFile(s_ProcMapsFileName, "00400000-00452000 r-xp 00000000 08:01 123 /tmp/nwb_loader\n", archive);

    const NWB::Log::CrashIngestResult result = ProcessCrashArchive(arena, s_Group, s_Stem, archive);

    EXPECT_TRUE(result.accepted);

    auto reportResult = ReadServerSymbolication(arena, s_Group, s_Stem);
    ASSERT_TRUE(reportResult);
    CrashTestText report = Move(*reportResult);
    EXPECT_TRUE(Contains(report, s_PlatformLinux));
    EXPECT_TRUE(Contains(report, "proc_maps=present"));
    EXPECT_TRUE(Contains(report, "instruction pointer was not found in proc maps"));

    RemoveTestArtifacts(arena, s_Group);
}

#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
TEST_F(LoggerServerCrash, LinuxCrashPackageDoesNotWrapSymbolOffset){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_linux_offset_overflow_test");
    constexpr AStringView s_Stem("linux_offset_overflow_001");
    RemoveTestArtifacts(arena, s_Group);

    CrashTestText archive(arena);
    BeginArchiveWithManifest("linux-offset-overflow-test", s_Linux, s_Crash, s_Signal, 11u, ManifestEventField::Include, ManifestTriggerFields{}, arena, archive);
    AppendArchiveFile(s_CpuContextFileName, "instruction_pointer=4097\n", archive);
    AppendArchiveFile(s_ProcMapsFileName, "1000-2000 r-xp ffffffffffffffff 00:00 0 /tmp/overflow\n", archive);
    const NWB::Log::CrashIngestResult result = ProcessCrashArchive(arena, s_Group, s_Stem, archive);
    ASSERT_TRUE(result.accepted);
    auto report = ReadServerSymbolication(arena, s_Group, s_Stem);
    ASSERT_TRUE(report);
    EXPECT_TRUE(Contains(*report, "instruction_pointer_module=/tmp/overflow"));
    EXPECT_TRUE(Contains(*report, "symbolication_relative_ip=unavailable"));
    RemoveTestArtifacts(arena, s_Group);
}
#endif

TEST_F(LoggerServerCrash, AndroidCrashPackageReportsTombstoneWithoutFrames){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_android_no_frames_test");
    constexpr AStringView s_Stem("android_no_frames_001");
    RemoveTestArtifacts(arena, s_Group);

    CrashTestText archive(arena);
    BeginArchiveWithManifest("android-no-frames-test", "android", s_Crash, s_ManualDump, 0u, ManifestEventField::Include, ManifestTriggerFields{}, arena, archive);
    AppendArchiveFile(s_AndroidTombstoneFileName, "pid: 7, tid: 7, name: nwb\nbacktrace:\n", archive);

    const NWB::Log::CrashIngestResult result = ProcessCrashArchive(arena, s_Group, s_Stem, archive);

    EXPECT_TRUE(result.accepted);

    auto reportResult = ReadServerSymbolication(arena, s_Group, s_Stem);
    ASSERT_TRUE(reportResult);
    CrashTestText report = Move(*reportResult);
    EXPECT_TRUE(Contains(report, "platform=android"));
    EXPECT_TRUE(Contains(report, "status=not_decoded"));
    EXPECT_TRUE(Contains(report, "android_tombstone=present"));
    EXPECT_TRUE(Contains(report, "no native frame lines were recognized"));

    RemoveTestArtifacts(arena, s_Group);
}

TEST_F(LoggerServerCrash, WindowsCrashPackageReportsMissingMinidump){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_windows_missing_dump_test");
    constexpr AStringView s_Stem("windows_missing_dump_001");
    RemoveTestArtifacts(arena, s_Group);

    CrashTestText archive(arena);
    BeginArchiveWithManifest("windows-missing-dump-test", s_Windows, s_Crash, s_WindowsExceptionCategory, 0xC0000005u, ManifestEventField::Include, ManifestTriggerFields{}, arena, archive);

    const NWB::Log::CrashIngestResult result = ProcessCrashArchive(arena, s_Group, s_Stem, archive);

    EXPECT_TRUE(result.accepted);

    auto reportResult = ReadServerSymbolication(arena, s_Group, s_Stem);
    ASSERT_TRUE(reportResult);
    CrashTestText report = Move(*reportResult);
    EXPECT_TRUE(Contains(report, "platform=windows"));
    EXPECT_TRUE(Contains(report, s_Event));
    EXPECT_TRUE(Contains(report, "event=crash"));
    EXPECT_TRUE(Contains(report, "exception=access_violation 0x00000000c0000005"));
    EXPECT_TRUE(Contains(report, "resolver=windows_pdb_minidump"));
#if defined(NWB_PLATFORM_WINDOWS)
    CrashTestText missingDumpMessage(arena);
    missingDumpMessage += s_ProcessDumpFileName;
    missingDumpMessage += " is missing or unreadable";
    EXPECT_TRUE(Contains(report, AStringView(missingDumpMessage.data(), missingDumpMessage.size())));
#else
    EXPECT_TRUE(Contains(report, "only available on Windows logserver builds"));
#endif

    RemoveTestArtifacts(arena, s_Group);
}

TEST_F(LoggerServerCrash, WindowsCrashPackageDecodesGpuDetectiveCaptureInProcess){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_gpu_detective_test");
    constexpr AStringView s_Stem("gpu_detective_001");
    RemoveTestArtifacts(arena, s_Group);

    CrashTestText archive(arena);
    BeginArchiveWithManifest("gpu-detective-test", s_Windows, s_Crash, s_WindowsExceptionCategory, 0xC0000005u, ManifestEventField::Include, ManifestTriggerFields{}, arena, archive);
    // Malformed RGD input must report decode failure without aborting crash ingest.
    AppendArchiveFile(s_GpuDetectiveCaptureFileName, "not a valid radeon gpu detective capture\n", archive);

    const NWB::Log::CrashIngestResult result = ProcessCrashArchive(arena, s_Group, s_Stem, archive);
    EXPECT_TRUE(result.accepted);

    auto reportResult = ReadServerSymbolication(arena, s_Group, s_Stem);
    ASSERT_TRUE(reportResult);
    CrashTestText report = Move(*reportResult);
    // The decoder always emits the section header before reporting invalid input.
    EXPECT_TRUE(Contains(report, "[gpu_detective]"));
    EXPECT_TRUE(Contains(report, "status=decode_failed"));

    RemoveTestArtifacts(arena, s_Group);
}


TEST_F(LoggerServerCrash, InvalidCrashPackageIsRejected){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_invalid_crash_test");
    constexpr AStringView s_Stem("invalid_001");
    RemoveTestArtifacts(arena, s_Group);

    CrashTestText archive(arena);
    archive += s_InvalidArchiveHeader;
    const NWB::Log::CrashIngestResult result = ProcessCrashArchive(arena, s_Group, s_Stem, archive);

    EXPECT_FALSE(result.accepted);
    EXPECT_EQ(result.type, NWB::Core::Common::LogType::Error);
    EXPECT_TRUE(Contains(result.message, NWB_TEXT("Crash upload rejected")));
    EXPECT_TRUE(Contains(result.message, NWB_TEXT("invalid crash archive header")));

    {
        const auto pathQuery = IsRegularFile(InvalidArchivePath(arena, s_Group, s_Stem));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }

    RemoveTestArtifacts(arena, s_Group);
}

TEST_F(LoggerServerCrash, UnsafeArchiveEntryCannotEscapePackageDirectory){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_unsafe_archive_20261107");
    constexpr AStringView s_Stem("unsafe_001");
    const auto testDirectory = NWB::Tests::CrashTestPaths::TestCaseDirectory(arena, s_Group);
    const auto existed = FileExistsNoFollow(testDirectory);
    ASSERT_TRUE(existed);
    ASSERT_FALSE(*existed);

    CrashTestText archive(arena);
    archive += s_ArchiveHeaderText;
    AppendArchiveFile("../escape", "payload", archive);
    const auto result = ProcessCrashArchive(arena, s_Group, s_Stem, archive);
    EXPECT_FALSE(result.accepted);
    EXPECT_TRUE(Contains(result.message, NWB_TEXT("malformed crash archive file header")));
    const auto escaped = FileExistsNoFollow(StorageDirectory(arena, s_Group) / "packages" / "escape");
    ASSERT_TRUE(escaped);
    EXPECT_FALSE(*escaped);
    RemoveTestArtifacts(arena, s_Group);
}

TEST_F(LoggerServerCrash, CrashManifestWithoutEventIsRejected){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_missing_event_manifest_crash_test");
    constexpr AStringView s_Stem("missing_event_manifest_001");
    RemoveTestArtifacts(arena, s_Group);

    CrashTestText archive(arena);
    BeginArchiveWithManifest("missing-event-manifest-test", s_Linux, s_Crash, s_Signal, 11u, ManifestEventField::Omit, ManifestTriggerFields{}, arena, archive);
    const NWB::Log::CrashIngestResult result = ProcessCrashArchive(arena, s_Group, s_Stem, archive);

    EXPECT_FALSE(result.accepted);
    EXPECT_EQ(result.type, NWB::Core::Common::LogType::Error);
    EXPECT_TRUE(Contains(result.message, NWB_TEXT("manifest.json is missing required fields")));
    {
        const auto pathQuery = IsRegularFile(InvalidArchivePath(arena, s_Group, s_Stem));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }

    RemoveTestArtifacts(arena, s_Group);
}

TEST_F(LoggerServerCrash, InvalidManifestBooleanIsRejectedWhileFalseAndZeroRemainValid){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_boolean_manifest_test");
    constexpr AStringView s_ValidStem("valid_false_001");
    constexpr AStringView s_InvalidStem("invalid_boolean_001");
    RemoveTestArtifacts(arena, s_Group);

    CrashTestText validArchive(arena);
    BeginArchiveWithManifest(s_ValidStem, s_Linux, s_Crash, s_Signal, 0u, ManifestEventField::Include, ManifestTriggerFields{}, arena, validArchive);
    const auto validResult = ProcessCrashArchive(arena, s_Group, s_ValidStem, validArchive);
    ASSERT_TRUE(validResult.accepted);

    CrashTestText manifest = BuildManifest(s_InvalidStem, s_Linux, s_Crash, s_Signal, 0u, ManifestEventField::Include, ManifestTriggerFields{}, arena);
    constexpr AStringView s_BooleanPrefix("\"has_exception_context\": ");
    const usize fieldOffset = AStringView(manifest.data(), manifest.size()).find(s_BooleanPrefix);
    ASSERT_NE(fieldOffset, AStringView::npos);
    manifest.replace(fieldOffset + s_BooleanPrefix.size(), 5u, "null");

    CrashTestText invalidArchive(arena);
    invalidArchive += s_ArchiveHeaderText;
    AppendArchiveFile(s_ManifestFileName, AStringView(manifest.data(), manifest.size()), invalidArchive);
    const auto invalidResult = ProcessCrashArchive(arena, s_Group, s_InvalidStem, invalidArchive);
    EXPECT_FALSE(invalidResult.accepted);
    EXPECT_TRUE(Contains(invalidResult.message, NWB_TEXT("manifest.json is missing required fields")));
    {
        const auto pathQuery = IsRegularFile(InvalidArchivePath(arena, s_Group, s_InvalidStem));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }
    {
        const auto pathQuery = IsDirectory(ExtractedPackageDirectory(arena, s_Group, s_ValidStem));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }

    RemoveTestArtifacts(arena, s_Group);
}

TEST_F(LoggerServerCrash, CrashRetentionPrunesOldestAcceptedUploads){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_retention_accepted_test");
    constexpr AStringView s_Stem0("retention_001");
    constexpr AStringView s_Stem1("retention_002");
    constexpr AStringView s_Stem2("retention_003");
    RemoveTestArtifacts(arena, s_Group);

    NWB::Log::CrashIngestConfig config = MakeIngestConfig(arena, s_Group);
    config.retention.maxExtractedPackages = 2u;
    config.retention.maxRawArchives = 2u;
    config.retention.maxInvalidArchives = 0u;

    {
        CrashTestText archive(arena);
        BuildLinuxCrashArchive(s_Stem0, arena, archive);
        const NWB::Log::CrashIngestResult result = ProcessCrashArchive(arena, s_Group, s_Stem0, archive, config);
        EXPECT_TRUE(result.accepted);
    }
    {
        CrashTestText archive(arena);
        BuildLinuxCrashArchive(s_Stem1, arena, archive);
        const NWB::Log::CrashIngestResult result = ProcessCrashArchive(arena, s_Group, s_Stem1, archive, config);
        EXPECT_TRUE(result.accepted);
    }
    {
        CrashTestText archive(arena);
        BuildLinuxCrashArchive(s_Stem2, arena, archive);
        const NWB::Log::CrashIngestResult result = ProcessCrashArchive(arena, s_Group, s_Stem2, archive, config);
        EXPECT_TRUE(result.accepted);
    }

    {
        const auto pathQuery = FileExists(ExtractedPackageDirectory(arena, s_Group, s_Stem0));
        ASSERT_TRUE(pathQuery);
        EXPECT_FALSE(*pathQuery);
    }
    {
        const auto pathQuery = FileExists(RawArchivePath(arena, s_Group, s_Stem0));
        ASSERT_TRUE(pathQuery);
        EXPECT_FALSE(*pathQuery);
    }
    {
        const auto pathQuery = IsDirectory(ExtractedPackageDirectory(arena, s_Group, s_Stem1));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }
    {
        const auto pathQuery = IsRegularFile(RawArchivePath(arena, s_Group, s_Stem1));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }
    {
        const auto pathQuery = IsDirectory(ExtractedPackageDirectory(arena, s_Group, s_Stem2));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }
    {
        const auto pathQuery = IsRegularFile(RawArchivePath(arena, s_Group, s_Stem2));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }

    RemoveTestArtifacts(arena, s_Group);
}

TEST_F(LoggerServerCrash, AcceptedCrashWarnsWhenRawArchiveCannotBeRetained){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_raw_archive_failed_test");
    constexpr AStringView s_Stem("raw_blocked_001");
    RemoveTestArtifacts(arena, s_Group);

    CrashTestText archive(arena);
    BuildLinuxCrashArchive(s_Stem, arena, archive);

    EXPECT_TRUE(EnsureDirectories(StorageDirectory(arena, s_Group)));
    EXPECT_TRUE(WriteTextFile(StorageDirectory(arena, s_Group) / NWB::Log::s_CrashRawDirectoryName, AStringView("blocked")));

    NWB::Log::CrashIngestConfig config = MakeIngestConfig(arena, s_Group);
    const NWB::Log::CrashIngestResult result = ProcessCrashArchive(arena, s_Group, s_Stem, archive, config);

    EXPECT_TRUE(result.accepted);
    EXPECT_EQ(result.type, NWB::Core::Common::LogType::Warning);
    EXPECT_TRUE(Contains(result.message, NWB_TEXT("raw upload archive could not be retained")));
    {
        const auto pathQuery = IsDirectory(ExtractedPackageDirectory(arena, s_Group, s_Stem));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }
    {
        const auto pathQuery = FileExists(ArchivePath(arena, s_Group, s_Stem));
        ASSERT_TRUE(pathQuery);
        EXPECT_FALSE(*pathQuery);
    }

    RemoveTestArtifacts(arena, s_Group);
}

TEST_F(LoggerServerCrash, CrashRetentionPrunesOldestInvalidUploads){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_retention_invalid_test");
    constexpr AStringView s_Stem0("invalid_retention_001");
    constexpr AStringView s_Stem1("invalid_retention_002");
    RemoveTestArtifacts(arena, s_Group);

    NWB::Log::CrashIngestConfig config = MakeIngestConfig(arena, s_Group);
    config.retention.maxExtractedPackages = 0u;
    config.retention.maxRawArchives = 0u;
    config.retention.maxInvalidArchives = 1u;

    {
        CrashTestText archive(arena);
        archive += s_InvalidArchiveHeader;
        const NWB::Log::CrashIngestResult result = ProcessCrashArchive(arena, s_Group, s_Stem0, archive, config);
        EXPECT_FALSE(result.accepted);
    }
    {
        CrashTestText archive(arena);
        archive += s_InvalidArchiveHeader;
        const NWB::Log::CrashIngestResult result = ProcessCrashArchive(arena, s_Group, s_Stem1, archive, config);
        EXPECT_FALSE(result.accepted);
    }

    {
        const auto pathQuery = FileExists(InvalidArchivePath(arena, s_Group, s_Stem0));
        ASSERT_TRUE(pathQuery);
        EXPECT_FALSE(*pathQuery);
    }
    {
        const auto pathQuery = IsRegularFile(InvalidArchivePath(arena, s_Group, s_Stem1));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }

    RemoveTestArtifacts(arena, s_Group);
}




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/capturing_logger.h>
#include <logger/message_payload_codec.h>
#include <tests/common/test_context.h>

#include <core/common/log.h>

#include <logger/common.h>
#include <logger/server/crash_auth.adhd.h>
#include <tests/integration/logger_server/name_cases.adhd.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_logger_protocol_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestArena = NWB::Tests::TestArena<>;
static constexpr AStringView s_SecretToken = NWB::Tests::LoggerCrashCases::s_LoggerProtocolSecretToken;


class LoggerProtocolTests : public ::testing::Test{
public:
    LoggerProtocolTests()
        : m_loggerRegistration(m_logger)
    {}


private:
    NWB::Tests::CapturingLogger m_logger;
    NWB::Core::Common::LoggerRegistrationGuard m_loggerRegistration;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(LoggerProtocolTests, MessagePayloadReadsUnalignedBytesAndPreservesEmbeddedNulls){
    TestArena testArena;
    constexpr Array<tchar, 3u> s_Message = { static_cast<tchar>('A'), 0, static_cast<tchar>('Z') };
    const TStringView message(s_Message.data(), s_Message.size());
    const NWB::Log::MessageType source = MakeTuple(Timer{}, NWB::Core::Common::LogType::Warning, NWB::Log::LogString(message, testArena.arena));
    NWB::Log::LogBytes payload(testArena.arena);
    ASSERT_TRUE(NWB::Log::MessagePayloadCodec::Build(source, payload));

    constexpr usize s_PrefixBytes = sizeof(tchar);
    NWB::Log::LogBytes shifted(testArena.arena);
    shifted.resize(s_PrefixBytes + payload.size());
    NWB_MEMCPY(shifted.data() + s_PrefixBytes, payload.size(), payload.data(), payload.size());
    const auto parsed = NWB::Log::MessagePayloadCodec::Parse(testArena.arena, shifted.data() + s_PrefixBytes, payload.size());
    ASSERT_TRUE(parsed);
    EXPECT_EQ(TStringView(Get<2u>(*parsed)), message);

    shifted.back() = 1u;
    const auto malformed = NWB::Log::MessagePayloadCodec::Parse(testArena.arena, shifted.data() + s_PrefixBytes, payload.size());
    ASSERT_FALSE(malformed);
    EXPECT_FALSE(malformed.error().empty());
    EXPECT_EQ(TStringView(Get<2u>(*parsed)), message);
}

TEST_F(LoggerProtocolTests, CrashUploadAuthorizationMatchesBearerToken){
    EXPECT_TRUE(NWB::Log::CrashUploadAuthorizationMatches(AStringView(), AStringView()));
    EXPECT_TRUE(NWB::Log::CrashUploadAuthorizationMatches(AStringView(), "bad"));
    EXPECT_FALSE(NWB::Log::CrashUploadAuthorizationMatches(s_SecretToken, AStringView()));
    EXPECT_FALSE(NWB::Log::CrashUploadAuthorizationMatches(s_SecretToken, s_SecretToken));
    EXPECT_FALSE(NWB::Log::CrashUploadAuthorizationMatches(s_SecretToken, "Bearer wrong"));
    EXPECT_FALSE(NWB::Log::CrashUploadAuthorizationMatches(s_SecretToken, "Bearer secret-token "));

    constexpr AStringView s_HeaderBytes = NWB::Tests::LoggerCrashCases::s_LoggerProtocolHeaderBytes;
    const AStringView header(s_HeaderBytes.data(), LengthOf("Bearer secret-token") - 1u);
    EXPECT_TRUE(NWB::Log::CrashUploadAuthorizationMatches(s_SecretToken, header));
    EXPECT_FALSE(NWB::Log::CrashUploadAuthorizationMatches(s_SecretToken, header.substr(0u, header.size() - 1u)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/capturing_logger.h>
#include <tests/common/filesystem_helpers.h>
#include <tests/common/test_context.h>

#include <gtest/gtest.h>

#include <core/common/log.h>

#include <global/filesystem/executable_paths.h>
#include <global/atomic.h>
#include <global/environment.h>
#include <global/filesystem/exclusive_publication.h>
#include <global/filesystem/operations.h>
#include <global/process.h>
#include <global/sync.h>
#include <global/thread.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_logserver_telemetry_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestArena = NWB::Tests::TestArena<>;


class LoggerTelemetryPublication : public ::testing::Test{
public:
    LoggerTelemetryPublication()
        : m_loggerRegistration(m_logger)
    {}


private:
    NWB::Tests::CapturingLogger m_logger;
    NWB::Core::Common::LoggerRegistrationGuard m_loggerRegistration;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static ::Path<NWB::Core::Alloc::GlobalArena> MakeTelemetryPublicationTestDirectory(TestArena& testArena){
    ::Path<NWB::Core::Alloc::GlobalArena> root(testArena.arena);
    const auto executableDirectory = ExecutablePaths(testArena.arena).directory();
    if(!executableDirectory)
        return root;
    for(u32 attempt = 0u; attempt < 128u; ++attempt){
        const auto name = StringFormat(testArena.arena, "telemetry_publication_test_{}_{}", CurrentProcessId(), attempt);
        root = *executableDirectory / name;
        const auto created = CreateDirectories(root);
        if(!created)
            return ::Path<NWB::Core::Alloc::GlobalArena>(testArena.arena);
        if(*created)
            return root;
    }
    return ::Path<NWB::Core::Alloc::GlobalArena>(testArena.arena);
}

TEST_F(LoggerTelemetryPublication, TelemetryPublicationDoesNotReplaceExistingFinalFile){
    TestArena testArena;
    const auto root = MakeTelemetryPublicationTestDirectory(testArena);
    ASSERT_FALSE(root.empty());
    const auto destination = root / "payload.nwbs";
    constexpr Array<u8, 3u> s_Existing = { 1u, 2u, 3u };
    constexpr Array<u8, 3u> s_New = { 4u, 5u, 6u };
    ASSERT_TRUE(WriteBinaryFile(destination, s_Existing));

    const auto result = PublishNewFileDurable(destination, BinaryByteView{ s_New.data(), s_New.size() });
    EXPECT_FALSE(result.published);
    EXPECT_TRUE(result.stagingRetained);
    EXPECT_TRUE(result.error);
    Vector<u8, NWB::Core::Alloc::GlobalArena> bytes(testArena.arena);
    ASSERT_TRUE(ReadBinaryFile(destination, bytes));
    ASSERT_EQ(bytes.size(), s_Existing.size());
    EXPECT_EQ(NWB_MEMCMP(bytes.data(), s_Existing.data(), s_Existing.size()), 0);
    auto staging = destination;
    staging += ".partial";
    ASSERT_TRUE(ReadBinaryFile(staging, bytes));
    ASSERT_EQ(bytes.size(), s_New.size());
    EXPECT_EQ(NWB_MEMCMP(bytes.data(), s_New.data(), s_New.size()), 0);
}

TEST_F(LoggerTelemetryPublication, TelemetryPublicationDoesNotReplaceExistingStagingFile){
    TestArena testArena;
    const auto root = MakeTelemetryPublicationTestDirectory(testArena);
    ASSERT_FALSE(root.empty());
    const auto destination = root / "payload.nwbs";
    auto staging = destination;
    staging += ".partial";
    constexpr Array<u8, 2u> s_Existing = { 7u, 8u };
    constexpr Array<u8, 2u> s_New = { 9u, 10u };
    ASSERT_TRUE(WriteBinaryFile(staging, s_Existing));

    const auto result = PublishNewFileDurable(destination, BinaryByteView{ s_New.data(), s_New.size() });
    EXPECT_FALSE(result.published);
    EXPECT_FALSE(result.stagingRetained);
    EXPECT_TRUE(result.error);
    Vector<u8, NWB::Core::Alloc::GlobalArena> bytes(testArena.arena);
    ASSERT_TRUE(ReadBinaryFile(staging, bytes));
    ASSERT_EQ(bytes.size(), s_Existing.size());
    EXPECT_EQ(NWB_MEMCMP(bytes.data(), s_Existing.data(), s_Existing.size()), 0);
    const auto finalExists = FileExists(destination);
    ASSERT_TRUE(finalExists);
    EXPECT_FALSE(*finalExists);
}

TEST_F(LoggerTelemetryPublication, ConcurrentTelemetryPublishersCannotReplaceEachOther){
    TestArena rootArena;
    TestArena firstArena;
    TestArena secondArena;
    const auto root = MakeTelemetryPublicationTestDirectory(rootArena);
    ASSERT_FALSE(root.empty());
    const auto destination = root / "payload.nwbs";
    const ::Path<NWB::Core::Alloc::GlobalArena> firstPath(firstArena.arena, destination);
    const ::Path<NWB::Core::Alloc::GlobalArena> secondPath(secondArena.arena, destination);
    constexpr Array<u8, 2u> s_First = { 21u, 22u };
    constexpr Array<u8, 2u> s_Second = { 31u, 32u };
    FilePublicationResult firstResult;
    FilePublicationResult secondResult;
    Atomic<b8> start = false;
    Thread first([&](){
        while(!start.load(MemoryOrder::acquire))
            YieldThread();
        firstResult = PublishNewFileDurable(firstPath, BinaryByteView{ s_First.data(), s_First.size() });
    });
    Thread second([&](){
        while(!start.load(MemoryOrder::acquire))
            YieldThread();
        secondResult = PublishNewFileDurable(secondPath, BinaryByteView{ s_Second.data(), s_Second.size() });
    });
    start.store(true, MemoryOrder::release);
    first.join();
    second.join();
    EXPECT_NE(firstResult.published, secondResult.published);
    Vector<u8, NWB::Core::Alloc::GlobalArena> bytes(rootArena.arena);
    ASSERT_TRUE(ReadBinaryFile(destination, bytes));
    ASSERT_EQ(bytes.size(), s_First.size());
    const auto& winner = firstResult.published ? s_First : s_Second;
    EXPECT_EQ(NWB_MEMCMP(bytes.data(), winner.data(), winner.size()), 0);
}

TEST_F(LoggerTelemetryPublication, TelemetryPublicationMovesCompletedBytesToNewFinalFile){
    TestArena testArena;
    const auto root = MakeTelemetryPublicationTestDirectory(testArena);
    ASSERT_FALSE(root.empty());
    const auto destination = root / "payload.nwbs";
    constexpr Array<u8, 3u> s_Data = { 11u, 12u, 13u };
    const auto result = PublishNewFileDurable(destination, BinaryByteView{ s_Data.data(), s_Data.size() });
    EXPECT_TRUE(result.published);
    EXPECT_FALSE(result.stagingRetained);
    EXPECT_FALSE(result.error);
    Vector<u8, NWB::Core::Alloc::GlobalArena> bytes(testArena.arena);
    ASSERT_TRUE(ReadBinaryFile(destination, bytes));
    ASSERT_EQ(bytes.size(), s_Data.size());
    EXPECT_EQ(NWB_MEMCMP(bytes.data(), s_Data.data(), s_Data.size()), 0);
    auto staging = destination;
    staging += ".partial";
    const auto stagingExists = FileExists(staging);
    ASSERT_TRUE(stagingExists);
    EXPECT_FALSE(*stagingExists);
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <logger/message_payload_codec.h>

#include <tests/common/capturing_logger.h>
#include <tests/common/test_context.h>
#include <tests/integration/logger_server/name_cases.adhd.h>

#include <logger/common.h>
#include <logger/processed_message_file.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_logger_producer_contract_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Tests::CrashTestPaths;
using namespace NWB::Tests::LoggerServerCrash;
using TestArena = NWB::Tests::TestArena<>;

class LoggerProducerContracts : public testing::Test{
public:
    LoggerProducerContracts()
        : m_registration(m_logger, NWB::Core::Common::LoggerBreakPolicy::ReportOnly)
    {}


private:
    NWB::Tests::CapturingLogger m_logger;
    NWB::Core::Common::LoggerRegistrationGuard m_registration;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(LoggerProducerContracts, MessagePayloadRejectsInvalidTypeWithoutPublishingStaleBytes){
    TestArena testArena;
    NWB::Log::LogBytes payload(testArena.arena);
    payload.push_back(42u);
    const NWB::Log::MessageType source = MakeTuple(Timer{}, static_cast<NWB::Core::Common::LogType::Enum>(255u), NWB::Log::LogString(NWB_TEXT("invalid"), testArena.arena));
    const auto built = NWB::Log::MessagePayloadCodec::Build(source, payload);
    ASSERT_FALSE(built);
    EXPECT_EQ(built.error(), NWB::Log::MessagePayloadError::InvalidType);
    EXPECT_TRUE(payload.empty());
}

TEST_F(LoggerProducerContracts, SimultaneousProcessedFilesKeepIndependentRecords){
    TestArena testArena;
    NWB::Log::ProcessedMessageFile first(testArena.arena);
    NWB::Log::ProcessedMessageFile second(testArena.arena);
    ASSERT_TRUE(first.open(NWB_TEXT("processed_file_isolation")));
    ASSERT_TRUE(second.open(NWB_TEXT("processed_file_isolation")));
    EXPECT_NE(first.path(), second.path());
    ASSERT_TRUE(first.writeLine(NWB_TEXT("first record")));
    ASSERT_TRUE(second.writeLine(NWB_TEXT("second record")));
    first.close();
    second.close();
    AString<NWB::Log::LogArena> firstText(testArena.arena);
    AString<NWB::Log::LogArena> secondText(testArena.arena);
    ASSERT_TRUE(ReadTextFile(first.path(), firstText));
    ASSERT_TRUE(ReadTextFile(second.path(), secondText));
    EXPECT_TRUE(Contains(firstText, "first record"));
    EXPECT_FALSE(Contains(firstText, "second record"));
    EXPECT_TRUE(Contains(secondText, "second record"));
    EXPECT_FALSE(Contains(secondText, "first record"));
}

TEST_F(LoggerProducerContracts, MissingArchiveRetainsNativeReadFailureInOwningRejection){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_missing_archive_native_error_test");
    constexpr AStringView s_Stem("missing_archive_001");
    RemoveTestArtifacts(arena, s_Group);
    const auto config = MakeIngestConfig(arena, s_Group);
    const auto path = ArchivePath(arena, s_Group, s_Stem);
    const auto missing = FileSize(path);
    ASSERT_FALSE(missing);
    const auto result = NWB::Log::ProcessCrashUpload(arena, path, config);
    EXPECT_FALSE(result.accepted);
    EXPECT_TRUE(Contains(result.message, NWB_TEXT("failed to read crash archive")));
    const auto native = MakeDiagnosticEventText("native_category='{}'; native_code={}", missing.error().category().name(), missing.error().value());
    constexpr Name s_FailureTextScratchName = NWB::Tests::LoggerCrashCases::s_LoggerCrashTestNameTestsIntegrationLoggerServerProducerContractsScratch;
    NWB::Core::Alloc::ScratchArena scratchArena(s_FailureTextScratchName);
    const auto nativeText = ConvertString(scratchArena, native.view());
    EXPECT_TRUE(Contains(result.message, nativeText));
    RemoveTestArtifacts(arena, s_Group);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_WINDOWS)

#include <tests/common/test_context.h>
#include <logger/server/log_row_painter.h>
#include <global/scope_exit.h>
#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_log_row_painter_tests{
using namespace NWB;

TEST(LogRowPainter, EmptyErrorRowsUseAlternatingColorsAndRestoreDeviceContext){
    Tests::TestArena<> arena;
    const HDC dc = CreateCompatibleDC(nullptr);
    ASSERT_NE(dc, nullptr);
    ScopeExit releaseDc([&]()noexcept{ DeleteDC(dc); });
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = 4;
    info.bmiHeader.biHeight = -4;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    const HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0u);
    ASSERT_NE(bitmap, nullptr);
    ScopeExit releaseBitmap([&]()noexcept{ DeleteObject(bitmap); });
    const HGDIOBJ previous = SelectObject(dc, bitmap);
    ASSERT_TRUE(previous && previous != HGDI_ERROR);
    ScopeExit restoreBitmap([&]()noexcept{ SelectObject(dc, previous); });
    ASSERT_NE(pixels, nullptr);
    ASSERT_NE(SetTextColor(dc, RGB(3, 4, 5)), CLR_INVALID);
    ASSERT_NE(SetBkMode(dc, OPAQUE), 0);
    Log::FrameDetail::MessageItem message(Log::LogString(NWB_TEXT(""), arena.arena), Core::Common::LogType::Error);
    DRAWITEMSTRUCT item{};
    item.hDC = dc;
    item.rcItem = RECT{ 0, 0, 4, 4 };
    Log::FrameDetail::LogRowPainter::Draw(item, message);
    EXPECT_EQ(GetPixel(dc, 1, 1), RGB(170, 0, 0));
    item.itemID = 1u;
    Log::FrameDetail::LogRowPainter::Draw(item, message);
    EXPECT_EQ(GetPixel(dc, 1, 1), RGB(200, 0, 0));
    EXPECT_EQ(GetTextColor(dc), RGB(3, 4, 5));
    EXPECT_EQ(GetBkMode(dc), OPAQUE);
}


TEST(LogRowPainter, LongTextWrapsWithinHiddenParentClientWidth){
    Tests::TestArena<> arena;
    const HWND parent = CreateWindowEx(0, NWB_TEXT("STATIC"), NWB_TEXT(""), WS_POPUP, 0, 0, 60, 100, nullptr, nullptr, GetModuleHandle(nullptr), nullptr);
    ASSERT_NE(parent, nullptr);
    ScopeExit destroyParent([&]()noexcept{ DestroyWindow(parent); });
    const HWND child = CreateWindowEx(0, NWB_TEXT("STATIC"), NWB_TEXT(""), WS_CHILD, 0, 0, 60, 100, parent, nullptr, GetModuleHandle(nullptr), nullptr);
    ASSERT_NE(child, nullptr);
    SendMessage(child, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), FALSE);
    Log::FrameDetail::MessageItem message(Log::LogString(NWB_TEXT("A long message that must wrap across several narrow rows of the hidden logger window."), arena.arena), Core::Common::LogType::Info);
    MEASUREITEMSTRUCT item{};
    Log::FrameDetail::LogRowPainter::Measure(parent, child, item, message);
    EXPECT_GT(item.itemHeight, static_cast<UINT>(Log::FrameDetail::LogRowPainter::s_FontHeight));
}

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#endif


#if defined(NWB_PLATFORM_WINDOWS)

#include <tests/common/capturing_logger.h>
#include <tests/common/test_context.h>
#include <tests/integration/logger_server/name_cases.adhd.h>

#include <core/alloc/scratch.h>
#include <core/crash/package_names.h>

#include <global/filesystem/operations.h>
#include <global/filesystem/utility.h>

#include <logger/server/crash_symbolicate.h>

#include <gtest/gtest.h>

#if defined(NWB_PLATFORM_WINDOWS)
#include <windows.h>
#include <dbghelp.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_minidump_boundary_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_WindowsPlatform = "windows";

using namespace NWB;
using DumpBytes = Vector<u8, Core::Alloc::GlobalArena>;

template<typename T>
void StoreDumpRecord(const usize offset, const T& record, const Span<u8> bytes)noexcept{
    NWB_ASSERT(offset <= bytes.size() && sizeof(T) <= bytes.size() - offset);
    NWB_MEMCPY(bytes.data() + offset, bytes.size() - offset, &record, sizeof(T));
}

[[nodiscard]] DumpBytes ExceptionDump(Core::Alloc::GlobalArena& arena, const MINIDUMP_STREAM_TYPE secondStream, const ULONG secondStreamBytes){
    constexpr usize s_DirectoryCount = 2u;
    constexpr usize s_ExceptionOffset = sizeof(MINIDUMP_HEADER) + s_DirectoryCount * sizeof(MINIDUMP_DIRECTORY);
    constexpr usize s_ContextOffset = s_ExceptionOffset + sizeof(MINIDUMP_EXCEPTION_STREAM);
    constexpr usize s_SecondStreamOffset = s_ContextOffset + sizeof(CONTEXT);
    DumpBytes bytes(arena);
    bytes.resize(s_SecondStreamOffset + secondStreamBytes, 0u);
    MINIDUMP_HEADER header{};
    header.Signature = MINIDUMP_SIGNATURE;
    header.Version = MINIDUMP_VERSION;
    header.NumberOfStreams = s_DirectoryCount;
    header.StreamDirectoryRva = sizeof(MINIDUMP_HEADER);
    StoreDumpRecord(0u, header, bytes);
    MINIDUMP_DIRECTORY exceptionDirectory{};
    exceptionDirectory.StreamType = ExceptionStream;
    exceptionDirectory.Location.DataSize = sizeof(MINIDUMP_EXCEPTION_STREAM);
    exceptionDirectory.Location.Rva = s_ExceptionOffset;
    StoreDumpRecord(sizeof(MINIDUMP_HEADER), exceptionDirectory, bytes);
    MINIDUMP_EXCEPTION_STREAM exception{};
    exception.ThreadId = 1u;
    exception.ThreadContext.DataSize = sizeof(CONTEXT);
    exception.ThreadContext.Rva = s_ContextOffset;
    StoreDumpRecord(s_ExceptionOffset, exception, bytes);
    MINIDUMP_DIRECTORY secondDirectory{};
    secondDirectory.StreamType = secondStream;
    secondDirectory.Location.DataSize = secondStreamBytes;
    secondDirectory.Location.Rva = s_SecondStreamOffset;
    StoreDumpRecord(sizeof(MINIDUMP_HEADER) + sizeof(MINIDUMP_DIRECTORY), secondDirectory, bytes);
    return bytes;
}

class MinidumpBoundaryTests : public testing::Test{
public:
    MinidumpBoundaryTests()
        : m_directory(m_testArena.arena, "logserver_minidump_boundary_tests")
        , m_loggerGuard(m_logger, Core::Common::LoggerBreakPolicy::ReportOnly)
    {}


protected:
    virtual void SetUp()override{
        ASSERT_TRUE(EnsureEmptyDirectory(m_directory));
    }

    virtual void TearDown()override{
        EXPECT_TRUE(RemoveAllIfExists(m_directory));
    }

    [[nodiscard]] Expected<Log::CrashReportText> report(const Span<const u8> bytes){
        if(!WriteBinaryFile(m_directory / Core::Crash::PackageNames::s_ProcessDumpFileName, bytes))
            return MakeUnexpected(Failure{});
        Log::CrashPackageSummary summary(m_testArena.arena);
        summary.platform = s_WindowsPlatform;
        summary.threadId = 1u;
        Log::CrashSymbolicationConfig config(m_testArena.arena);
        const Log::CrashScratchPath scratchDirectory(m_scratchArena, m_directory);
        return Log::BuildCrashSymbolicationReport(scratchDirectory, summary, config, m_testArena.arena, m_scratchArena);
    }


protected:
    Tests::TestArena<> m_testArena;
    Core::Alloc::ScratchArena m_scratchArena{ Name(NWB::Tests::LoggerCrashCases::s_LoggerCrashFixtureNameTestsMinidumpBoundaryScratch) };
    ::Path<Core::Alloc::GlobalArena> m_directory;
    Tests::CapturingLogger m_logger;
    Core::Common::LoggerRegistrationGuard m_loggerGuard;
};

TEST_F(MinidumpBoundaryTests, MissingDumpRetainsNativeReadFailureInReturnedReport){
    const auto missing = FileSize(m_directory / Core::Crash::PackageNames::s_ProcessDumpFileName);
    ASSERT_FALSE(missing);
    Log::CrashPackageSummary summary(m_testArena.arena);
    summary.platform = s_WindowsPlatform;
    Log::CrashSymbolicationConfig config(m_testArena.arena);
    const Log::CrashScratchPath scratchDirectory(m_scratchArena, m_directory);
    const auto actual = Log::BuildCrashSymbolicationReport(scratchDirectory, summary, config, m_testArena.arena, m_scratchArena);
    EXPECT_NE(actual.find("status=not_decoded"), Log::CrashReportText::npos);
    const auto native = MakeDiagnosticEventText("native_category={}\nnative_code={}\n", missing.error().category().name(), missing.error().value());
    EXPECT_NE(actual.find(native.view()), Log::CrashReportText::npos);
}

TEST_F(MinidumpBoundaryTests, TruncatedHeadersAndOutOfFileStreamDirectoriesCannotDecode){
    DumpBytes bytes(m_testArena.arena);
    bytes.resize(sizeof(MINIDUMP_HEADER), 0u);
    for(const usize size : Array<usize, 3u>{ 1u, sizeof(MINIDUMP_HEADER) - 1u, sizeof(MINIDUMP_HEADER) }){
        bytes.resize(size, 0u);
        const auto actual = report(bytes);
        ASSERT_TRUE(actual);
        EXPECT_NE(actual->find("status=not_decoded"), Log::CrashReportText::npos);
    }
    bytes.resize(sizeof(MINIDUMP_HEADER), 0u);
    MINIDUMP_HEADER header{};
    header.Signature = MINIDUMP_SIGNATURE;
    header.Version = MINIDUMP_VERSION;
    header.StreamDirectoryRva = sizeof(MINIDUMP_HEADER);
    for(const ULONG count : Array<ULONG, 2u>{ 1u, Limit<ULONG>::s_Max }){
        header.NumberOfStreams = count;
        StoreDumpRecord(0u, header, bytes);
        const auto actual = report(bytes);
        ASSERT_TRUE(actual);
        EXPECT_NE(actual->find("status=not_decoded"), Log::CrashReportText::npos);
    }
    header.NumberOfStreams = 1u;
    header.StreamDirectoryRva = Limit<RVA>::s_Max;
    StoreDumpRecord(0u, header, bytes);
    const auto actual = report(bytes);
    ASSERT_TRUE(actual);
    EXPECT_NE(actual->find("status=not_decoded"), Log::CrashReportText::npos);
}

TEST_F(MinidumpBoundaryTests, TruncatedThreadListCountCannotReadBeyondItsAdmittedStream){
    auto bytes = ExceptionDump(m_testArena.arena, ThreadListStream, sizeof(ULONG));
    const ULONG count = Limit<ULONG>::s_Max;
    StoreDumpRecord(bytes.size() - sizeof(ULONG), count, bytes);
    const auto actual = report(bytes);
    ASSERT_TRUE(actual);
    EXPECT_NE(actual->find("status=not_decoded"), Log::CrashReportText::npos);
}

TEST_F(MinidumpBoundaryTests, ShortExceptionContextCannotBorrowSurroundingFileBytes){
    auto bytes = ExceptionDump(m_testArena.arena, UnusedStream, 0u);
    constexpr usize s_ExceptionOffset = sizeof(MINIDUMP_HEADER) + 2u * sizeof(MINIDUMP_DIRECTORY);
    MINIDUMP_EXCEPTION_STREAM exception{};
    exception.ThreadId = 1u;
    exception.ThreadContext.DataSize = sizeof(CONTEXT) - 1u;
    exception.ThreadContext.Rva = s_ExceptionOffset + sizeof(MINIDUMP_EXCEPTION_STREAM);
    StoreDumpRecord(s_ExceptionOffset, exception, bytes);
    const auto actual = report(bytes);
    ASSERT_TRUE(actual);
    EXPECT_NE(actual->find("status=not_decoded"), Log::CrashReportText::npos);
    EXPECT_NE(actual->find("no usable thread context"), Log::CrashReportText::npos);
}

TEST_F(MinidumpBoundaryTests, Memory64OffsetsAndLengthsCannotTruncateIntoASmallFile){
    for(const b8 oversizedOffset : Array<b8, 2u>{ false, true }){
        constexpr usize s_StreamSize = offsetof(MINIDUMP_MEMORY64_LIST, MemoryRanges) + sizeof(MINIDUMP_MEMORY_DESCRIPTOR64);
        auto bytes = ExceptionDump(m_testArena.arena, Memory64ListStream, s_StreamSize);
        const usize streamOffset = bytes.size() - s_StreamSize;
        const ULONG64 count = 1u;
        const RVA64 baseRva = oversizedOffset ? (u64(1u) << 32u) + bytes.size() - 1u : bytes.size() - 1u;
        MINIDUMP_MEMORY_DESCRIPTOR64 memory{};
        memory.StartOfMemoryRange = 0x1000u;
        memory.DataSize = oversizedOffset ? 1u : (u64(1u) << 32u) + 1u;
        StoreDumpRecord(streamOffset + offsetof(MINIDUMP_MEMORY64_LIST, NumberOfMemoryRanges), count, bytes);
        StoreDumpRecord(streamOffset + offsetof(MINIDUMP_MEMORY64_LIST, BaseRva), baseRva, bytes);
        StoreDumpRecord(streamOffset + offsetof(MINIDUMP_MEMORY64_LIST, MemoryRanges), memory, bytes);
        const auto actual = report(bytes);
        ASSERT_TRUE(actual);
        EXPECT_NE(actual->find("status=not_decoded"), Log::CrashReportText::npos);
        EXPECT_NE(actual->find("minidump contains invalid memory ranges"), Log::CrashReportText::npos);
    }
}

TEST_F(MinidumpBoundaryTests, TruncatedModuleListCannotReadBeyondItsAdmittedStream){
    auto bytes = ExceptionDump(m_testArena.arena, ModuleListStream, sizeof(ULONG));
    const ULONG count = Limit<ULONG>::s_Max;
    StoreDumpRecord(bytes.size() - sizeof(ULONG), count, bytes);
    const auto actual = report(bytes);
    ASSERT_TRUE(actual);
    EXPECT_NE(actual->find("module_load=invalid_module_list"), Log::CrashReportText::npos);
}


TEST_F(MinidumpBoundaryTests, InvalidModuleNameLengthsAndTerminatorsCannotBorrowOtherDumpBytes){
    struct InvalidName{
        ULONG byteCount;
        b8 terminated;
        b8 includeTerminator;
    };
    for(const auto& invalid : Array<InvalidName, 4u>{ InvalidName{ 1u, true, true }, InvalidName{ Limit<ULONG>::s_Max - 1u, true, true }, InvalidName{ sizeof(wchar), false, true }, InvalidName{ sizeof(wchar), false, false } }){
        constexpr usize s_StreamSize = offsetof(MINIDUMP_MODULE_LIST, Modules) + sizeof(MINIDUMP_MODULE);
        auto bytes = ExceptionDump(m_testArena.arena, ModuleListStream, s_StreamSize);
        const usize streamOffset = bytes.size() - s_StreamSize;
        const ULONG count = 1u;
        const RVA nameOffset = static_cast<RVA>(bytes.size());
        MINIDUMP_MODULE module{};
        module.ModuleNameRva = nameOffset;
        StoreDumpRecord(streamOffset, count, bytes);
        StoreDumpRecord(streamOffset + offsetof(MINIDUMP_MODULE_LIST, Modules), module, bytes);
        bytes.resize(bytes.size() + sizeof(ULONG) + sizeof(wchar) * (invalid.includeTerminator ? 2u : 1u), 0u);
        StoreDumpRecord(nameOffset, invalid.byteCount, bytes);
        const wchar character = L'a';
        StoreDumpRecord(nameOffset + sizeof(ULONG), character, bytes);
        if(invalid.includeTerminator){
            const wchar terminator = invalid.terminated ? L'\0' : L'x';
            StoreDumpRecord(nameOffset + sizeof(ULONG) + sizeof(wchar), terminator, bytes);
        }
        const auto actual = report(bytes);
        ASSERT_TRUE(actual);
        EXPECT_NE(actual->find("module_load=invalid_module_list"), Log::CrashReportText::npos);
    }
}

TEST_F(MinidumpBoundaryTests, UnalignedExceptionAndContextRecordsKeepTheirBoundedValues){
    const auto original = ExceptionDump(m_testArena.arena, UnusedStream, 0u);
    constexpr usize s_ExceptionOffset = sizeof(MINIDUMP_HEADER) + 2u * sizeof(MINIDUMP_DIRECTORY);
    DumpBytes bytes(m_testArena.arena);
    bytes.resize(original.size() + 1u, 0u);
    NWB_MEMCPY(bytes.data(), bytes.size(), original.data(), s_ExceptionOffset);
    NWB_MEMCPY(bytes.data() + s_ExceptionOffset + 1u, bytes.size() - s_ExceptionOffset - 1u, original.data() + s_ExceptionOffset, original.size() - s_ExceptionOffset);
    MINIDUMP_DIRECTORY directory{};
    directory.StreamType = ExceptionStream;
    directory.Location.DataSize = sizeof(MINIDUMP_EXCEPTION_STREAM);
    directory.Location.Rva = s_ExceptionOffset + 1u;
    StoreDumpRecord(sizeof(MINIDUMP_HEADER), directory, bytes);
    MINIDUMP_EXCEPTION_STREAM exception{};
    exception.ThreadId = 1u;
    exception.ThreadContext.DataSize = sizeof(CONTEXT);
    exception.ThreadContext.Rva = s_ExceptionOffset + sizeof(MINIDUMP_EXCEPTION_STREAM) + 1u;
    StoreDumpRecord(s_ExceptionOffset + 1u, exception, bytes);
    const auto actual = report(bytes);
    ASSERT_TRUE(actual);
    EXPECT_NE(actual->find("status=decoded"), Log::CrashReportText::npos);
    EXPECT_EQ(actual->find("status=not_decoded"), Log::CrashReportText::npos);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#endif


#if defined(NWB_PLATFORM_WINDOWS)

#include <tests/common/test_context.h>

#include <global/scope_exit.h>
#include <global/termination.h>

#include <logger/server/clipboard_text_internal.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_logger_server_clipboard_text_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;

TEST(LoggerClipboardText, EmptyInputOwnsOneTerminatedNativeString){
    Tests::TestArena<> testArena;
    Log::FrameDetail::MessageDeque messages(testArena.arena);
    auto memory = Log::FrameDetail::BuildClipboardText(messages);
    ASSERT_TRUE(memory);
    EXPECT_GE(GlobalSize(memory->native()), sizeof(tchar));
    const auto* const text = static_cast<const tchar*>(GlobalLock(memory->native()));
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(text[0], tchar{});
    SetLastError(NO_ERROR);
    EXPECT_TRUE(GlobalUnlock(memory->native()) || GetLastError() == NO_ERROR);
}

TEST(LoggerClipboardText, MultipleUnicodeMessagesPreserveOrderCrLfAndTerminator){
    Tests::TestArena<> testArena;
    Log::FrameDetail::MessageDeque messages(testArena.arena);
    messages.emplace_back(Log::LogString(NWB_TEXT("first"), testArena.arena), Core::Common::LogType::Info);
    messages.emplace_back(Log::LogString(NWB_TEXT("\uD55C\uAE00 \u03A9"), testArena.arena), Core::Common::LogType::Warning);
    auto memory = Log::FrameDetail::BuildClipboardText(messages);
    ASSERT_TRUE(memory);
    constexpr TStringView s_Expected = NWB_TEXT("first\r\n\uD55C\uAE00 \u03A9\r\n");
    EXPECT_GE(GlobalSize(memory->native()), (s_Expected.size() + 1u) * sizeof(tchar));
    const auto* const text = static_cast<const tchar*>(GlobalLock(memory->native()));
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(TStringView(text, s_Expected.size()), s_Expected);
    EXPECT_EQ(text[s_Expected.size()], tchar{});
    SetLastError(NO_ERROR);
    EXPECT_TRUE(GlobalUnlock(memory->native()) || GetLastError() == NO_ERROR);
}

TEST(LoggerClipboardText, NativeOwnershipFreesUntransferredMemoryAndReleasesTransferredMemory){
    Tests::TestArena<> testArena;
    Log::FrameDetail::MessageDeque messages(testArena.arena);
    HGLOBAL freed = nullptr;
    {
        auto memory = Log::FrameDetail::BuildClipboardText(messages);
        ASSERT_TRUE(memory);
        freed = memory->native();
    }
    EXPECT_EQ(GlobalFlags(freed), GMEM_INVALID_HANDLE);
    HGLOBAL transferred = nullptr;
    {
        auto memory = Log::FrameDetail::BuildClipboardText(messages);
        ASSERT_TRUE(memory);
        transferred = memory->native();
        memory->release();
    }
    ScopeExit cleanup([&]()noexcept{
        if(transferred && GlobalFree(transferred))
            TerminateInvariant();
    });
    EXPECT_NE(GlobalFlags(transferred), GMEM_INVALID_HANDLE);
    EXPECT_NE(GlobalSize(transferred), 0u);
}

TEST(LoggerClipboardText, ByteCountRejectsOverflowBeforeNativeAllocation){
    const auto empty = Log::FrameDetail::ClipboardByteCount(0u);
    ASSERT_TRUE(empty);
    EXPECT_EQ(*empty, sizeof(tchar));
    constexpr usize s_MaxCharacters = (Limit<usize>::s_Max / sizeof(tchar)) - 1u;
    const auto boundary = Log::FrameDetail::ClipboardByteCount(s_MaxCharacters);
    ASSERT_TRUE(boundary);
    EXPECT_EQ(*boundary, (s_MaxCharacters + 1u) * sizeof(tchar));
    const auto overflow = Log::FrameDetail::ClipboardByteCount(s_MaxCharacters + 1u);
    ASSERT_FALSE(overflow);
    EXPECT_EQ(overflow.error(), MakeErrorCode(ErrorCondition::value_too_large));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#endif


