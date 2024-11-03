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
public:
    enum class Type : u8{
        Info,
        Warning,
        Error,
        Fatal,
    };

private:
    using MessageType = std::tuple<std::chrono::system_clock::time_point, Type, std::basic_string<tchar>>;
    using MessageQueue = ParallelQueue<MessageType>;


private:
    constexpr static u64 RenewIntervalMS = 30;


private:
    static bool globalInit();
    static usize receiveCallback(void* contents, usize size, usize nmemb, Server* _this);


public:
    Server();
    ~Server();


public:
    bool init(const char* url);

    bool update();


private:
    void enqueue(std::basic_string<tchar>&& str, Type type = Type::Info);
    void enqueue(const std::basic_string<tchar>& str, Type type = Type::Info);


private:
    CURL* m_curl;
    MessageQueue m_msgQueue;


private:
    static bool m_globalInit;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

