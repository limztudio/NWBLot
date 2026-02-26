// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "pch.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "server.h"

#include "frame.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_logger{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ConnectionInfo{
    u8* buffer;
    usize size;
};

Server* g_Logger = nullptr;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


MHD_Result Server::requestCallback(void* cls, MHD_Connection* connection, const char* url, const char* method, const char* version, const char* upload_data, size_t* upload_data_size, void** con_cls){
    (void)url;
    (void)version;

    const auto methodPtr = MakeNotNull(method);
    if(NWB_STRCMP(methodPtr.get(), "POST") != 0)
        return MHD_NO;

    const auto thisPtr = MakeNotNull(static_cast<Server*>(cls));
    const auto uploadDataSizePtr = MakeNotNull(upload_data_size);
    auto& uploadDataSize = *uploadDataSizePtr;
    const auto conClsPtr = MakeNotNull(con_cls);
    auto& conCls = *conClsPtr;

    auto receivedCallback = [thisPtr](const void* contents, usize totalSize){
        const auto* ptr = reinterpret_cast<const u8*>(contents);
        auto sizeLeft = static_cast<isize>(totalSize);

        Timer time;
        {
            NWB_MEMCPY(&time, sizeof(decltype(time)), ptr, sizeof(decltype(time)));
            ptr += sizeof(decltype(time));
            sizeLeft -= sizeof(decltype(time));
        }

        Type type;
        {
            NWB_MEMCPY(&type, sizeof(decltype(type)), ptr, sizeof(decltype(type)));
            ptr += sizeof(decltype(type));
            sizeLeft -= sizeof(decltype(type));
        }

        NWB_ASSERT(sizeLeft > 0);
        TString strMsg(reinterpret_cast<const tchar*>(ptr));

        thisPtr->enqueue(MakeTuple(Move(time), type, Move(strMsg)));
    };

    if(!conCls){
        auto* info = reinterpret_cast<__hidden_logger::ConnectionInfo*>(Core::Alloc::CoreAlloc(sizeof(__hidden_logger::ConnectionInfo), "ConnectionInfo allocated at Server::requestCallback"));
        if(!info){
            thisPtr->enqueue(StringFormat(NWB_TEXT("Failed to allocate on {}"), SERVER_NAME), Type::Fatal);
            return MHD_NO;
        }
        info->buffer = nullptr;
        info->size = 0;

        conCls = info;
        return MHD_YES;
    }

    auto*& info = reinterpret_cast<__hidden_logger::ConnectionInfo*&>(conCls);

    if(uploadDataSize){
        const auto uploadDataPtr = MakeNotNull(upload_data);
        info->buffer = reinterpret_cast<u8*>(Core::Alloc::CoreRealloc(info->buffer, info->size + uploadDataSize, "ConnectionInfo buffer reallocated at Server::requestCallback"));
        if(!info->buffer){
            thisPtr->enqueue(StringFormat(NWB_TEXT("Failed to reallocate a buffer on {}"), SERVER_NAME), Type::Fatal);
            return MHD_NO;
        }

        NWB_MEMCPY(info->buffer + info->size, uploadDataSize, uploadDataPtr.get(), uploadDataSize);
        info->size += uploadDataSize;
        uploadDataSize = 0;
        return MHD_YES;
    }
    else{
        receivedCallback(info->buffer, info->size);

        char nullStr[] = "";
        auto* response = MHD_create_response_from_buffer(0, nullStr, MHD_RESPMEM_PERSISTENT);
        auto ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);

        Core::Alloc::CoreFree(info->buffer, "ConnectionInfo buffer freed at Server::requestCallback");
        Core::Alloc::CoreFree(info, "ConnectionInfo freed at Server::requestCallback");
        info = nullptr;

        return ret;
    }
}


Server::Server()
    : m_daemon(nullptr)
{}
Server::~Server(){
    if(m_daemon){
        MHD_stop_daemon(m_daemon);
        m_daemon = nullptr;
    }
}

bool Server::internalInit(u16 port){
    m_daemon = MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD, port, nullptr, nullptr, &Server::requestCallback, this, MHD_OPTION_END);

    return true;
}
bool Server::internalUpdate(){
    MessageType msg;
    while(tryDequeue(msg)){
        const auto& [time, type, str] = msg;
        switch(type){
        case Type::Info:
            Frame::print(StringFormat(NWB_TEXT("{} [INFO]:\n{}"), DurationInTimeDelta(time), str), type);
            break;
        case Type::Warning:
            Frame::print(StringFormat(NWB_TEXT("{} [WARNING]:\n{}"), DurationInTimeDelta(time), str), type);
            break;
        case Type::Error:
            Frame::print(StringFormat(NWB_TEXT("{} [ERROR]:\n{}"), DurationInTimeDelta(time), str), type);
            break;
        case Type::Fatal:
            Frame::print(StringFormat(NWB_TEXT("{} [FATAL]:\n{}"), DurationInTimeDelta(time), str), type);
            break;
        }
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

