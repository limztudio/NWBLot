// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"
#include "crash_paths.h"
#include "frame.h"

#include <core/alloc/module.h>
#include <core/common/name_symbols.h>

#include <global/algorithm.h>
#include <global/diagnostics.h>
#include <global/process.h>
#include <global/scope_exit.h>


#if defined(NWB_PLATFORM_LINUX)
#include <arpa/inet.h>
#include <netinet/in.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_logger_server{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr TStringView s_ServerName = NWB_TEXT("Server");
inline constexpr usize s_ConnectionInitialBufferCapacity = 256u;
inline constexpr usize s_BytesPerMebibyte = 1024u * 1024u;
inline constexpr usize s_MaxCrashPackageUploadMebibytes = 128u;
inline constexpr AStringView s_UnsupportedNameSymbolUploadPath = "/namesym";
inline constexpr AStringView s_UploadDiagnosticCategory = "logger_server_upload";

inline constexpr Name s_CrashIngestArena("logger/server/crash_ingest");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ConnectionUploadKind{
    enum Enum : u8{
        LogMessage,
        Crash,
        Telemetry,
    };
};

struct ConnectionInfo : NoCopy{
    u8* buffer = nullptr;
    usize size = 0u;
    usize capacity = 0u;
    u32 rejectionStatus = 0u;
    ConnectionUploadKind::Enum uploadKind = ConnectionUploadKind::LogMessage;
    b8 crashUploadOwned = false;
    Path crashUploadPath;
    OutputFileStream crashUploadStream;


public:
    explicit ConnectionInfo(LogArena& arena)
        : crashUploadPath(arena)
    {}
    ~ConnectionInfo()noexcept{
        if(crashUploadOwned)
            discardCrashUpload();
        else if(!closeCrashUploadStream())
            CaptureDiagnosticEvent(s_UploadDiagnosticCategory, "Failed to close crash upload stream", __FILE__, __LINE__);
        Core::Alloc::CoreFree(buffer);
    }


public:
    [[nodiscard]] b8 append(const BinaryByteView uploadData){
        const usize appendSize = uploadData.size();
        if(appendSize == 0u)
            return true;

        if(size > Limit<usize>::s_Max - appendSize)
            return false;

        if(uploadKind == ConnectionUploadKind::Crash)
            return appendCrashUpload(uploadData);

        const usize requiredSize = size + appendSize;
        if(!reserve(requiredSize))
            return false;

        NWB_MEMCPY(buffer + size, appendSize, uploadData.data(), appendSize);
        size = requiredSize;
        return true;
    }

    [[nodiscard]] b8 appendCrashUpload(const BinaryByteView uploadData){
        if(!crashUploadStream.is_open())
            return false;

        crashUploadStream.write(reinterpret_cast<const char*>(uploadData.data()), static_cast<StreamSize>(uploadData.size()));
        if(!crashUploadStream.good())
            return false;

        size += uploadData.size();
        return true;
    }

    [[nodiscard]] b8 closeCrashUploadStream()noexcept{
        if(crashUploadStream.is_open())
            crashUploadStream.close();

        return crashUploadStream.good();
    }

    void discardCrashUpload()noexcept{
        if(!crashUploadOwned)
            return;
        if(!closeCrashUploadStream())
            CaptureDiagnosticEvent(s_UploadDiagnosticCategory, "Failed to close discarded crash upload stream", __FILE__, __LINE__);
        const auto removed = RemoveFile(crashUploadPath);
        if(!removed){
            const DiagnosticEventText diagnostic = MakeDiagnosticEventText("Failed to remove discarded crash upload '{}', error {}", crashUploadPath.c_str(), removed.error().value());
            CaptureDiagnosticEvent(s_UploadDiagnosticCategory, diagnostic.view(), __FILE__, __LINE__);
        }
        crashUploadOwned = false;
    }

    [[nodiscard]] b8 reserve(const usize requiredSize)noexcept{
        if(requiredSize <= capacity)
            return true;

        const usize newCapacity = ::NextGrowingCapacity(capacity, requiredSize, s_ConnectionInitialBufferCapacity);

        auto* newBuffer = static_cast<u8*>(Core::Alloc::CoreRealloc(buffer, newCapacity));
        if(!newBuffer)
            return false;

        buffer = newBuffer;
        capacity = newCapacity;
        return true;
    }
};

