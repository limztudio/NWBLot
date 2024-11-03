// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <chrono>

#include <logger/global.h>

#include <curl/curl.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Server{
private:
    using MessageType = std::tuple<std::chrono::system_clock::time_point, std::basic_string<tchar>>;
    using MessageQueue = ParallelQueue<MessageType>;


private:
    constexpr static u64 RenewIntervalMS = 100;


private:
    static bool globalInit();
    static size_t receiveCallback(void* contents, size_t size, size_t nmemb, Server* _this);


public:
    Server();
    ~Server();


public:
    bool init(const char* url);

    bool update();


private:
    void enqueue(std::basic_string<tchar>&& str);
    void enqueue(const std::basic_string<tchar>& str);


private:
    CURL* m_curl;
    MessageQueue m_msgQueue;


private:
    static bool m_globalInit;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

