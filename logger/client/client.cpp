// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "client.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Client::s_SendSwitch = false;


bool Client::globalInit(){
    CURLcode ret;

    ret = curl_global_init(CURL_GLOBAL_ALL);
    if(ret != CURLE_OK)
        return false;

    return true;
}
usize Client::sendCallback(void* contents, usize size, usize nmemb, Client* _this){
    (void)size;
    (void)nmemb;

    const auto thisPtr = MakeNotNull(_this);
    const auto contentsPtr = MakeNotNull(contents);

    if(s_SendSwitch){
        s_SendSwitch = false;
        return 0;
    }

    MessageType msg;
    if(!thisPtr->try_dequeue(msg))
        return 0;

    auto* ptr = reinterpret_cast<u8*>(contentsPtr.get());

    const auto& [time, type, str] = msg;
    {
        NWB_MEMCPY(ptr, sizeof(decltype(time)), &time, sizeof(decltype(time)));
        ptr += static_cast<isize>(sizeof(decltype(time)));
    }
    {
        NWB_MEMCPY(ptr, sizeof(decltype(type)), &type, sizeof(decltype(type)));
        ptr += static_cast<isize>(sizeof(decltype(type)));
    }
    {
        NWB_MEMCPY(ptr, str.size() * sizeof(tchar), str.c_str(), str.size() * sizeof(tchar));
        ptr += static_cast<isize>(str.size() * sizeof(tchar));

        (*reinterpret_cast<tchar*>(ptr)) = 0;
        ptr += sizeof(tchar);
    }

    auto sizeWritten = static_cast<usize>(ptr - reinterpret_cast<u8*>(contentsPtr.get()));

    s_SendSwitch = true;
    return sizeWritten;
}


Client::Client()
    : m_curl(nullptr)
    , m_msgCount(0)
{}
Client::~Client(){
    if(m_curl){
        curl_easy_cleanup(m_curl);
        m_curl = nullptr;
    }
}

bool Client::internalInit(NotNull<const char*> url){
    m_curl = curl_easy_init();
    if(!m_curl){
        enqueue(StringFormat(NWB_TEXT("Failed to initialize CURL on {}"), CLIENT_NAME), Type::Fatal);
        return false;
    }

    CURLcode ret;

    ret = curl_easy_setopt(m_curl, CURLOPT_URL, url.get());
    if(ret != CURLE_OK){
        enqueue(StringFormat(NWB_TEXT("Failed to set URL on {}: {}"), CLIENT_NAME, StringConvert(curl_easy_strerror(ret))), Type::Fatal);
        return false;
    }

    ret = curl_easy_setopt(m_curl, CURLOPT_READFUNCTION, sendCallback);
    if(ret != CURLE_OK){
        enqueue(StringFormat(NWB_TEXT("Failed to set write callback on {}: {}"), CLIENT_NAME, StringConvert(curl_easy_strerror(ret))), Type::Fatal);
        return false;
    }

    ret = curl_easy_setopt(m_curl, CURLOPT_READDATA, this);
    if(ret != CURLE_OK){
        enqueue(StringFormat(NWB_TEXT("Failed to set write data on {}: {}"), CLIENT_NAME, StringConvert(curl_easy_strerror(ret))), Type::Fatal);
        return false;
    }

    ret = curl_easy_setopt(m_curl, CURLOPT_POST, 1);
    if(ret != CURLE_OK){
        enqueue(StringFormat(NWB_TEXT("Failed to set post on {}: {}"), CLIENT_NAME, StringConvert(curl_easy_strerror(ret))), Type::Fatal);
        return false;
    }

    ret = curl_easy_setopt(m_curl, CURLOPT_POSTFIELDSIZE, -1);
    if(ret != CURLE_OK){
        enqueue(StringFormat(NWB_TEXT("Failed to set file size on {}: {}"), CLIENT_NAME, StringConvert(curl_easy_strerror(ret))), Type::Fatal);
        return false;
    }

    return true;
}
bool Client::internalUpdate(){
    if(!m_msgCount.load(MemoryOrder::memory_order_acquire))
        return true;

    CURLcode ret;

    ret = curl_easy_perform(m_curl);
    if(ret != CURLE_OK){
        enqueue(StringFormat(NWB_TEXT("Failed to perform on {}: {}"), CLIENT_NAME, StringConvert(curl_easy_strerror(ret))), Type::Error);
        return true;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

