// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <microhttpd.h>

#include <logger/common.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr tchar SERVER_NAME[] = NWB_TEXT("Server");
class Server : public BaseUpdateOrdinary<Server, 0.1f, SERVER_NAME>{
    friend class Base;
    friend class BaseUpdateOrdinary;


private:
    static MHD_Result requestCallback(void* cls, MHD_Connection* connection, const char* url, const char* method, const char* version, const char* upload_data, size_t* upload_data_size, void** con_cls);


public:
    Server();
    virtual ~Server()override;


public:
    inline void enqueue(TString&& str, Type type = Type::Info){ return Base::enqueue(Move(str), type); }
    inline void enqueue(const TString& str, Type type = Type::Info){ return Base::enqueue(str, type); }


protected:
    bool internalInit(u16 port);
    bool internalUpdate();

protected:
    inline void enqueue(MessageType&& data){ return BaseUpdateOrdinary::enqueue(Move(data)); }
    inline void enqueue(const MessageType& data){ return BaseUpdateOrdinary::enqueue(data); }

    inline bool tryDequeue(MessageType& msg){ return BaseUpdateOrdinary::tryDequeue(msg); }


private:
    MHD_Daemon* m_daemon;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_logger{
    extern Server* g_logger;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_LOGGER_REGISTER(inst) NWB::Log::__hidden_logger::g_logger = inst

#define NWB_LOGGER NWB_ASSERT(NWB::Log::__hidden_logger::g_logger != nullptr);(*NWB::Log::__hidden_logger::g_logger)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