inline constexpr usize s_MaxCrashPackageUploadBytes = s_MaxCrashPackageUploadMebibytes * s_BytesPerMebibyte;

static void DestroyConnectionInfo(ConnectionInfo*& info, void*& connectionContext)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static usize UploadSizeLimit(const ConnectionUploadKind::Enum uploadKind)noexcept{
    switch(uploadKind){
    case ConnectionUploadKind::Crash:
        return s_MaxCrashPackageUploadBytes;
    case ConnectionUploadKind::Telemetry:
        return s_MaxTelemetryUploadBytes;
    case ConnectionUploadKind::LogMessage:
    default:
        return s_MaxLogMessageUploadBytes;
    }
}

static void EnqueueServerMessage(Server& server, const TStringView message, const Core::Common::LogType::Enum type){
    server.enqueue(StringFormat(server.arena(), NWB_TEXT("{} on {}"), message, s_ServerName), type);
}

[[nodiscard]] MHD_Result QueueEmptyResponse(Server& server, MHD_Connection& connection, const unsigned int statusCode = MHD_HTTP_OK){
    static char s_EmptyResponse[] = "";

    auto* response = MHD_create_response_from_buffer(0, s_EmptyResponse, MHD_RESPMEM_PERSISTENT);
    if(!response){
        EnqueueServerMessage(server, NWB_TEXT("Failed to create a response"), Core::Common::LogType::Fatal);
        return MHD_NO;
    }

    const auto responseResult = MHD_queue_response(&connection, statusCode, response);
    MHD_destroy_response(response);
    return responseResult;
}

[[nodiscard]] MHD_Result FinishConnectionUpload(Server& server, MHD_Connection& connection, ConnectionInfo*& info, void*& connectionContext, const u32 statusCode){
    const MHD_Result responseResult = QueueEmptyResponse(server, connection, statusCode);
    DestroyConnectionInfo(info, connectionContext);
    return responseResult;
}

[[nodiscard]] static CrashScratchPath CrashInboxDirectory(Core::Alloc::ScratchArena& scratchArena){
    return CrashDefaultRootDirectory(scratchArena) / s_CrashInboxDirectoryName;
}

[[nodiscard]] static Path MakeCrashPackagePath(Server& server, Core::Alloc::ScratchArena& scratchArena){
    static Atomic<u64> s_CrashPackageCounter{ 1u };

    const auto timeResult = GetLocalTime();
    if(!timeResult)
        NWB_LOGGER_WARNING(NWB_TEXT("Failed to read local time for crash package name"));
    const LocalTime localTime = timeResult ? *timeResult : LocalTime{};

    const u64 counter = s_CrashPackageCounter.fetch_add(1u, MemoryOrder::relaxed);
    const auto fileName = StringFormat(
        scratchArena,
        "{}{:04}{:02}{:02}_{:02}{:02}{:02}_{}_{}{}",
        s_CrashUploadArchiveFilePrefix,
        localTime.tm_year + s_LocalTimeYearBase,
        localTime.tm_mon + s_LocalTimeMonthBase,
        localTime.tm_mday,
        localTime.tm_hour,
        localTime.tm_min,
        localTime.tm_sec,
        CurrentProcessId(),
        counter,
        s_CrashUploadArchiveFileExtension
    );

    return Path(server.arena(), CrashInboxDirectory(scratchArena) / fileName);
}

