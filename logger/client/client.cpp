// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "client.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Client::m_globalInit = false;


bool Client::globalInit(){
    CURLcode ret;

    ret = curl_global_init(CURL_GLOBAL_ALL);
    if(ret != CURLE_OK)
        return false;

    return true;
}


Client::Client()
    : m_curl(nullptr)
{}
Client::~Client(){
    if(m_curl){
        curl_easy_cleanup(m_curl);
        m_curl = nullptr;
    }
}

bool Client::init(const char* url){
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

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

