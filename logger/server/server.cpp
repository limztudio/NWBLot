// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "server.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Server::m_globalInit = false;


bool Server::globalInit(){
    CURLcode ret;

    ret = curl_global_init(CURL_GLOBAL_ALL);
    if(ret != CURLE_OK)
        return false;

    return true;
}
size_t Server::receiveCallback(void* contents, size_t size, size_t nmemb, Server* _this){
    std::basic_string<tchar> str(reinterpret_cast<tchar>(contents), (size * nmemb) / sizeof(tchar));
    _this->enqueue(std::move(str));
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

    for(;; std::this_thread::sleep_for(std::chrono::milliseconds(RenewIntervalMS))){
        ret = curl_easy_perform(m_curl);
        if(ret != CURLE_OK)
            enqueue(convert(std::format("Failed to bring message: {}", curl_easy_strerror(ret))));

        MessageType msg;
        if (!m_msgQueue.try_dequeue(msg))
            continue;


    }

    return true;
}


void Server::enqueue(std::basic_string<tchar>&& str){ m_msgQueue.enqueue(std::make_tuple(std::chrono::system_clock::now(), std::move(str))); }
void Server::enqueue(const std::basic_string<tchar>& str){ m_msgQueue.enqueue(std::make_tuple(std::chrono::system_clock::now(), str)); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

