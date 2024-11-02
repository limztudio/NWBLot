// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "server.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Server::m_globalInit = false;


Server::Server()
    : m_curl(nullptr)
{}
Server::~Server(){
    if(m_curl){
        curl_easy_cleanup(m_curl);
        m_curl = nullptr;
    }
}

bool Server::globalInit(){
    CURLcode ret;

    ret = curl_global_init(CURL_GLOBAL_ALL);
    if(ret != CURLE_OK)
        return false;

    return true;
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



    return true;
}

bool Server::update(){
    for(;; std::this_thread::sleep_for(std::chrono::milliseconds(RenewIntervalMS))){
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

