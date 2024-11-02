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


private:
    constexpr static u64 RenewIntervalMS = 100;


public:
    Server();
    ~Server();


private:
    static bool globalInit();
public:
    bool init(const char* url);

    bool update();


private:
    void enqueue(std::basic_string<tchar>&& str);
    void enqueue(const std::basic_string<tchar>& str);


private:
    CURL* m_curl;
    ParallelQueue<MessageType> m_msgQueue;


private:
    static bool m_globalInit;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

