// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"

#include "crash_auth.h"
#include "frame.h"

#include <core/alloc/standalone_runtime.h>


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
        static_cast<void>(closeCrashUploadStream());
        Core::Alloc::CoreFree(buffer, "ConnectionInfo buffer freed at Server::requestCallback");
    }

    [[nodiscard]] bool append(NotNull<const char*> uploadData, const usize appendSize){
        if(appendSize == 0u)
            return true;

        if(size > Limit<usize>::s_Max - appendSize)
            return false;

        if(isCrashUpload)
            return appendCrashUpload(uploadData, appendSize);

        const usize requiredSize = size + appendSize;
        if(!reserve(requiredSize))
            return false;

        NWB_MEMCPY(buffer + size, appendSize, uploadData.get(), appendSize);
        size = requiredSize;
        return true;
    }

    [[nodiscard]] bool appendCrashUpload(NotNull<const char*> uploadData, const usize appendSize){
        if(!crashUploadStream.is_open())
            return false;

        crashUploadStream.write(uploadData.get(), static_cast<StreamSize>(appendSize));
        if(!crashUploadStream.good())
            return false;

        size += appendSize;
        return true;
    }

    [[nodiscard]] bool closeCrashUploadStream(){
        if(crashUploadStream.is_open())
            crashUploadStream.close();

        return crashUploadStream.good();
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
    bool isCrashUpload = false;
    char crashUploadPath[1024] = {};
    OutputFileStream crashUploadStream;
};

inline constexpr usize s_MaxLogMessageUploadBytes = 1u * 1024u * 1024u;
inline constexpr usize s_MaxCrashPackageUploadBytes = 128u * 1024u * 1024u;

static void DestroyConnectionInfo(ConnectionInfo*& info, void*& conCls)noexcept;

[[nodiscard]] static usize UploadSizeLimit(const bool isCrashUpload)noexcept{
    return isCrashUpload
        ? s_MaxCrashPackageUploadBytes
        : s_MaxLogMessageUploadBytes
    ;
}

static void EnqueueServerMessage(Server& server, const tchar* message, const Type::Enum type){
    server.enqueue(StringFormat(server.arena(), NWB_TEXT("{} on {}"), message, SERVER_NAME), type);
}

