// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "pch.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "server.h"

#include "frame.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace LoggerDetail{


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

    if(!cls || !connection || !method || !upload_data_size || !con_cls)
        return MHD_NO;

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
        usize sizeLeft = totalSize;

        if(sizeLeft < sizeof(Timer) + sizeof(Type::Enum) + sizeof(tchar)){
            thisPtr->enqueue(StringFormat(NWB_TEXT("Received a truncated message on {}"), SERVER_NAME), Type::Error);
            return;
        }

        Timer time{};
        {
            NWB_MEMCPY(&time, sizeof(decltype(time)), ptr, sizeof(decltype(time)));
            ptr += sizeof(decltype(time));
            sizeLeft -= sizeof(decltype(time));
        }

        Type::Enum type{};
        {
            NWB_MEMCPY(&type, sizeof(decltype(type)), ptr, sizeof(decltype(type)));
            ptr += sizeof(decltype(type));
            sizeLeft -= sizeof(decltype(type));
        }

        if(sizeLeft < sizeof(tchar) || (sizeLeft % sizeof(tchar)) != 0){
            thisPtr->enqueue(StringFormat(NWB_TEXT("Received a malformed message payload on {}"), SERVER_NAME), Type::Error);
            return;
        }

        const auto* msgText = reinterpret_cast<const tchar*>(ptr);
        const usize msgCharCount = sizeLeft / sizeof(tchar);
        if(msgText[msgCharCount - 1] != 0){
            thisPtr->enqueue(StringFormat(NWB_TEXT("Received a non-null-terminated message on {}"), SERVER_NAME), Type::Error);
            return;
        }

        TString strMsg(msgText, msgCharCount - 1);

        thisPtr->enqueue(MakeTuple(Move(time), type, Move(strMsg)));
    };

    if(!conCls){
        auto* info = reinterpret_cast<LoggerDetail::ConnectionInfo*>(Core::Alloc::CoreAlloc(sizeof(LoggerDetail::ConnectionInfo), "ConnectionInfo allocated at Server::requestCallback"));
        if(!info){
            thisPtr->enqueue(StringFormat(NWB_TEXT("Failed to allocate on {}"), SERVER_NAME), Type::Fatal);
            return MHD_NO;
        }
        info->buffer = nullptr;
        info->size = 0;

        conCls = info;
        return MHD_YES;
    }

    auto* info = static_cast<LoggerDetail::ConnectionInfo*>(conCls);
    auto freeConnectionInfo = [&](){
        Core::Alloc::CoreFree(info->buffer, "ConnectionInfo buffer freed at Server::requestCallback");
        Core::Alloc::CoreFree(info, "ConnectionInfo freed at Server::requestCallback");
        info = nullptr;
        conCls = nullptr;
    };

    if(uploadDataSize){
        if(!upload_data){
            thisPtr->enqueue(StringFormat(NWB_TEXT("Received a malformed upload chunk on {}"), SERVER_NAME), Type::Error);
            freeConnectionInfo();
            return MHD_NO;
        }

        const auto uploadDataPtr = MakeNotNull(upload_data);
        if(uploadDataSize > static_cast<size_t>(Limit<usize>::s_Max) || info->size > Limit<usize>::s_Max - static_cast<usize>(uploadDataSize)){
            thisPtr->enqueue(StringFormat(NWB_TEXT("Received an oversized message on {}"), SERVER_NAME), Type::Error);
            freeConnectionInfo();
            return MHD_NO;
        }

        const usize appendSize = static_cast<usize>(uploadDataSize);
        auto* newBuffer = reinterpret_cast<u8*>(Core::Alloc::CoreRealloc(info->buffer, info->size + appendSize, "ConnectionInfo buffer reallocated at Server::requestCallback"));
        if(!newBuffer){
            thisPtr->enqueue(StringFormat(NWB_TEXT("Failed to reallocate a buffer on {}"), SERVER_NAME), Type::Fatal);
            freeConnectionInfo();
            return MHD_NO;
        }

        info->buffer = newBuffer;
        NWB_MEMCPY(info->buffer + info->size, appendSize, uploadDataPtr.get(), appendSize);
        info->size += appendSize;
        uploadDataSize = 0;
        return MHD_YES;
    }
    else{
        receivedCallback(info->buffer, info->size);

        char nullStr[] = "";
        auto* response = MHD_create_response_from_buffer(0, nullStr, MHD_RESPMEM_PERSISTENT);
        if(!response){
            thisPtr->enqueue(StringFormat(NWB_TEXT("Failed to create a response on {}"), SERVER_NAME), Type::Fatal);
            freeConnectionInfo();
            return MHD_NO;
        }

        const auto ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);

        freeConnectionInfo();

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

    stopWorker();
    m_processedMsgFile.close();
}

bool Server::internalInit(u16 port, BasicStringView<tchar> logFileNameBase){
    if(logFileNameBase.empty()){
        if(!m_processedMsgFile.openByExecutableName())
            return false;
    }
    else{
        if(!m_processedMsgFile.open(logFileNameBase))
            return false;
    }

    m_daemon = MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD, port, nullptr, nullptr, &Server::requestCallback, this, MHD_OPTION_END);

    return m_daemon != nullptr;
}
bool Server::internalUpdate(){
    MessageType msg;
    while(tryDequeue(msg)){
        const auto type = Get<1>(msg);
        const TString formattedMessage = FormatMessageForProcessing(msg);

        Frame::print(formattedMessage, type);
        m_processedMsgFile.writeLine(formattedMessage);
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

