// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "client.h"

#include <curl/curl.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_logger_client{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void BuildPayload(const MessageType& msg, Vector<u8>& outPayload){
    const auto& [time, type, str] = msg;
    const usize strBytes = str.size() * sizeof(tchar);
    const usize payloadSize = sizeof(decltype(time)) + sizeof(decltype(type)) + strBytes + sizeof(tchar);

    outPayload.resize(payloadSize);

    u8* ptr = outPayload.data();
    {
        NWB_MEMCPY(ptr, sizeof(decltype(time)), &time, sizeof(decltype(time)));
        ptr += static_cast<isize>(sizeof(decltype(time)));
    }
    {
        NWB_MEMCPY(ptr, sizeof(decltype(type)), &type, sizeof(decltype(type)));
        ptr += static_cast<isize>(sizeof(decltype(type)));
    }
    {
        if(strBytes){
            NWB_MEMCPY(ptr, strBytes, str.c_str(), strBytes);
            ptr += static_cast<isize>(strBytes);
        }

        constexpr tchar nullTerminator = 0;
        NWB_MEMCPY(ptr, sizeof(nullTerminator), &nullTerminator, sizeof(nullTerminator));
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


bool Client::globalInit(){
    CURLcode ret;

    ret = curl_global_init(CURL_GLOBAL_ALL);
    if(ret != CURLE_OK)
        return false;

    return true;
}


Client::Client()
    : m_curl(nullptr)
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
        enqueue(StringFormat(NWB_TEXT("Failed to initialize CURL on {}"), CLIENT_NAME), Type::Fatal);
        return false;
    }

    CURL* const curlHandle = static_cast<CURL*>(m_curl);
    CURLcode ret;

    ret = curl_easy_setopt(curlHandle, CURLOPT_URL, url.get());
    if(ret != CURLE_OK){
        enqueue(StringFormat(NWB_TEXT("Failed to set URL on {}: {}"), CLIENT_NAME, StringConvert(curl_easy_strerror(ret))), Type::Fatal);
        return false;
    }

    ret = curl_easy_setopt(curlHandle, CURLOPT_POST, 1);
    if(ret != CURLE_OK){
        enqueue(StringFormat(NWB_TEXT("Failed to set post on {}: {}"), CLIENT_NAME, StringConvert(curl_easy_strerror(ret))), Type::Fatal);
        return false;
    }

    return true;
}
bool Client::internalUpdate(){
    if(!m_msgCount.load(std::memory_order_relaxed))
        return true;

    MessageType msg;
    if(!try_dequeue(msg))
        return true;

    Vector<u8> payload;
    __hidden_logger_client::BuildPayload(msg, payload);

    CURL* const curlHandle = static_cast<CURL*>(m_curl);
    CURLcode ret;

    ret = curl_easy_setopt(curlHandle, CURLOPT_POSTFIELDS, reinterpret_cast<char*>(payload.data()));
    if(ret != CURLE_OK){
        enqueue(StringFormat(NWB_TEXT("Failed to set post fields on {}: {}"), CLIENT_NAME, StringConvert(curl_easy_strerror(ret))), Type::Error);
        return true;
    }

    ret = curl_easy_setopt(curlHandle, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(payload.size()));
    if(ret != CURLE_OK){
        enqueue(StringFormat(NWB_TEXT("Failed to set payload size on {}: {}"), CLIENT_NAME, StringConvert(curl_easy_strerror(ret))), Type::Error);
        return true;
    }

    ret = curl_easy_perform(curlHandle);
    if(ret != CURLE_OK){
        enqueue(StringFormat(NWB_TEXT("Failed to perform on {}: {}"), CLIENT_NAME, StringConvert(curl_easy_strerror(ret))), Type::Error);
        return true;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ClientStandalone::globalInit(){
    return true;
}


ClientStandalone::ClientStandalone(){
}
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
    MessageType msg;
    while(tryDequeue(msg)){
        const TString formattedMessage = FormatMessageForProcessing(msg);

        NWB_TCOUT << formattedMessage << static_cast<tchar>('\n');
        m_processedMsgFile.writeLine(formattedMessage);
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