[[nodiscard]] MHD_Result QueueEmptyResponse(Server& server, MHD_Connection& connection, const unsigned int statusCode = MHD_HTTP_OK){
    static char s_EmptyResponse[] = "";

    auto* response = MHD_create_response_from_buffer(0, s_EmptyResponse, MHD_RESPMEM_PERSISTENT);
    if(!response){
        EnqueueServerMessage(server, NWB_TEXT("Failed to create a response"), Type::Fatal);
        return MHD_NO;
    }

    const auto ret = MHD_queue_response(&connection, statusCode, response);
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

[[nodiscard]] static Path CrashInboxDirectory(Server& server){
    Path executableDirectory(server.arena());
    if(GetExecutableDirectory(executableDirectory))
        return executableDirectory / "crashes" / "inbox";

    return Path(server.arena(), "crashes") / "inbox";
}

[[nodiscard]] static Path MakeCrashPackagePath(Server& server){
    static Atomic<u64> s_CrashPackageCounter{ 1u };

    LocalTime localTime = {};
    static_cast<void>(GetLocalTime(localTime));

    const u64 counter = s_CrashPackageCounter.fetch_add(1u, MemoryOrder::relaxed);
    const auto fileName = StringFormat(
        server.arena(),
        "crash_{:04}{:02}{:02}_{:02}{:02}{:02}_{}.nwbcrashpkg",
        localTime.tm_year + 1900,
        localTime.tm_mon + 1,
        localTime.tm_mday,
        localTime.tm_hour,
        localTime.tm_min,
        localTime.tm_sec,
        counter
    );

    return CrashInboxDirectory(server) / fileName;
}

[[nodiscard]] static bool CopyCrashUploadPath(Server& server, const Path& path, char (&outPath)[1024]){
    const AString<LogArena> pathText = PathToString<char>(server.arena(), path);
    if(pathText.size() >= sizeof(outPath))
        return false;

    CopyFixedBuffer(outPath, AStringView(pathText.data(), pathText.size()));
    return true;
}

[[nodiscard]] static bool OpenCrashUploadStream(Server& server, ConnectionInfo& info){
    ErrorCode error;
    const Path inboxDirectory = CrashInboxDirectory(server);
    static_cast<void>(EnsureDirectories(inboxDirectory, error));
    if(error)
        return false;

    const Path path = MakeCrashPackagePath(server);
    if(!CopyCrashUploadPath(server, path, info.crashUploadPath))
        return false;

    info.crashUploadStream.open(path.c_str(), s_FileOpenBinary | s_FileOpenTruncate);
    return info.crashUploadStream.is_open();
}

static void DiscardStoredCrashUpload(Server& server, ConnectionInfo& info){
    if(!info.isCrashUpload || info.crashUploadPath[0] == 0)
        return;

    static_cast<void>(info.closeCrashUploadStream());

    ErrorCode error;
    static_cast<void>(RemoveFile(Path(server.arena(), AStringView(info.crashUploadPath)), error));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static ConnectionInfo* CreateConnectionInfo(Server& server, const bool isCrashUpload){
    void* memory = Core::Alloc::CoreAlloc(sizeof(ConnectionInfo), "ConnectionInfo allocated at Server::requestCallback");
    if(!memory)
        return nullptr;

    auto* info = new(memory) ConnectionInfo();
    info->isCrashUpload = isCrashUpload;
    if(isCrashUpload && !OpenCrashUploadStream(server, *info)){
        info->~ConnectionInfo();
        Core::Alloc::CoreFree(info, "ConnectionInfo freed after crash upload stream init failure");
        return nullptr;
    }
    return info;
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
    static_cast<void>(version);

    if(!cls || !connection || !url || !method || !upload_data_size || !con_cls)
        return MHD_NO;

    const auto methodPtr = MakeNotNull(method);
    if(NWB_STRCMP(methodPtr.get(), "POST") != 0)
        return MHD_NO;

    const auto thisPtr = MakeNotNull(static_cast<Server*>(cls));
    const auto uploadDataSizePtr = MakeNotNull(upload_data_size);
    auto& uploadDataSize = *uploadDataSizePtr;
    const auto conClsPtr = MakeNotNull(con_cls);
    auto& conCls = *conClsPtr;

    const bool isCrashUpload = NWB_STRCMP(url, "/crash") == 0;

    if(!conCls){
        if(isCrashUpload && !thisPtr->crashUploadAuthorized(*connection)){
            __hidden_logger_server::EnqueueServerMessage(*thisPtr, NWB_TEXT("Rejected unauthorized crash upload"), Type::Warning);
            return __hidden_logger_server::QueueEmptyResponse(*thisPtr, *connection, MHD_HTTP_UNAUTHORIZED);
        }

        auto* info = __hidden_logger_server::CreateConnectionInfo(*thisPtr, isCrashUpload);
        if(!info){
            __hidden_logger_server::EnqueueServerMessage(*thisPtr, NWB_TEXT("Failed to initialize connection upload state"), Type::Fatal);
            return MHD_NO;
        }

        conCls = info;
        return MHD_YES;
    }

    auto* info = static_cast<__hidden_logger_server::ConnectionInfo*>(conCls);

    if(uploadDataSize){
        if(!upload_data){
            __hidden_logger_server::EnqueueServerMessage(*thisPtr, NWB_TEXT("Received a malformed upload chunk"), Type::Error);
            __hidden_logger_server::DiscardStoredCrashUpload(*thisPtr, *info);
            __hidden_logger_server::DestroyConnectionInfo(info, conCls);
            return MHD_NO;
        }

        const auto uploadDataPtr = MakeNotNull(upload_data);
        const usize uploadSizeLimit = __hidden_logger_server::UploadSizeLimit(info->isCrashUpload);
        if(
            uploadDataSize > static_cast<size_t>(uploadSizeLimit)
            || info->size > uploadSizeLimit - static_cast<usize>(uploadDataSize)
        ){
            __hidden_logger_server::EnqueueServerMessage(*thisPtr, NWB_TEXT("Received an oversized message"), Type::Error);
            __hidden_logger_server::DiscardStoredCrashUpload(*thisPtr, *info);
            __hidden_logger_server::DestroyConnectionInfo(info, conCls);
            return MHD_NO;
        }

        const usize appendSize = static_cast<usize>(uploadDataSize);
        if(!info->append(uploadDataPtr, appendSize)){
            __hidden_logger_server::EnqueueServerMessage(*thisPtr, NWB_TEXT("Failed to store upload chunk"), Type::Fatal);
            __hidden_logger_server::DiscardStoredCrashUpload(*thisPtr, *info);
            __hidden_logger_server::DestroyConnectionInfo(info, conCls);
            return MHD_NO;
        }

        uploadDataSize = 0;
        return MHD_YES;
    }

    if(info->isCrashUpload){
        if(info->closeCrashUploadStream() && info->crashUploadPath[0] != 0){
            const Path storedPath(thisPtr->arena(), AStringView(info->crashUploadPath));
            thisPtr->enqueueCrashUpload(storedPath);
            thisPtr->enqueue(
                StringFormat(
                    thisPtr->arena(),
                    NWB_TEXT("Crash upload queued for ingest at '{}'"),
                    PathToString<tchar>(storedPath)
                ),
                Type::EssentialInfo
            );
        }
        else{
            __hidden_logger_server::DiscardStoredCrashUpload(*thisPtr, *info);
            __hidden_logger_server::EnqueueServerMessage(*thisPtr, NWB_TEXT("Failed to store crash upload"), Type::Error);
        }
    }
    else{
        MessageType message = MakeMessageType(thisPtr->arena());
        const tchar* error = nullptr;
        if(ParseMessagePayload(thisPtr->arena(), info->buffer, info->size, message, error))
            thisPtr->enqueue(Move(message));
        else{
            __hidden_logger_server::EnqueueServerMessage(
                *thisPtr,
                error ? error : NWB_TEXT("Received a malformed message"),
                Type::Error
            );
        }
    }

    return __hidden_logger_server::FinishConnectionUpload(*thisPtr, *connection, info, conCls);
}


Server::Server()
    : UpdateBaseType("NWB::Log::Server")
    , m_daemon(nullptr)
    , m_processedMsgFile(BaseType::arena())
    , m_crashIngestConfig(BaseType::arena())
    , m_crashUploadToken(BaseType::arena())
    , m_crashUploads(BaseType::arena())
{}
Server::~Server(){
    if(m_daemon){
        MHD_stop_daemon(m_daemon);
        m_daemon = nullptr;
    }

    stopWorker();
    m_processedMsgFile.close();
}

bool Server::internalInit(
    u16 port,
    BasicStringView<tchar> logFileNameBase,
    AStringView crashSymbolStoreDirectory,
    CrashRetentionConfig crashRetentionConfig,
    AStringView crashUploadToken
){
    m_crashIngestConfig.symbolication.symbolStoreDirectory.clear();
    if(!crashSymbolStoreDirectory.empty())
        m_crashIngestConfig.symbolication.symbolStoreDirectory = crashSymbolStoreDirectory;
    m_crashIngestConfig.retention = crashRetentionConfig;
    m_crashUploadToken.assign(crashUploadToken.data(), crashUploadToken.size());

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

void Server::enqueueCrashUpload(const Path& path){
    PendingCrashUpload upload;
    const AString<LogArena> pathText = PathToString<char>(BaseType::arena(), path);
    if(pathText.size() >= sizeof(upload.path)){
        enqueue(BasicStringView<tchar>(NWB_TEXT("Crash upload path exceeds queue capacity")), Type::Error);
        return;
    }

    CopyFixedBuffer(upload.path, AStringView(pathText.data(), pathText.size()));
    m_crashUploads.emplace(upload);
}

bool Server::crashUploadAuthorized(MHD_Connection& connection)const{
    const char* authorizationHeader = MHD_lookup_connection_value(&connection, MHD_HEADER_KIND, "Authorization");
    return CrashUploadAuthorizationMatches(AStringView(m_crashUploadToken.data(), m_crashUploadToken.size()), authorizationHeader);
}

bool Server::tryDequeueCrashUpload(PendingCrashUpload& outUpload){
    return m_crashUploads.try_pop(outUpload);
}

bool Server::internalUpdate(){
    PendingCrashUpload crashUpload;
    while(tryDequeueCrashUpload(crashUpload)){
        const Path archivePath(BaseType::arena(), AStringView(crashUpload.path));
        CrashIngestResult ingestResult = ProcessCrashUpload(BaseType::arena(), archivePath, m_crashIngestConfig);
        enqueue(Move(ingestResult.message), ingestResult.type);
    }

    MessageType msg = MakeMessageType(BaseType::arena());
    while(tryDequeue(msg)){
        const auto type = Get<1>(msg);
        const LogString formattedMessage = FormatMessageForProcessing(BaseType::arena(), msg);

        Frame::print(formattedMessage, type);
        m_processedMsgFile.writeLine(formattedMessage);
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