[[nodiscard]] static Expected<void, ErrorCode> OpenCrashUploadStream(Server& server, ConnectionInfo& info, Core::Alloc::ScratchArena& scratchArena){
    const CrashScratchPath inboxDirectory = CrashInboxDirectory(scratchArena);
    const auto directories = EnsureDirectories(inboxDirectory);
    if(!directories)
        return MakeUnexpected(directories.error());

    info.crashUploadPath = MakeCrashPackagePath(server, scratchArena);
    const AString<Core::Alloc::ScratchArena> pathText = PathToString<char>(scratchArena, info.crashUploadPath);
    if(pathText.size() >= s_MaxPendingCrashUploadPathText)
        return MakeUnexpected(MakeErrorCode(ErrorCondition::filename_too_long));

    info.crashUploadStream.open(info.crashUploadPath.c_str(), s_FileOpenBinary | OutputFileStream::noreplace);
    info.crashUploadOwned = info.crashUploadStream.is_open();
    if(!info.crashUploadOwned)
        return MakeUnexpected(MakeErrorCode(ErrorCondition::io_error));
    return {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<NotNull<ConnectionInfo*>, ErrorCode> CreateConnectionInfo(Server& server, const ConnectionUploadKind::Enum uploadKind){
    void* memory = Core::Alloc::CoreAlloc(sizeof(ConnectionInfo));
    if(!memory)
        return MakeUnexpected(MakeErrorCode(ErrorCondition::not_enough_memory));

    ScopeExit releaseMemory([&]()noexcept{ Core::Alloc::CoreFree(memory); });
    auto& info = *new(memory) ConnectionInfo(server.arena());
    releaseMemory.release();
    ScopeExit releaseInfo([&]()noexcept{
        info.~ConnectionInfo();
        Core::Alloc::CoreFree(&info);
    });
    info.uploadKind = uploadKind;
    if(uploadKind == ConnectionUploadKind::Crash){
        Core::Alloc::ScratchArena scratchArena(Name("logger/server/crash_upload/scratch"));
        const auto opened = OpenCrashUploadStream(server, info, scratchArena);
        if(!opened)
            return MakeUnexpected(opened.error());
    }
    releaseInfo.release();
    return MakeNotNull(&info);
}

static void DestroyConnectionInfo(ConnectionInfo*& info, void*& connectionContext)noexcept{
    if(info){
        info->~ConnectionInfo();
        Core::Alloc::CoreFree(info);
    }

    info = nullptr;
    connectionContext = nullptr;
}

static void RequestCompleted([[maybe_unused]] void* serverContext, [[maybe_unused]] MHD_Connection* connection, void** connectionContextAddress, [[maybe_unused]] MHD_RequestTerminationCode terminationCode)noexcept{
    if(!connectionContextAddress)
        return;
    auto* info = static_cast<ConnectionInfo*>(*connectionContextAddress);
    DestroyConnectionInfo(info, *connectionContextAddress);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


MHD_Result Server::RequestCallback(void* serverContext, MHD_Connection* connection, const char* url, const char* method, [[maybe_unused]] const char* version, const char* uploadData, size_t* uploadDataSizeAddress, void** connectionContextAddress){
    if(!serverContext || !connection || !url || !method || !uploadDataSizeAddress || !connectionContextAddress)
        return MHD_NO;

    const AStringView methodView(method);
    const auto thisPtr = MakeNotNull(static_cast<Server*>(serverContext));
    if(methodView == "GET" && AStringView(url) == "/ready" && !thisPtr->m_readinessToken.empty()){
        auto* response = MHD_create_response_from_buffer(thisPtr->m_readinessToken.size(), thisPtr->m_readinessToken.data(), MHD_RESPMEM_MUST_COPY);
        if(!response)
            return MHD_NO;
        const MHD_Result result = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
        return result;
    }
    if(methodView != "POST")
        return MHD_NO;
    const auto uploadDataSizePtr = MakeNotNull(uploadDataSizeAddress);
    auto& uploadDataSize = *uploadDataSizePtr;
    const auto connectionContextPtr = MakeNotNull(connectionContextAddress);
    auto& connectionContext = *connectionContextPtr;

    const AStringView urlView(url);
    if(urlView == __hidden_logger_server::s_UnsupportedNameSymbolUploadPath)
        return __hidden_logger_server::QueueEmptyResponse(*thisPtr, *connection, MHD_HTTP_NOT_FOUND);

    const b8 isCrashUpload = urlView == Core::Crash::PackageNames::s_CrashUploadEndpoint;
    const b8 isTelemetryUpload = urlView == s_TelemetryUploadEndpoint;
    const auto uploadKind = isCrashUpload ? __hidden_logger_server::ConnectionUploadKind::Crash : isTelemetryUpload ? __hidden_logger_server::ConnectionUploadKind::Telemetry : __hidden_logger_server::ConnectionUploadKind::LogMessage;

    if(!connectionContext){
        if(isCrashUpload && !thisPtr->crashUploadAuthorized(*connection)){
            __hidden_logger_server::EnqueueServerMessage(*thisPtr, NWB_TEXT("Rejected unauthorized crash upload"), Core::Common::LogType::Warning);
            return __hidden_logger_server::QueueEmptyResponse(*thisPtr, *connection, MHD_HTTP_UNAUTHORIZED);
        }

        const auto info = __hidden_logger_server::CreateConnectionInfo(*thisPtr, uploadKind);
        if(!info){
            thisPtr->enqueue(
                StringFormat(thisPtr->arena(), NWB_TEXT("Failed to initialize connection upload state: [{}] {} on {}"), info.error().value(), ConvertString(info.error().message()), __hidden_logger_server::s_ServerName),
                Core::Common::LogType::Error
            );
            return __hidden_logger_server::QueueEmptyResponse(*thisPtr, *connection, MHD_HTTP_SERVICE_UNAVAILABLE);
        }

        connectionContext = info->get();
        return MHD_YES;
    }

    auto* info = static_cast<__hidden_logger_server::ConnectionInfo*>(connectionContext);

    if(uploadDataSize){
        if(info->rejectionStatus){
            uploadDataSize = 0u;
            return MHD_YES;
        }
        if(!uploadData){
            __hidden_logger_server::EnqueueServerMessage(*thisPtr, NWB_TEXT("Received a malformed upload chunk"), Core::Common::LogType::Error);
            info->discardCrashUpload();
            info->rejectionStatus = MHD_HTTP_BAD_REQUEST;
            uploadDataSize = 0u;
            return MHD_YES;
        }

        const usize uploadSizeLimit = __hidden_logger_server::UploadSizeLimit(info->uploadKind);
        if(uploadDataSize > static_cast<size_t>(uploadSizeLimit) || info->size > uploadSizeLimit - static_cast<usize>(uploadDataSize)){
            __hidden_logger_server::EnqueueServerMessage(*thisPtr, NWB_TEXT("Received an oversized message"), Core::Common::LogType::Error);
            info->discardCrashUpload();
            info->rejectionStatus = MHD_HTTP_CONTENT_TOO_LARGE;
            uploadDataSize = 0u;
            return MHD_YES;
        }

        const usize appendSize = static_cast<usize>(uploadDataSize);
        if(!info->append(BinaryByteView(reinterpret_cast<const u8*>(uploadData), appendSize))){
            __hidden_logger_server::EnqueueServerMessage(*thisPtr, NWB_TEXT("Failed to store upload chunk"), Core::Common::LogType::Fatal);
            info->discardCrashUpload();
            info->rejectionStatus = MHD_HTTP_SERVICE_UNAVAILABLE;
            uploadDataSize = 0u;
            return MHD_YES;
        }

        uploadDataSize = 0;
        return MHD_YES;
    }

    if(info->rejectionStatus)
        return __hidden_logger_server::FinishConnectionUpload(*thisPtr, *connection, info, connectionContext, info->rejectionStatus);

    u32 responseStatus = MHD_HTTP_OK;
    if(info->uploadKind == __hidden_logger_server::ConnectionUploadKind::Crash){
        if(info->closeCrashUploadStream() && info->crashUploadOwned){
            Core::Alloc::ScratchArena scratchArena(Name("logger/server/crash_enqueue/scratch"));
            const Path& storedPath = info->crashUploadPath;
            if(thisPtr->enqueueCrashUpload(storedPath, scratchArena)){
                info->crashUploadOwned = false;
                thisPtr->enqueue(StringFormat(thisPtr->arena(), NWB_TEXT("Crash upload queued for ingest at '{}'"), PathToString<tchar>(storedPath)), Core::Common::LogType::EssentialInfo);
            }
            else{
                info->crashUploadOwned = false;
                responseStatus = MHD_HTTP_SERVICE_UNAVAILABLE;
                thisPtr->enqueue(StringFormat(thisPtr->arena(), NWB_TEXT("Crash upload could not be queued and was retained at '{}'"), PathToString<tchar>(storedPath)), Core::Common::LogType::Error);
            }
        }
        else{
            info->crashUploadOwned = false;
            responseStatus = MHD_HTTP_SERVICE_UNAVAILABLE;
            thisPtr->enqueue(StringFormat(thisPtr->arena(), NWB_TEXT("Crash upload could not be closed and was retained at '{}'"), PathToString<tchar>(info->crashUploadPath)), Core::Common::LogType::Error);
        }
    }
    else if(info->uploadKind == __hidden_logger_server::ConnectionUploadKind::Telemetry){
        TelemetryIngestResult result = ProcessTelemetryUpload(thisPtr->arena(), info->buffer, info->size, thisPtr->m_telemetryIngestConfig);
        if(!result.storedRaw)
            responseStatus = info->size == 0u ? MHD_HTTP_BAD_REQUEST : MHD_HTTP_SERVICE_UNAVAILABLE;
        if(result.rawCleanupError){
            thisPtr->enqueue(StringFormat(thisPtr->arena(), NWB_TEXT("Telemetry raw stream was stored but staging cleanup failed: raw='{}' staging_retained={} error={}"), PathToString<tchar>(result.rawPath), result.rawStagingRetained, result.rawCleanupError.value()), Core::Common::LogType::Error);
        }
        thisPtr->enqueue(Move(result.message), result.type);
    }
    else{
        auto message = ParseMessagePayload(thisPtr->arena(), info->buffer, info->size);
        if(message)
            thisPtr->enqueue(Move(*message));
        else{
            responseStatus = MHD_HTTP_BAD_REQUEST;
            __hidden_logger_server::EnqueueServerMessage(*thisPtr, message.error(), Core::Common::LogType::Error);
        }
    }

    return __hidden_logger_server::FinishConnectionUpload(*thisPtr, *connection, info, connectionContext, responseStatus);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void Server::CrashIngestUpdate(Server& self){
    for(;;){
        self.m_crashIngestSemaphore.acquire();

        while(const auto crashUpload = self.tryDequeueCrashUpload()){
            LogArena ingestArena(__hidden_logger_server::s_CrashIngestArena);

            CrashIngestConfig ingestConfig(ingestArena);
            ingestConfig.storageDirectory = self.m_crashIngestConfig.storageDirectory;
            ingestConfig.symbolication.symbolStoreDirectory = self.m_crashIngestConfig.symbolication.symbolStoreDirectory;
            ingestConfig.retention = self.m_crashIngestConfig.retention;

            const Path archivePath(ingestArena, AStringView(crashUpload->path.data()));
            CrashIngestResult ingestResult = ProcessCrashUpload(ingestArena, archivePath, ingestConfig);
            self.enqueue(Move(ingestResult.message), ingestResult.type);
        }

        if(self.m_crashIngestExit.load(MemoryOrder::acquire))
            break;
    }
}


Server::Server()
    : UpdateBaseType("NWB::Log::Server")
    , m_daemon(nullptr)
    , m_processedMessageFile(BaseType::arena())
    , m_crashIngestConfig(BaseType::arena())
    , m_telemetryIngestConfig(BaseType::arena())
    , m_crashUploadToken(BaseType::arena())
    , m_readinessToken(BaseType::arena())
    , m_crashUploads(BaseType::arena())
    , m_crashIngestSemaphore(0)
    , m_crashIngestExit(false)
{}

Server::~Server()noexcept{
    if(m_daemon){
        MHD_stop_daemon(m_daemon);
        m_daemon = nullptr;
    }

    stopCrashIngestWorker();
    stopWorker();
    m_processedMessageFile.close();
}

b8 Server::internalInit(u16 port, BasicStringView<tchar> logFileNameBase, AStringView crashSymbolStoreDirectory, CrashRetentionConfig crashRetentionConfig, AStringView crashUploadToken, AStringView readinessToken){
    m_crashIngestConfig.symbolication.symbolStoreDirectory.clear();
    if(!crashSymbolStoreDirectory.empty())
        m_crashIngestConfig.symbolication.symbolStoreDirectory = crashSymbolStoreDirectory;
    m_crashIngestConfig.retention = crashRetentionConfig;
    m_crashUploadToken.assign(crashUploadToken.data(), crashUploadToken.size());
    m_readinessToken.assign(readinessToken.data(), readinessToken.size());
    const auto nameSymbolLoad = Core::Common::NameSymbols::LoadDefaultFile();
    b8 loadedNameSymbols = false;
    if(nameSymbolLoad)
        loadedNameSymbols = *nameSymbolLoad;
    else{
        const Core::Common::NameSymbols::DefaultFileLoadFailure& failure = nameSymbolLoad.error();
        loadedNameSymbols = failure.loadedAny;
        enqueue(StringFormat(BaseType::arena(), NWB_TEXT("Log server: startup name-symbol filesystem load failed: [{}] {}"), failure.code.value(), ConvertString(failure.code.message())), Core::Common::LogType::Warning);
    }
    if(loadedNameSymbols)
        enqueue(BasicStringView<tchar>(NWB_TEXT("Log server: loaded startup name-symbol mappings")), Core::Common::LogType::Info);

    if(logFileNameBase.empty()){
        if(!m_processedMessageFile.openByExecutableName())
            return false;
    }
    else{
        if(!m_processedMessageFile.open(logFileNameBase))
            return false;
    }

    sockaddr_in listenAddress = {};
    listenAddress.sin_family = AF_INET;
    listenAddress.sin_port = htons(port);
    listenAddress.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    m_daemon = MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD
        , 0u
        , nullptr
        , nullptr
        , &Server::RequestCallback
        , this
        , MHD_OPTION_SOCK_ADDR_LEN
        , static_cast<socklen_t>(sizeof(listenAddress))
        , &listenAddress
        , MHD_OPTION_NOTIFY_COMPLETED
        , &__hidden_logger_server::RequestCompleted
        , nullptr
        , MHD_OPTION_END
    );

    if(!m_daemon)
        return false;

    m_crashIngestExit.store(false, MemoryOrder::release);
    m_crashIngestThread = Thread([this](){ CrashIngestUpdate(*this); });
    return true;
}

b8 Server::internalUpdate(){
    MessageType msg = MakeMessageType(BaseType::arena());
    while(tryDequeue(msg)){
        const auto type = Get<1>(msg);
        LogString formattedMessage = FormatMessageForProcessing(BaseType::arena(), msg);
        Core::Common::NameSymbols::DecodeHashTokens(BaseType::arena(), formattedMessage);

        Frame::Print(formattedMessage, type);
        if(!m_processedMessageFile.writeLine(formattedMessage)){
            constexpr TStringView s_WriteFailure = NWB_TEXT("Log server: failed to write processed message to file.");
            Frame::Print(s_WriteFailure, Core::Common::LogType::Error);
#if defined(NWB_PLATFORM_WINDOWS)
            NWB_TCERR << s_WriteFailure << static_cast<tchar>('\n');
#endif
        }
    }

    return true;
}

b8 Server::enqueueCrashUpload(const Path& path, Core::Alloc::ScratchArena& scratchArena){
    PendingCrashUpload upload;
    const AString<Core::Alloc::ScratchArena> pathText = PathToString<char>(scratchArena, path);
    if(pathText.size() >= sizeof(upload.path)){
        enqueue(BasicStringView<tchar>(NWB_TEXT("Crash upload path exceeds queue capacity")), Core::Common::LogType::Error);
        return false;
    }

    CopyFixedBuffer(upload.path, AStringView(pathText.data(), pathText.size()));
    m_crashUploads.emplace(upload);
    m_crashIngestSemaphore.release();
    return true;
}

void Server::stopCrashIngestWorker(){
    const b8 alreadyStopping = m_crashIngestExit.exchange(true, MemoryOrder::acq_rel);
    if(!alreadyStopping)
        m_crashIngestSemaphore.release();
    if(m_crashIngestThread.joinable())
        m_crashIngestThread.join();
}

b8 Server::crashUploadAuthorized(MHD_Connection& connection)const noexcept{
    const char* authorizationHeader = MHD_lookup_connection_value(&connection, MHD_HEADER_KIND, "Authorization");
    return CrashUploadAuthorizationMatches(AStringView(m_crashUploadToken.data(), m_crashUploadToken.size()), authorizationHeader ? AStringView(authorizationHeader) : AStringView{});
}

Expected<PendingCrashUpload> Server::tryDequeueCrashUpload(){
    PendingCrashUpload upload;
    if(!m_crashUploads.try_pop(upload))
        return MakeUnexpected(Failure{});
    return upload;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

