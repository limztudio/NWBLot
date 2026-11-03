// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"

#include <global/diagnostics.h>

#include <curl/curl.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_logger_client{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr long s_ConnectTimeoutMs = 1000L;
static constexpr long s_RequestTimeoutMs = 2000L;
static constexpr u32 s_RetrySleepMs = 100u;
static constexpr u32 s_MaxRetrySleepMs = 5000u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void CaptureInitializationFailure(const AStringView operation, const CURLcode code)noexcept{
    const DiagnosticEventText detail = MakeDiagnosticEventText("{}: native_category=curl native_code={} detail={}", operation, code, curl_easy_strerror(code));
    CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = DiagnosticEventName::s_Error, .category = "logger_client_initialization", .message = detail.view(), .file = __FILE__, .line = __LINE__, });
}

[[nodiscard]] static AString<LogArena> UrlWithEndpoint(LogArena& arena, const AStringView baseUrl, const AStringView endpoint){
    AString<LogArena> output(AStringView(baseUrl.data(), baseUrl.size()), arena);
    if(output.empty() || endpoint.empty())
        return output;

    const b8 baseHasSlash = output.back() == static_cast<char>('/');
    const b8 endpointHasSlash = endpoint.front() == static_cast<char>('/');
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


b8 Client::GlobalInit()noexcept{
    CURLcode curlResult;

    curlResult = curl_global_init(CURL_GLOBAL_ALL);
    if(curlResult != CURLE_OK){
        __hidden_logger_client::CaptureInitializationFailure("curl_global_init", curlResult);
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
    , m_preferTelemetry(true)
    , m_retryDelayMs(__hidden_logger_client::s_RetrySleepMs)
    , m_lastHttpFailureStatus(-1)
    , m_lastCurlFailureCode(0)
    , m_rejectedPayloadCount(0)
    , m_messageCount(0)
    , m_telemetryCount(0)
    , m_telemetryQueue(BaseType::arena())
{}

Client::~Client()noexcept{
    stopWorker();

    if(m_curl){
        curl_easy_cleanup(static_cast<CURL*>(m_curl));
        m_curl = nullptr;
    }
}

b8 Client::enqueueTelemetry(const void* const bytes, const usize byteCount){
    if(!bytes || byteCount == 0u || byteCount > s_MaxTelemetryUploadBytes){
        m_rejectedPayloadCount.fetch_add(1u, MemoryOrder::relaxed);
        return false;
    }

    LogBytes upload(BaseType::arena());
    upload.resize(byteCount);
    NWB_MEMCPY(upload.data(), upload.size(), bytes, byteCount);
    m_telemetryQueue.emplace(Move(upload));
    m_telemetryCount.fetch_add(1u, MemoryOrder::relaxed);
    this->m_semaphore.release();
    return true;
}

b8 Client::internalInit(const AStringView url){
    m_messageUrl = url;
    m_telemetryUrl = __hidden_logger_client::UrlWithEndpoint(BaseType::arena(), url, s_TelemetryUploadEndpoint);

    m_curl = curl_easy_init();
    if(!m_curl){
        __hidden_logger_client::CaptureInitializationFailure("curl_easy_init", CURLE_FAILED_INIT);
        return false;
    }

    const NotNull<CURL*> curlHandle = MakeNotNull(static_cast<CURL*>(m_curl));
    CURLcode curlResult;

    curlResult = curl_easy_setopt(curlHandle.get(), CURLOPT_POST, 1);
    if(curlResult != CURLE_OK){
        __hidden_logger_client::CaptureInitializationFailure("CURLOPT_POST", curlResult);
        return false;
    }

    curlResult = curl_easy_setopt(curlHandle.get(), CURLOPT_NOSIGNAL, 1L);
    if(curlResult != CURLE_OK){
        __hidden_logger_client::CaptureInitializationFailure("CURLOPT_NOSIGNAL", curlResult);
        return false;
    }

    curlResult = curl_easy_setopt(curlHandle.get(), CURLOPT_CONNECTTIMEOUT_MS, __hidden_logger_client::s_ConnectTimeoutMs);
    if(curlResult != CURLE_OK){
        __hidden_logger_client::CaptureInitializationFailure("CURLOPT_CONNECTTIMEOUT_MS", curlResult);
        return false;
    }

    curlResult = curl_easy_setopt(curlHandle.get(), CURLOPT_TIMEOUT_MS, __hidden_logger_client::s_RequestTimeoutMs);
    if(curlResult != CURLE_OK){
        __hidden_logger_client::CaptureInitializationFailure("CURLOPT_TIMEOUT_MS", curlResult);
        return false;
    }

    return true;
}

b8 Client::internalUpdate(){
    if(!m_hasPendingPayload){
        const auto takeTelemetry = [this](){
            if(!m_telemetryCount.load(MemoryOrder::relaxed))
                return;
            LogBytes upload(BaseType::arena());
            if(m_telemetryQueue.try_pop(upload)){
                m_telemetryCount.fetch_sub(1u, MemoryOrder::relaxed);
                m_pendingPayload = Move(upload);
                m_pendingPayloadKind = ClientPayloadKind::Telemetry;
                m_hasPendingPayload = true;
                m_preferTelemetry = false;
            }
        };
        const b8 tryTelemetryFirst = m_preferTelemetry || !m_messageCount.load(MemoryOrder::relaxed);
        if(tryTelemetryFirst)
            takeTelemetry();

        if(!m_hasPendingPayload && m_messageCount.load(MemoryOrder::relaxed)){
            MessageType msg = MakeMessageType(BaseType::arena());
            if(tryDequeueMessage(msg)){
                constexpr usize s_MaxMessageTextUnits = (s_MaxLogMessageUploadBytes - sizeof(Timer) - sizeof(Core::Common::LogType::Enum) - sizeof(tchar)) / sizeof(tchar);
                if(Get<2>(msg).size() > s_MaxMessageTextUnits){
                    CaptureDiagnosticEvent("logger_client_payload", "Message exceeded the server upload limit", __FILE__, __LINE__);
                    m_rejectedPayloadCount.fetch_add(1u, MemoryOrder::relaxed);
                    NWB_CERR << "[ERROR] Log client: message exceeded the server upload limit.\n";
                    msg = MakeTuple(Timer{}, Core::Common::LogType::Error, LogString(NWB_TEXT("Logger client dropped an oversized message"), BaseType::arena()));
                }
                const auto builtPayload = BuildMessagePayload(msg, m_pendingPayload);
                if(!builtPayload){
                    const DiagnosticEventText detail = MakeDiagnosticEventText("Message payload admission failed: domain=MessagePayloadError code={}", builtPayload.error());
                    CaptureDiagnosticEvent("logger_client_payload", detail.view(), __FILE__, __LINE__);
                    m_rejectedPayloadCount.fetch_add(1u, MemoryOrder::relaxed);
                    NWB_CERR << "[ERROR] Log client: message payload could not be encoded.\n";
                    const MessageType fallbackMsg = MakeTuple(Timer{}, Core::Common::LogType::Error, LogString(NWB_TEXT("Logger client dropped an invalid message payload"), BaseType::arena()));
                    if(!BuildMessagePayload(fallbackMsg, m_pendingPayload))
                        return true;
                }
                m_pendingPayloadKind = ClientPayloadKind::Message;
                m_hasPendingPayload = true;
                m_preferTelemetry = true;
            }
        }
        if(!m_hasPendingPayload && !tryTelemetryFirst)
            takeTelemetry();
        if(!m_hasPendingPayload)
            return true;
    }

    auto scheduleRetry = [this](){
        const auto reportUnsent = [this](){
            CaptureDiagnosticEvent("logger_client_delivery", "Logger stopped with an unsent payload", __FILE__, __LINE__);
            NWB_CERR << "[ERROR] Log client: stopped with an unsent payload and "
                     << m_messageCount.load(MemoryOrder::relaxed) << " queued messages and "
                     << m_telemetryCount.load(MemoryOrder::relaxed) << " queued telemetry streams.\n";
            return false;
        };
        if(this->m_exit.load(MemoryOrder::acquire))
            return reportUnsent();

        for(u32 elapsed = 0u; elapsed < m_retryDelayMs; elapsed += __hidden_logger_client::s_RetrySleepMs){
            SleepMS(__hidden_logger_client::s_RetrySleepMs);
            if(this->m_exit.load(MemoryOrder::acquire))
                return reportUnsent();
        }
        m_retryDelayMs = m_retryDelayMs < __hidden_logger_client::s_MaxRetrySleepMs / 2u
            ? m_retryDelayMs * 2u : __hidden_logger_client::s_MaxRetrySleepMs;
        this->m_semaphore.release();
        return true;
    };

    const auto retryCurlFailure = [this, &scheduleRetry](const CURLcode code, const char* operation){
        if(m_lastCurlFailureCode != static_cast<i32>(code)){
            const DiagnosticEventText detail = MakeDiagnosticEventText("Logger {} failed: curl code={} detail={}", operation, static_cast<i32>(code), curl_easy_strerror(code));
            CaptureDiagnosticEvent("logger_client_delivery", detail.view(), __FILE__, __LINE__);
            NWB_CERR << "[ERROR] Log client: " << operation << " failed: " << curl_easy_strerror(code) << ".\n";
            m_lastCurlFailureCode = static_cast<i32>(code);
        }
        return scheduleRetry();
    };

    const NotNull<CURL*> curlHandle = MakeNotNull(static_cast<CURL*>(m_curl));
    CURLcode curlResult;
    if(m_pendingPayload.size() > static_cast<usize>(Limit<curl_off_t>::s_Max)){
        CaptureDiagnosticEvent("logger_client_payload", "Payload exceeds curl's size limit", __FILE__, __LINE__);
        m_rejectedPayloadCount.fetch_add(1u, MemoryOrder::relaxed);
        NWB_CERR << "[ERROR] Log client: payload exceeds the transport size limit.\n";
        m_pendingPayload.clear();
        m_hasPendingPayload = false;
        m_pendingPayloadKind = ClientPayloadKind::Message;
        m_retryDelayMs = __hidden_logger_client::s_RetrySleepMs;
        return true;
    }

    curlResult = curl_easy_setopt(curlHandle.get(), CURLOPT_URL, m_pendingPayloadKind == ClientPayloadKind::Telemetry ? m_telemetryUrl.c_str() : m_messageUrl.c_str());
    if(curlResult != CURLE_OK)
        return retryCurlFailure(curlResult, "setting URL");

    curlResult = curl_easy_setopt(curlHandle.get(), CURLOPT_POSTFIELDS, reinterpret_cast<char*>(m_pendingPayload.data()));
    if(curlResult != CURLE_OK)
        return retryCurlFailure(curlResult, "setting payload");

    curlResult = curl_easy_setopt(curlHandle.get(), CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(m_pendingPayload.size()));
    if(curlResult != CURLE_OK)
        return retryCurlFailure(curlResult, "setting payload size");

    curlResult = curl_easy_perform(curlHandle.get());
    if(curlResult != CURLE_OK)
        return retryCurlFailure(curlResult, "sending payload");
    m_lastCurlFailureCode = 0;

    long responseCode = 0;
    curlResult = curl_easy_getinfo(curlHandle.get(), CURLINFO_RESPONSE_CODE, &responseCode);
    if(curlResult != CURLE_OK)
        return retryCurlFailure(curlResult, "reading HTTP status");
    m_lastCurlFailureCode = 0;
    if(responseCode == 400 || responseCode == 413 || responseCode == 422){
        const DiagnosticEventText detail = MakeDiagnosticEventText("Server rejected a logger payload with HTTP status {}", responseCode);
        CaptureDiagnosticEvent("logger_client_payload", detail.view(), __FILE__, __LINE__);
        m_rejectedPayloadCount.fetch_add(1u, MemoryOrder::relaxed);
        NWB_CERR << "[ERROR] Log client: server rejected a payload with HTTP status " << responseCode << ".\n";
    }
    else if(responseCode < 200 || responseCode >= 300){
        if(m_lastHttpFailureStatus != responseCode){
            const DiagnosticEventText detail = MakeDiagnosticEventText("Logger delivery is waiting after HTTP status {}", responseCode);
            CaptureDiagnosticEvent("logger_client_delivery", detail.view(), __FILE__, __LINE__);
            NWB_CERR << "[ERROR] Log client: delivery is waiting after HTTP status " << responseCode << ".\n";
            m_lastHttpFailureStatus = responseCode;
        }
        return scheduleRetry();
    }

    m_retryDelayMs = __hidden_logger_client::s_RetrySleepMs;
    m_lastHttpFailureStatus = -1;
    m_pendingPayload.clear();
    m_hasPendingPayload = false;
    m_pendingPayloadKind = ClientPayloadKind::Message;
    return true;
}

void Client::enqueue(MessageType&& data){
    this->m_messageQueue.emplace(Move(data));
    m_messageCount.fetch_add(1, MemoryOrder::relaxed);
    this->m_semaphore.release();
}

b8 Client::tryDequeueMessage(MessageType& msg){
    auto dequeued = this->tryDequeue(msg);
    if(dequeued)
        m_messageCount.fetch_sub(1, MemoryOrder::relaxed);
    return dequeued;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ClientStandalone::ClientStandalone()
    : ClientBaseType("NWB::Log::ClientStandalone")
    , m_processedMessageFile(BaseType::arena())
{}

ClientStandalone::~ClientStandalone()noexcept{
    stopWorker();
    m_processedMessageFile.close();
}

b8 ClientStandalone::internalInit(BasicStringView<tchar> logFileNameBase){
    if(logFileNameBase.empty())
        return m_processedMessageFile.openByExecutableName();

    return m_processedMessageFile.open(logFileNameBase);
}

b8 ClientStandalone::internalUpdate(){
    MessageType msg = MakeMessageType(BaseType::arena());
    while(tryDequeue(msg)){
        const LogString formattedMessage = FormatMessageForProcessing(BaseType::arena(), msg);

        auto& stream = MessageTypeWritesToErrorStream(Get<1>(msg)) ? NWB_TCERR : NWB_TCOUT;
        stream << formattedMessage << static_cast<tchar>('\n');
        if(!m_processedMessageFile.writeLine(formattedMessage))
            NWB_TCERR << NWB_TEXT("[ERROR] Log client: failed to write processed message to file.\n");
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

