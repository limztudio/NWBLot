// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"
#include <logger/message_payload_codec.h>

#include <global/diagnostics.h>
#include <global/text_utils.h>

#include <curl/curl.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_logger_client{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr u32 s_RetrySleepMs = 100u;
static constexpr u32 s_MaxRetrySleepMs = 5000u;
static constexpr long s_HttpBadRequestCode = 400L;
static constexpr long s_HttpPayloadTooLargeCode = 413L;
static constexpr long s_HttpUnprocessableContentCode = 422L;
static constexpr long s_HttpSuccessRangeMin = 200L;
static constexpr long s_HttpSuccessRangeMax = 300L;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////





////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 Client::GlobalInit()noexcept{ return LogHttpTransport::GlobalInit(); }

Client::Client()
    : ClientBaseType("NWB::Log::Client")
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


}

b8 Client::enqueueTelemetry(const void* const bytes, const usize byteCount){
    if(!bytes || byteCount == 0u || byteCount > s_MaxTelemetryUploadBytes){
        m_rejectedPayloadCount.fetch_add(1u, MemoryOrder::relaxed);
        return false;
    }

    LogBytes upload(BaseType::arena());
    upload.resize(byteCount);
    FOUNDATION_MEMCPY(upload.data(), upload.size(), bytes, byteCount);
    m_telemetryQueue.emplace(Move(upload));
    m_telemetryCount.fetch_add(1u, MemoryOrder::relaxed);
    this->m_semaphore.release();
    return true;
}

b8 Client::internalInit(const AStringView url){
    m_messageUrl = url;
    m_telemetryUrl = JoinWithSingleSlash(BaseType::arena(), url, s_TelemetryUploadEndpoint);

    return m_transport.init();
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
                    FOUNDATION_CERR << "[ERROR] Log client: message exceeded the server upload limit.\n";
                    msg = MakeTuple(Timer{}, Core::Common::LogType::Error, LogString(FOUNDATION_TEXT("Logger client dropped an oversized message"), BaseType::arena()));
                }
                const auto builtPayload = MessagePayloadCodec::Build(msg, m_pendingPayload);
                if(!builtPayload){
                    const DiagnosticEventText detail = MakeDiagnosticEventText("Message payload admission failed: domain=MessagePayloadError code={}", builtPayload.error());
                    CaptureDiagnosticEvent("logger_client_payload", detail.view(), __FILE__, __LINE__);
                    m_rejectedPayloadCount.fetch_add(1u, MemoryOrder::relaxed);
                    FOUNDATION_CERR << "[ERROR] Log client: message payload could not be encoded.\n";
                    const MessageType fallbackMsg = MakeTuple(Timer{}, Core::Common::LogType::Error, LogString(FOUNDATION_TEXT("Logger client dropped an invalid message payload"), BaseType::arena()));
                    if(!MessagePayloadCodec::Build(fallbackMsg, m_pendingPayload))
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
            FOUNDATION_CERR << "[ERROR] Log client: stopped with an unsent payload and "
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

    const auto retryCurlFailure = [this, &scheduleRetry](const CURLcode code, const AStringView operation){
        if(m_lastCurlFailureCode != static_cast<i32>(code)){
            const DiagnosticEventText detail = MakeDiagnosticEventText("Logger {} failed: curl code={} detail={}", operation, static_cast<i32>(code), curl_easy_strerror(code));
            CaptureDiagnosticEvent("logger_client_delivery", detail.view(), __FILE__, __LINE__);
            FOUNDATION_CERR << "[ERROR] Log client: " << operation << " failed: " << curl_easy_strerror(code) << ".\n";
            m_lastCurlFailureCode = static_cast<i32>(code);
        }
        return scheduleRetry();
    };

    if(m_pendingPayload.size() > static_cast<usize>(Limit<curl_off_t>::s_Max)){
        CaptureDiagnosticEvent("logger_client_payload", "Payload exceeds curl's size limit", __FILE__, __LINE__);
        m_rejectedPayloadCount.fetch_add(1u, MemoryOrder::relaxed);
        FOUNDATION_CERR << "[ERROR] Log client: payload exceeds the transport size limit.\n";
        m_pendingPayload.clear();
        m_hasPendingPayload = false;
        m_pendingPayloadKind = ClientPayloadKind::Message;
        m_retryDelayMs = __hidden_logger_client::s_RetrySleepMs;
        return true;
    }

    const auto result = m_transport.send(m_pendingPayloadKind == ClientPayloadKind::Telemetry ? m_telemetryUrl.c_str() : m_messageUrl.c_str(), m_pendingPayload.data(), m_pendingPayload.size());
    if(result.transferCompleted)
        m_lastCurlFailureCode = 0;
    if(result.code != 0)
        return retryCurlFailure(static_cast<CURLcode>(result.code), result.operation);
    m_lastCurlFailureCode = 0;
    const long responseCode = result.responseCode;
    if(responseCode == __hidden_logger_client::s_HttpBadRequestCode || responseCode == __hidden_logger_client::s_HttpPayloadTooLargeCode || responseCode == __hidden_logger_client::s_HttpUnprocessableContentCode){
        const DiagnosticEventText detail = MakeDiagnosticEventText("Server rejected a logger payload with HTTP status {}", responseCode);
        CaptureDiagnosticEvent("logger_client_payload", detail.view(), __FILE__, __LINE__);
        m_rejectedPayloadCount.fetch_add(1u, MemoryOrder::relaxed);
        FOUNDATION_CERR << "[ERROR] Log client: server rejected a payload with HTTP status " << responseCode << ".\n";
    }
    else if(responseCode < __hidden_logger_client::s_HttpSuccessRangeMin || responseCode >= __hidden_logger_client::s_HttpSuccessRangeMax){
        if(m_lastHttpFailureStatus != responseCode){
            const DiagnosticEventText detail = MakeDiagnosticEventText("Logger delivery is waiting after HTTP status {}", responseCode);
            CaptureDiagnosticEvent("logger_client_delivery", detail.view(), __FILE__, __LINE__);
            FOUNDATION_CERR << "[ERROR] Log client: delivery is waiting after HTTP status " << responseCode << ".\n";
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

        auto& stream = MessageTypeWritesToErrorStream(Get<1>(msg)) ? FOUNDATION_TCERR : FOUNDATION_TCOUT;
        stream << formattedMessage << static_cast<tchar>('\n');
        if(!m_processedMessageFile.writeLine(formattedMessage))
            FOUNDATION_TCERR << FOUNDATION_TEXT("[ERROR] Log client: failed to write processed message to file.\n");
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

