// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <iostream>

#include "server.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


#ifdef NWB_UNICODE
#define _COUT std::wcout
#define _CIN std::wcin
#define _CERR std::wcerr
#else
#define _COUT std::cout
#define _CIN std::cin
#define _CERR std::cerr
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


usize Server::receiveCallback(void* contents, usize size, usize nmemb, Server* _this){
    auto totalSize = size * nmemb;
    if(!totalSize)
        return 0;

    auto* ptr = reinterpret_cast<u8*>(contents);
    auto sizeLeft = static_cast<isize>(totalSize);

    std::chrono::system_clock::time_point time;
    {
        NWB_MEMCPY(&time, sizeof(decltype(time)), ptr, sizeLeft);
        ptr += sizeof(decltype(time));
        sizeLeft -= sizeof(decltype(time));
    }

    Type type;
    {
        NWB_MEMCPY(&type, sizeof(decltype(type)), ptr, sizeLeft);
        ptr += sizeof(decltype(type));
        sizeLeft -= sizeof(decltype(type));
    }

    assert(sizeLeft > 0);
    std::basic_string<tchar> strMsg(reinterpret_cast<tchar*>(ptr), static_cast<usize>(sizeLeft) / sizeof(tchar));

    _this->enqueue(std::make_tuple(std::move(time), type, std::move(strMsg)));
    return totalSize;
}


Server::Server(){}

bool Server::internalInit(const char* url){
    CURLcode ret;

    ret = curl_easy_setopt(m_curl, CURLOPT_URL, url);
    if(ret != CURLE_OK){
        enqueue(std::format(NWB_TEXT("Failed to set URL on {}: {}"), SERVER_NAME, convert(curl_easy_strerror(ret))), Type::Fatal);
        return false;
    }

    ret = curl_easy_setopt(m_curl, CURLOPT_WRITEFUNCTION, receiveCallback);
    if(ret != CURLE_OK){
        enqueue(std::format(NWB_TEXT("Failed to set write callback on {}: {}"), SERVER_NAME, convert(curl_easy_strerror(ret))), Type::Fatal);
        return false;
    }

    ret = curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, this);
    if(ret != CURLE_OK){
        enqueue(std::format(NWB_TEXT("Failed to set write data on {}: {}"), SERVER_NAME, convert(curl_easy_strerror(ret))), Type::Fatal);
        return false;
    }

    return true;
}
bool Server::internalUpdate(){
    CURLcode ret;

    ret = curl_easy_perform(m_curl);
    if(ret != CURLE_OK)
        enqueue(std::format(NWB_TEXT("Failed to bring message on {}: {}"), SERVER_NAME, convert(curl_easy_strerror(ret))), Type::Error);

    MessageType msg;
    while(try_dequeue(msg)){
        const auto& [time, type, str] = msg;
        switch(type){
        case Type::Info:
            _COUT << std::format(NWB_TEXT("{} [INFO]: {}"), std::chrono::system_clock::to_time_t(time), str) << std::endl;
            break;
        case Type::Warning:
            _COUT << std::format(NWB_TEXT("{} [WARNING]: {}"), std::chrono::system_clock::to_time_t(time), str) << std::endl;
            break;
        case Type::Error:
            _COUT << std::format(NWB_TEXT("{} [ERROR]: {}"), std::chrono::system_clock::to_time_t(time), str) << std::endl;
            break;
        case Type::Fatal:
            _COUT << std::format(NWB_TEXT("{} [FATAL]: {}"), std::chrono::system_clock::to_time_t(time), str) << std::endl;
            break;
        }
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef _COUT
#undef _CIN
#undef _CERR


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

