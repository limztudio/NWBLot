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


bool Server::m_globalInit = false;


bool Server::globalInit(){
    CURLcode ret;

    ret = curl_global_init(CURL_GLOBAL_ALL);
    if(ret != CURLE_OK)
        return false;

    return true;
}
usize Server::receiveCallback(void* contents, usize size, usize nmemb, Server* _this){
    auto* ptr = reinterpret_cast<u8*>(contents);
    auto sizeLeft = static_cast<isize>(size * nmemb);

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
    std::basic_string<tchar> strMsg(reinterpret_cast<tchar>(ptr), static_cast<usize>(sizeLeft) / sizeof(tchar));

    _this->m_msgQueue.enqueue(std::make_tuple(std::move(time), type, std::move(strMsg)));
}


Server::Server()
    : m_curl(nullptr)
{}
Server::~Server(){
    if(m_curl){
        curl_easy_cleanup(m_curl);
        m_curl = nullptr;
    }
}

bool Server::init(const char* url){
    if(!m_globalInit){
        if(!globalInit()){
            enqueue(NWB_TEXT("Failed to global initialization on Server"));
            return false;
        }
        m_globalInit = true;
    }

    m_curl = curl_easy_init();
    if(!m_curl){
        enqueue(NWB_TEXT("Failed to initialize CURL on Server"));
        return false;
    }

    CURLcode ret;

    ret = curl_easy_setopt(m_curl, CURLOPT_URL, url);
    if(ret != CURLE_OK){
        enqueue(convert(std::format("Failed to set URL on Server: {}", curl_easy_strerror(ret))));
        return false;
    }

    ret = curl_easy_setopt(m_curl, CURLOPT_WRITEFUNCTION, receiveCallback);
    if(ret != CURLE_OK){
        enqueue(convert(std::format("Failed to set write callback: {}", curl_easy_strerror(ret))));
        return false;
    }

    ret = curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, this);
    if(ret != CURLE_OK){
        enqueue(convert(std::format("Failed to set write data: {}", curl_easy_strerror(ret))));
        return false;
    }

    return true;
}

bool Server::update(){
    CURLcode ret;

    ret = curl_easy_perform(m_curl);
    if(ret != CURLE_OK)
        enqueue(convert(std::format("Failed to bring message: {}", curl_easy_strerror(ret))));

    MessageType msg;
    while(m_msgQueue.try_dequeue(msg)){
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


void Server::enqueue(std::basic_string<tchar>&& str, Type type){ m_msgQueue.enqueue(std::make_tuple(std::chrono::system_clock::now(), type, std::move(str))); }
void Server::enqueue(const std::basic_string<tchar>& str, Type type){ m_msgQueue.enqueue(std::make_tuple(std::chrono::system_clock::now(), type, str)); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef _COUT
#undef _CIN
#undef _CERR


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

