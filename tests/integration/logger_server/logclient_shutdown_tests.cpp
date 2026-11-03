// limztudio@gmail.com
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

