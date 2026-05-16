// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/alloc/standalone_runtime.h>
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
        if(appendSize == 0u)
            return true;

        if(size > Limit<usize>::s_Max - appendSize)
            return false;

        const usize requiredSize = size + appendSize;
        if(!reserve(requiredSize))
            return false;

        NWB_MEMCPY(buffer + size, appendSize, uploadData.get(), appendSize);
        size = requiredSize;
        return true;
    }

    [[nodiscard]] bool reserve(const usize requiredSize){
        if(requiredSize <= capacity)
            return true;

        constexpr usize s_InitialBufferCapacity = 256u;
        usize newCapacity = capacity > 0u ? capacity : s_InitialBufferCapacity;
        while(newCapacity < requiredSize){
            if(newCapacity > Limit<usize>::s_Max / 2u){
                newCapacity = requiredSize;
                break;
            }
            newCapacity *= 2u;
        }

        auto* newBuffer = reinterpret_cast<u8*>(Core::Alloc::CoreRealloc(
            buffer,
            newCapacity,
            "ConnectionInfo buffer reallocated at Server::requestCallback"
        ));
        if(!newBuffer)
            return false;

        buffer = newBuffer;
        capacity = newCapacity;
        return true;
    }

    u8* buffer = nullptr;
    usize size = 0u;
    usize capacity = 0u;
};

static void DestroyConnectionInfo(ConnectionInfo*& info, void*& conCls)noexcept;

static void EnqueueServerMessage(Server& server, const tchar* message, const Type::Enum type){
    server.enqueue(StringFormat(NWB_TEXT("{} on {}"), message, SERVER_NAME), type);
}

[[nodiscard]] MHD_Result QueueEmptyResponse(Server& server, MHD_Connection& connection){
    static char s_EmptyResponse[] = "";

    auto* response = MHD_create_response_from_buffer(0, s_EmptyResponse, MHD_RESPMEM_PERSISTENT);
    if(!response){
        EnqueueServerMessage(server, NWB_TEXT("Failed to create a response"), Type::Fatal);
        return MHD_NO;
    }

    const auto ret = MHD_queue_response(&connection, MHD_HTTP_OK, response);
    MHD_destroy_response(response);
    return ret;
}

[[nodiscard]] MHD_Result FinishConnectionUpload(
    Server& server,
    MHD_Connection& connection,
    ConnectionInfo*& info,
    void*& conCls
){
    const MHD_Result ret = QueueEmptyResponse(server, connection);
    DestroyConnectionInfo(info, conCls);
    return ret;
}


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


namespace ServerLoggerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Server* g_ServerLogger = nullptr;


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

    if(!conCls){
        auto* info = __hidden_logger_server::CreateConnectionInfo();
        if(!info){
            __hidden_logger_server::EnqueueServerMessage(*thisPtr, NWB_TEXT("Failed to allocate"), Type::Fatal);
            return MHD_NO;
        }

        conCls = info;
        return MHD_YES;
    }

    auto* info = static_cast<__hidden_logger_server::ConnectionInfo*>(conCls);

    if(uploadDataSize){
        if(!upload_data){
            __hidden_logger_server::EnqueueServerMessage(*thisPtr, NWB_TEXT("Received a malformed upload chunk"), Type::Error);
            __hidden_logger_server::DestroyConnectionInfo(info, conCls);
            return MHD_NO;
        }

        const auto uploadDataPtr = MakeNotNull(upload_data);
        if(uploadDataSize > static_cast<size_t>(Limit<usize>::s_Max) || info->size > Limit<usize>::s_Max - static_cast<usize>(uploadDataSize)){
            __hidden_logger_server::EnqueueServerMessage(*thisPtr, NWB_TEXT("Received an oversized message"), Type::Error);
            __hidden_logger_server::DestroyConnectionInfo(info, conCls);
            return MHD_NO;
        }

        const usize appendSize = static_cast<usize>(uploadDataSize);
        if(!info->append(uploadDataPtr, appendSize)){
            __hidden_logger_server::EnqueueServerMessage(*thisPtr, NWB_TEXT("Failed to reallocate a buffer"), Type::Fatal);
            __hidden_logger_server::DestroyConnectionInfo(info, conCls);
            return MHD_NO;
        }

        uploadDataSize = 0;
        return MHD_YES;
    }

    MessageType message;
    const tchar* error = nullptr;
    if(ParseMessagePayload(info->buffer, info->size, message, error))
        thisPtr->enqueue(Move(message));
    else{
        __hidden_logger_server::EnqueueServerMessage(
            *thisPtr,
            error ? error : NWB_TEXT("Received a malformed message"),
            Type::Error
        );
    }

    return __hidden_logger_server::FinishConnectionUpload(*thisPtr, *connection, info, conCls);
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

