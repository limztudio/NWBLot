// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "client.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


usize Client::sendCallback(void* contents, usize size, usize nmemb, Client* _this){
    (void)size;
    (void)nmemb;

    MessageType msg;
    if(!_this->try_dequeue(msg))
        return 0;

    auto* ptr = reinterpret_cast<u8*>(contents);

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
    }

    auto sizeWritten = static_cast<usize>(ptr - reinterpret_cast<u8*>(contents));
    return sizeWritten;
}


Client::Client()
    : m_msgCount(0)
{}

bool Client::internalInit(const char* url){
    CURLcode ret;

    ret = curl_easy_setopt(m_curl, CURLOPT_URL, url);
    if(ret != CURLE_OK){
        enqueue(stringFormat(NWB_TEXT("Failed to set URL on {}: {}"), CLIENT_NAME, stringConvert(curl_easy_strerror(ret))), Type::Fatal);
        return false;
    }

    ret = curl_easy_setopt(m_curl, CURLOPT_WRITEFUNCTION, sendCallback);
    if(ret != CURLE_OK){
        enqueue(stringFormat(NWB_TEXT("Failed to set write callback on {}: {}"), CLIENT_NAME, stringConvert(curl_easy_strerror(ret))), Type::Fatal);
        return false;
    }

    ret = curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, this);
    if(ret != CURLE_OK){
        enqueue(stringFormat(NWB_TEXT("Failed to set write data on {}: {}"), CLIENT_NAME, stringConvert(curl_easy_strerror(ret))), Type::Fatal);
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
        enqueue(stringFormat(NWB_TEXT("Failed to perform on {}: {}"), CLIENT_NAME, stringConvert(curl_easy_strerror(ret))), Type::Error);
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

