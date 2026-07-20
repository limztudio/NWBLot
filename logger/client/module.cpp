// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"

#include <curl/curl.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_log_client{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr long s_ConnectTimeoutMs = 1000L;
static constexpr long s_RequestTimeoutMs = 2000L;
static constexpr u32 s_RetrySleepMs = 100u;

[[nodiscard]] static AString<LogArena> UrlWithEndpoint(LogArena& arena, const AStringView baseUrl, const AStringView endpoint){
    AString<LogArena> output(baseUrl, arena);
    if(output.empty() || endpoint.empty())
        return output;

    const bool baseHasSlash = output.back() == '/';
    const bool endpointHasSlash = endpoint.front() == '/';
    if(baseHasSlash && endpointHasSlash)
        output.pop_back();
    else if(!baseHasSlash && !endpointHasSlash)
        output += '/';

    output += endpoint;
    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Client::globalInit(){
    CURLcode ret;

    ret = curl_global_init(CURL_GLOBAL_ALL);
    if(ret != CURLE_OK)
        return false;

    return true;
}


Client::Client()
    : UpdateBaseType("NWB::Log::Client")
    , m_curl(nullptr)
    , m_pendingPayload(BaseType::arena())
    , m_messageUrl(BaseType::arena())
    , m_telemetryUrl(BaseType::arena())
    , m_hasPendingPayload(false)
    , m_pendingPayloadKind(ClientPayloadKind::Message)
    , m_msgCount(0)
    , m_telemetryCount(0)
    , m_telemetryQueue(BaseType::arena())
{}
Client::~Client(){
    stopWorker();

    if(m_curl){
        curl_easy_cleanup(static_cast<CURL*>(m_curl));
        m_curl = nullptr;
    }
}

bool Client::internalInit(NotNull<const char*> url){
    m_messageUrl = AStringView(url.get());
    m_telemetryUrl = __hidden_log_client::UrlWithEndpoint(BaseType::arena(), AStringView(url.get()), AStringView(s_TelemetryUploadEndpoint));

    m_curl = curl_easy_init();
    if(!m_curl){
        enqueue(StringFormat(BaseType::arena(), NWB_TEXT("Failed to initialize CURL on {}"), CLIENT_NAME), Type::Fatal);
        return false;
    }

    CURL* const curlHandle = static_cast<CURL*>(m_curl);
    CURLcode ret;

    ret = curl_easy_setopt(curlHandle, CURLOPT_POST, 1);
    if(ret != CURLE_OK){
        enqueue(StringFormat(BaseType::arena(), NWB_TEXT("Failed to set post on {}: {}"), CLIENT_NAME, StringConvert(BaseType::arena(), curl_easy_strerror(ret))), Type::Fatal);
        return false;
    }

    ret = curl_easy_setopt(curlHandle, CURLOPT_NOSIGNAL, 1L);
    if(ret != CURLE_OK){
        enqueue(StringFormat(BaseType::arena(), NWB_TEXT("Failed to set no-signal mode on {}: {}"), CLIENT_NAME, StringConvert(BaseType::arena(), curl_easy_strerror(ret))), Type::Fatal);
        return false;
    }

    ret = curl_easy_setopt(curlHandle, CURLOPT_CONNECTTIMEOUT_MS, __hidden_log_client::s_ConnectTimeoutMs);
    if(ret != CURLE_OK){
        enqueue(StringFormat(BaseType::arena(), NWB_TEXT("Failed to set connect timeout on {}: {}"), CLIENT_NAME, StringConvert(BaseType::arena(), curl_easy_strerror(ret))), Type::Fatal);
        return false;
    }

    ret = curl_easy_setopt(curlHandle, CURLOPT_TIMEOUT_MS, __hidden_log_client::s_RequestTimeoutMs);
    if(ret != CURLE_OK){
        enqueue(StringFormat(BaseType::arena(), NWB_TEXT("Failed to set request timeout on {}: {}"), CLIENT_NAME, StringConvert(BaseType::arena(), curl_easy_strerror(ret))), Type::Fatal);
        return false;
    }

    return true;
}
bool Client::enqueueTelemetry(const void* const bytes, const usize byteCount){
    if(!bytes || byteCount == 0u)
        return false;

    LogBytes upload(BaseType::arena());
    upload.resize(byteCount);
    NWB_MEMCPY(upload.data(), upload.size(), bytes, byteCount);
    m_telemetryQueue.emplace(Move(upload));
    m_telemetryCount.fetch_add(1u, MemoryOrder::relaxed);
    this->m_semaphore.release();
    return true;
}
bool Client::internalUpdate(){
    if(!m_hasPendingPayload){
        if(m_telemetryCount.load(MemoryOrder::relaxed)){
            LogBytes upload(BaseType::arena());
            if(m_telemetryQueue.try_pop(upload)){
                m_telemetryCount.fetch_sub(1u, MemoryOrder::relaxed);
                m_pendingPayload = Move(upload);
                m_pendingPayloadKind = ClientPayloadKind::Telemetry;
                m_hasPendingPayload = true;
            }
        }

        if(!m_hasPendingPayload){
            if(!m_msgCount.load(MemoryOrder::relaxed))
                return true;

            MessageType msg = MakeMessageType(BaseType::arena());
            if(!try_dequeue(msg))
                return true;

            if(!BuildMessagePayload(msg, m_pendingPayload)){
                const MessageType fallbackMsg = MakeTuple(
                    Timer{},
                    Type::Error,
                    LogString(NWB_TEXT("Logger client dropped an oversized message"), BaseType::arena())
                );
                if(!BuildMessagePayload(fallbackMsg, m_pendingPayload))
                    return true;
            }
            m_pendingPayloadKind = ClientPayloadKind::Message;
            m_hasPendingPayload = true;
        }
    }

    auto scheduleRetry = [this](){
        if(this->m_exit.load(MemoryOrder::acquire))
            return;

        SleepMS(__hidden_log_client::s_RetrySleepMs);
        if(this->m_exit.load(MemoryOrder::acquire))
            return;

        this->m_semaphore.release();
    };

    CURL* const curlHandle = static_cast<CURL*>(m_curl);
    CURLcode ret;
    if(m_pendingPayload.size() > static_cast<usize>(Limit<curl_off_t>::s_Max)){
        m_pendingPayload.clear();
        m_hasPendingPayload = false;
        return true;
    }

    ret = curl_easy_setopt(
        curlHandle,
        CURLOPT_URL,
        m_pendingPayloadKind == ClientPayloadKind::Telemetry
            ? m_telemetryUrl.c_str()
            : m_messageUrl.c_str()
    );
    if(ret != CURLE_OK){
        scheduleRetry();
        return true;
    }

    ret = curl_easy_setopt(curlHandle, CURLOPT_POSTFIELDS, reinterpret_cast<char*>(m_pendingPayload.data()));
    if(ret != CURLE_OK){
        scheduleRetry();
        return true;
    }

    ret = curl_easy_setopt(curlHandle, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(m_pendingPayload.size()));
    if(ret != CURLE_OK){
        scheduleRetry();
        return true;
    }

    ret = curl_easy_perform(curlHandle);
    if(ret != CURLE_OK){
        scheduleRetry();
        return true;
    }

    m_pendingPayload.clear();
    m_hasPendingPayload = false;
    m_pendingPayloadKind = ClientPayloadKind::Message;
    return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ClientStandalone::globalInit(){
    return true;
}


ClientStandalone::ClientStandalone()
    : UpdateBaseType("NWB::Log::ClientStandalone")
    , m_processedMsgFile(BaseType::arena())
{}
ClientStandalone::~ClientStandalone(){
    stopWorker();
    m_processedMsgFile.close();
}


bool ClientStandalone::internalInit(BasicStringView<tchar> logFileNameBase){
    if(logFileNameBase.empty())
        return m_processedMsgFile.openByExecutableName();

    return m_processedMsgFile.open(logFileNameBase);
}
bool ClientStandalone::internalUpdate(){
    MessageType msg = MakeMessageType(BaseType::arena());
    while(tryDequeue(msg)){
        const LogString formattedMessage = FormatMessageForProcessing(BaseType::arena(), msg);

        NWB_TCOUT << formattedMessage << static_cast<tchar>('\n');
        m_processedMsgFile.writeLine(formattedMessage);
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

