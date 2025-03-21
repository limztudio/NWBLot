// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "pch.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "server.h"

#include "frame.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


usize Server::receiveCallback(void* contents, usize size, usize nmemb, Server* _this){
    auto totalSize = size * nmemb;
    if(!totalSize)
        return 0;

    auto* ptr = reinterpret_cast<u8*>(contents);
    auto sizeLeft = static_cast<isize>(totalSize);

    Timer time;
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
    TString strMsg(reinterpret_cast<tchar*>(ptr), static_cast<usize>(sizeLeft) / sizeof(tchar));

    _this->enqueue(MakeTuple(Move(time), type, Move(strMsg)));
    return totalSize;
}


Server::Server(){}

bool Server::internalInit(const char* url){
    CURLcode ret;

    ret = curl_easy_setopt(m_curl, CURLOPT_URL, url);
    if(ret != CURLE_OK){
        enqueue(stringFormat(NWB_TEXT("Failed to set URL on {}: {}"), SERVER_NAME, stringConvert(curl_easy_strerror(ret))), Type::Fatal);
        return false;
    }

    ret = curl_easy_setopt(m_curl, CURLOPT_WRITEFUNCTION, receiveCallback);
    if(ret != CURLE_OK){
        enqueue(stringFormat(NWB_TEXT("Failed to set write callback on {}: {}"), SERVER_NAME, stringConvert(curl_easy_strerror(ret))), Type::Fatal);
        return false;
    }

    ret = curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, this);
    if(ret != CURLE_OK){
        enqueue(stringFormat(NWB_TEXT("Failed to set write data on {}: {}"), SERVER_NAME, stringConvert(curl_easy_strerror(ret))), Type::Fatal);
        return false;
    }

    return true;
}
bool Server::internalUpdate(){
    CURLcode ret;

    ret = curl_easy_perform(m_curl);
    if(ret != CURLE_OK)
        enqueue(stringFormat(NWB_TEXT("Failed to bring message on {}: {}"), SERVER_NAME, stringConvert(curl_easy_strerror(ret))), Type::Error);

    MessageType msg;
    while(try_dequeue(msg)){
        const auto& [time, type, str] = msg;
        switch(type){
        case Type::Info:
            Frame::print(stringFormat(NWB_TEXT("{} [INFO]:\n{}"), durationInTimeDelta(time), str));
            break;
        case Type::Warning:
            Frame::print(stringFormat(NWB_TEXT("{} [WARNING]:\n{}"), durationInTimeDelta(time), str));
            break;
        case Type::Error:
            Frame::print(stringFormat(NWB_TEXT("{} [ERROR]:\n{}"), durationInTimeDelta(time), str));
            break;
        case Type::Fatal:
            Frame::print(stringFormat(NWB_TEXT("{} [FATAL]:\n{}"), durationInTimeDelta(time), str));
            break;
        }
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

