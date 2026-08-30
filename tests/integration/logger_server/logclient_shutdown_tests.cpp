// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <logger/client/module.h>

#include <microhttpd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_logclient_shutdown_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_ShutdownMessageCount = 32u;
inline constexpr u32 s_FirstRequestDelayMilliseconds = 250u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ShutdownCaptureServer final : NoCopy{
private:
    static MHD_Result requestCallback(
        void* cls,
        MHD_Connection* connection,
        const char* url,
        const char* method,
        const char* version,
        const char* uploadData,
        size_t* uploadDataSize,
        void** connectionContext
    );


public:
    ShutdownCaptureServer() = default;
    ~ShutdownCaptureServer();


public:
    [[nodiscard]] bool start();
    [[nodiscard]] inline u16 port()const{ return m_port; }
    [[nodiscard]] inline u32 requestCount()const{ return m_requestCount.load(MemoryOrder::acquire); }


private:
    MHD_Daemon* m_daemon = nullptr;
    Atomic<bool> m_delayedFirstRequest = false;
    Atomic<u32> m_requestCount = 0u;
    u16 m_port = 0u;
};


MHD_Result ShutdownCaptureServer::requestCallback(
    void* const cls,
    MHD_Connection* const connection,
    const char* const url,
    const char* const method,
    const char* const version,
    const char* const uploadData,
    size_t* const uploadDataSize,
    void** const connectionContext
){
    static_cast<void>(url);
    static_cast<void>(version);
    static_cast<void>(uploadData);

    if(!cls || !connection || !method || !uploadDataSize || !connectionContext)
        return MHD_NO;
    if(NWB_STRCMP(method, "POST") != 0)
        return MHD_NO;

    if(!*connectionContext){
        *connectionContext = cls;
        return MHD_YES;
    }
    if(*uploadDataSize != 0u){
        *uploadDataSize = 0u;
        return MHD_YES;
    }

    auto& server = *static_cast<ShutdownCaptureServer*>(cls);
    if(!server.m_delayedFirstRequest.exchange(true, MemoryOrder::acq_rel))
        SleepMS(s_FirstRequestDelayMilliseconds);
    server.m_requestCount.fetch_add(1u, MemoryOrder::release);

    static char s_EmptyResponse[] = "";
    MHD_Response* const response = MHD_create_response_from_buffer(0u, s_EmptyResponse, MHD_RESPMEM_PERSISTENT);
    if(!response)
        return MHD_NO;

    const MHD_Result result = MHD_queue_response(connection, MHD_HTTP_OK, response);
    MHD_destroy_response(response);
    return result;
}


ShutdownCaptureServer::~ShutdownCaptureServer(){
    if(m_daemon){
        MHD_stop_daemon(m_daemon);
        m_daemon = nullptr;
    }
}


bool ShutdownCaptureServer::start(){
    if(m_daemon)
        return false;

    m_daemon = MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD, 0u, nullptr, nullptr, &ShutdownCaptureServer::requestCallback, this, MHD_OPTION_END);
    if(!m_daemon)
        return false;

    const MHD_DaemonInfo* const daemonInfo = MHD_get_daemon_info(m_daemon, MHD_DAEMON_INFO_BIND_PORT);
    if(!daemonInfo || daemonInfo->port == 0u){
        MHD_stop_daemon(m_daemon);
        m_daemon = nullptr;
        return false;
    }

    m_port = daemonInfo->port;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(LogClientShutdown, DrainsEveryQueuedMessageAfterStopBegins){
    ShutdownCaptureServer server;
    ASSERT_TRUE(server.start());

    {
        NWB::Log::Client client;
        const AString<NWB::Log::LogArena> url = StringFormat(client.arena(), "http://127.0.0.1:{}", server.port());
        ASSERT_TRUE(client.init(MakeNotNull(url.c_str())));

        for(u32 messageIndex = 0u; messageIndex < s_ShutdownMessageCount; ++messageIndex){
            client.enqueue(StringFormat(
                client.arena(),
                NWB_TEXT("Log client shutdown payload {}"),
                messageIndex
            ));
        }
    }

    EXPECT_EQ(server.requestCount(), s_ShutdownMessageCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

