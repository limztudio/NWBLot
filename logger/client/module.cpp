// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"

#include <curl/curl.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


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
    , m_hasPendingPayload(false)
    , m_msgCount(0)
{}
Client::~Client(){
    stopWorker();

    if(m_curl){
        curl_easy_cleanup(static_cast<CURL*>(m_curl));
        m_curl = nullptr;
    }
}

bool Client::internalInit(NotNull<const char*> url){
    m_curl = curl_easy_init();
    if(!m_curl){
        enqueue(StringFormat(BaseType::arena(), NWB_TEXT("Failed to initialize CURL on {}"), CLIENT_NAME), Type::Fatal);
        return false;
    }

    CURL* const curlHandle = static_cast<CURL*>(m_curl);
    CURLcode ret;

    ret = curl_easy_setopt(curlHandle, CURLOPT_URL, url.get());
    if(ret != CURLE_OK){
        enqueue(StringFormat(BaseType::arena(), NWB_TEXT("Failed to set URL on {}: {}"), CLIENT_NAME, StringConvert(BaseType::arena(), curl_easy_strerror(ret))), Type::Fatal);
        return false;
    }

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

    ret = curl_easy_setopt(curlHandle, CURLOPT_CONNECTTIMEOUT_MS, 1000L);
    if(ret != CURLE_OK){
        enqueue(StringFormat(BaseType::arena(), NWB_TEXT("Failed to set connect timeout on {}: {}"), CLIENT_NAME, StringConvert(BaseType::arena(), curl_easy_strerror(ret))), Type::Fatal);
        return false;
    }

    ret = curl_easy_setopt(curlHandle, CURLOPT_TIMEOUT_MS, 2000L);
    if(ret != CURLE_OK){
        enqueue(StringFormat(BaseType::arena(), NWB_TEXT("Failed to set request timeout on {}: {}"), CLIENT_NAME, StringConvert(BaseType::arena(), curl_easy_strerror(ret))), Type::Fatal);
        return false;
    }

    return true;
}
bool Client::internalUpdate(){
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
        m_hasPendingPayload = true;
    }

    auto scheduleRetry = [this](){
        if(this->m_exit.load(MemoryOrder::acquire))
            return;

        SleepMS(100);
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

