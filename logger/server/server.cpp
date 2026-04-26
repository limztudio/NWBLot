// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "pch.h"
#include "server.h"

#include "frame.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_logger_server{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ConnectionInfo{
    ConnectionInfo() = default;
    ConnectionInfo(ConnectionInfo&&) = delete;
    ConnectionInfo(const ConnectionInfo&) = delete;
    ConnectionInfo& operator=(ConnectionInfo&&) = delete;
    ConnectionInfo& operator=(const ConnectionInfo&) = delete;
    ~ConnectionInfo(){
        Core::Alloc::CoreFree(buffer, "ConnectionInfo buffer freed at Server::requestCallback");
    }

    [[nodiscard]] bool append(NotNull<const char*> uploadData, const usize appendSize){
        if(size > Limit<usize>::s_Max - appendSize)
            return false;

        auto* newBuffer = reinterpret_cast<u8*>(Core::Alloc::CoreRealloc(
            buffer,
            size + appendSize,
            "ConnectionInfo buffer reallocated at Server::requestCallback"
        ));
        if(!newBuffer)
            return false;

        buffer = newBuffer;
        NWB_MEMCPY(buffer + size, appendSize, uploadData.get(), appendSize);
        size += appendSize;
        return true;
    }

    u8* buffer = nullptr;
    usize size = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static ConnectionInfo* CreateConnectionInfo(){
    void* memory = Core::Alloc::CoreAlloc(sizeof(ConnectionInfo), "ConnectionInfo allocated at Server::requestCallback");
    if(!memory)
        return nullptr;

    return new(memory) ConnectionInfo();
}

static void DestroyConnectionInfo(ConnectionInfo*& info, void*& conCls)noexcept{
    if(info){
        info->~ConnectionInfo();
        Core::Alloc::CoreFree(info, "ConnectionInfo freed at Server::requestCallback");
    }

    info = nullptr;
    conCls = nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace LoggerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Server* g_Logger = nullptr;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


MHD_Result Server::requestCallback(void* cls, MHD_Connection* connection, const char* url, const char* method, const char* version, const char* upload_data, size_t* upload_data_size, void** con_cls){
    static_cast<void>(url);
    static_cast<void>(version);

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
        auto* info = __hidden_logger_server::CreateConnectionInfo();
        if(!info){
            thisPtr->enqueue(StringFormat(NWB_TEXT("Failed to allocate on {}"), SERVER_NAME), Type::Fatal);
            return MHD_NO;
        }

        conCls = info;
        return MHD_YES;
    }

    auto* info = static_cast<__hidden_logger_server::ConnectionInfo*>(conCls);

    if(uploadDataSize){
        if(!upload_data){
            thisPtr->enqueue(StringFormat(NWB_TEXT("Received a malformed upload chunk on {}"), SERVER_NAME), Type::Error);
            __hidden_logger_server::DestroyConnectionInfo(info, conCls);
            return MHD_NO;
        }

        const auto uploadDataPtr = MakeNotNull(upload_data);
        if(uploadDataSize > static_cast<size_t>(Limit<usize>::s_Max) || info->size > Limit<usize>::s_Max - static_cast<usize>(uploadDataSize)){
            thisPtr->enqueue(StringFormat(NWB_TEXT("Received an oversized message on {}"), SERVER_NAME), Type::Error);
            __hidden_logger_server::DestroyConnectionInfo(info, conCls);
            return MHD_NO;
        }

        const usize appendSize = static_cast<usize>(uploadDataSize);
        if(!info->append(uploadDataPtr, appendSize)){
            thisPtr->enqueue(StringFormat(NWB_TEXT("Failed to reallocate a buffer on {}"), SERVER_NAME), Type::Fatal);
            __hidden_logger_server::DestroyConnectionInfo(info, conCls);
            return MHD_NO;
        }

        uploadDataSize = 0;
        return MHD_YES;
    }
    else{
        receivedCallback(info->buffer, info->size);

        char nullStr[] = "";
        auto* response = MHD_create_response_from_buffer(0, nullStr, MHD_RESPMEM_PERSISTENT);
        if(!response){
            thisPtr->enqueue(StringFormat(NWB_TEXT("Failed to create a response on {}"), SERVER_NAME), Type::Fatal);
            __hidden_logger_server::DestroyConnectionInfo(info, conCls);
            return MHD_NO;
        }

        const auto ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);

        __hidden_logger_server::DestroyConnectionInfo(info, conCls);

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

