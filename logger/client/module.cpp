// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"

#include <global/diagnostics.h>

#include <curl/curl.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_log_client{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr long s_ConnectTimeoutMs = 1000L;
static constexpr long s_RequestTimeoutMs = 2000L;
static constexpr u32 s_RetrySleepMs = 100u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void CaptureInitializationFailure(const AStringView operation, const CURLcode code)noexcept{
    const DiagnosticEventText detail = MakeDiagnosticEventText("{}: native_category=curl native_code={} detail={}", operation, code, curl_easy_strerror(code));
    CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = DiagnosticEventName::s_Error, .category = "logger_client_initialization", .message = detail.view(), .file = __FILE__, .line = __LINE__, });
}

[[nodiscard]] static AString<LogArena> UrlWithEndpoint(LogArena& arena, const AStringView baseUrl, const AStringView endpoint){
    AString<LogArena> output(AStringView(baseUrl.data(), baseUrl.size()), arena);
    if(output.empty() || endpoint.empty())
        return output;

    const bool baseHasSlash = output.back() == static_cast<char>('/');
    const bool endpointHasSlash = endpoint.front() == static_cast<char>('/');
    if(baseHasSlash && endpointHasSlash)
        output.pop_back();
    else if(!baseHasSlash && !endpointHasSlash)
        output += static_cast<char>('/');

    output.append(endpoint.data(), endpoint.size());
    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Client::GlobalInit()noexcept{
    CURLcode curlResult;

    curlResult = curl_global_init(CURL_GLOBAL_ALL);
    if(curlResult != CURLE_OK){
        __hidden_log_client::CaptureInitializationFailure("curl_global_init", curlResult);
        return false;
    }

    return true;
}


Client::Client()
    : ClientBaseType("NWB::Log::Client")
    , m_curl(nullptr)
    , m_pendingPayload(BaseType::arena())
    , m_messageUrl(BaseType::arena())
    , m_telemetryUrl(BaseType::arena())
    , m_pendingPayloadKind(ClientPayloadKind::Message)
    , m_hasPendingPayload(false)
    , m_messageCount(0)
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


bool Client::internalInit(const AStringView url){
    m_messageUrl = url;
    m_telemetryUrl = __hidden_log_client::UrlWithEndpoint(BaseType::arena(), url, s_TelemetryUploadEndpoint);

    m_curl = curl_easy_init();
    if(!m_curl){
        __hidden_log_client::CaptureInitializationFailure("curl_easy_init", CURLE_FAILED_INIT);
        return false;
    }

    CURL* const curlHandle = static_cast<CURL*>(m_curl);
    CURLcode curlResult;

    curlResult = curl_easy_setopt(curlHandle, CURLOPT_POST, 1);
    if(curlResult != CURLE_OK){
        __hidden_log_client::CaptureInitializationFailure("CURLOPT_POST", curlResult);
        return false;
    }

    curlResult = curl_easy_setopt(curlHandle, CURLOPT_NOSIGNAL, 1L);
    if(curlResult != CURLE_OK){
        __hidden_log_client::CaptureInitializationFailure("CURLOPT_NOSIGNAL", curlResult);
        return false;
    }

    curlResult = curl_easy_setopt(curlHandle, CURLOPT_CONNECTTIMEOUT_MS, __hidden_log_client::s_ConnectTimeoutMs);
    if(curlResult != CURLE_OK){
        __hidden_log_client::CaptureInitializationFailure("CURLOPT_CONNECTTIMEOUT_MS", curlResult);
        return false;
    }

    curlResult = curl_easy_setopt(curlHandle, CURLOPT_TIMEOUT_MS, __hidden_log_client::s_RequestTimeoutMs);
    if(curlResult != CURLE_OK){
        __hidden_log_client::CaptureInitializationFailure("CURLOPT_TIMEOUT_MS", curlResult);
        return false;
    }

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
            if(!m_messageCount.load(MemoryOrder::relaxed))
                return true;

            MessageType msg = MakeMessageType(BaseType::arena());
            if(!tryDequeueMessage(msg))
                return true;

            const auto builtPayload = BuildMessagePayload(msg, m_pendingPayload);
            if(!builtPayload){
                const DiagnosticEventText detail = MakeDiagnosticEventText("Message payload admission failed: domain=MessagePayloadError code={}", builtPayload.error());
                CaptureDiagnosticEvent("logger_client_payload", detail.view(), __FILE__, __LINE__);
                const MessageType fallbackMsg = MakeTuple(Timer{}, Core::Common::LogType::Error, LogString(NWB_TEXT("Logger client dropped an invalid message payload"), BaseType::arena()));
                if(!BuildMessagePayload(fallbackMsg, m_pendingPayload))
                    return true;
            }
            m_pendingPayloadKind = ClientPayloadKind::Message;
            m_hasPendingPayload = true;
        }
    }

    auto scheduleRetry = [this]()noexcept(noexcept(this->m_semaphore.release())){
        if(this->m_exit.load(MemoryOrder::acquire))
            return false;

        SleepMS(__hidden_log_client::s_RetrySleepMs);
        if(this->m_exit.load(MemoryOrder::acquire))
            return false;

        this->m_semaphore.release();
        return true;
    };

    CURL* const curlHandle = static_cast<CURL*>(m_curl);
    CURLcode curlResult;
    if(m_pendingPayload.size() > static_cast<usize>(Limit<curl_off_t>::s_Max)){
        m_pendingPayload.clear();
        m_hasPendingPayload = false;
        return true;
    }

    curlResult = curl_easy_setopt(curlHandle, CURLOPT_URL, m_pendingPayloadKind == ClientPayloadKind::Telemetry ? m_telemetryUrl.c_str() : m_messageUrl.c_str());
    if(curlResult != CURLE_OK)
        return scheduleRetry();

    curlResult = curl_easy_setopt(curlHandle, CURLOPT_POSTFIELDS, reinterpret_cast<char*>(m_pendingPayload.data()));
    if(curlResult != CURLE_OK)
        return scheduleRetry();

    curlResult = curl_easy_setopt(curlHandle, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(m_pendingPayload.size()));
    if(curlResult != CURLE_OK)
        return scheduleRetry();

    curlResult = curl_easy_perform(curlHandle);
    if(curlResult != CURLE_OK)
        return scheduleRetry();

    m_pendingPayload.clear();
    m_hasPendingPayload = false;
    m_pendingPayloadKind = ClientPayloadKind::Message;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ClientStandalone::ClientStandalone()
    : ClientBaseType("NWB::Log::ClientStandalone")
    , m_processedMessageFile(BaseType::arena())
{}
ClientStandalone::~ClientStandalone(){
    stopWorker();
    m_processedMessageFile.close();
}


bool ClientStandalone::internalInit(BasicStringView<tchar> logFileNameBase){
    if(logFileNameBase.empty())
        return m_processedMessageFile.openByExecutableName();

    return m_processedMessageFile.open(logFileNameBase);
}
bool ClientStandalone::internalUpdate(){
    MessageType msg = MakeMessageType(BaseType::arena());
    while(tryDequeue(msg)){
        const LogString formattedMessage = FormatMessageForProcessing(BaseType::arena(), msg);

        auto& stream = MessageTypeWritesToErrorStream(Get<1>(msg)) ? NWB_TCERR : NWB_TCOUT;
        stream << formattedMessage << static_cast<tchar>('\n');
        if(!m_processedMessageFile.writeLine(formattedMessage))
            NWB_TCERR << NWB_TEXT("Log client: failed to write processed message to file.\n");
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

