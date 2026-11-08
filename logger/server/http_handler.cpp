// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "http_handler.h"
#include "http_connection_upload.h"
#include <logger/message_payload_codec.h>

#include "crash_auth.h"
#include "crash_paths.h"
#include "module.h"

#include <core/alloc/module.h>

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
inline constexpr usize s_BytesPerMebibyte = 1024u * 1024u;
inline constexpr usize s_MaxCrashPackageUploadMebibytes = 128u;
inline constexpr AStringView s_UnsupportedNameSymbolUploadPath = "/namesym";



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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


MHD_Result ServerHttpHandler::RequestCallback(void* serverContext, MHD_Connection* connection, const char* url, const char* method, [[maybe_unused]] const char* version, const char* uploadData, size_t* uploadDataSizeAddress, void** connectionContextAddress){
    if(!serverContext || !connection || !url || !method || !uploadDataSizeAddress || !connectionContextAddress)
        return MHD_NO;

    const AStringView methodView(method);
    const auto handler = MakeNotNull(static_cast<ServerHttpHandler*>(serverContext));
    const auto thisPtr = MakeNotNull(&handler->m_server);
    if(methodView == "GET" && AStringView(url) == "/ready" && !handler->m_readinessToken.empty()){
        auto* response = MHD_create_response_from_buffer(handler->m_readinessToken.size(), handler->m_readinessToken.data(), MHD_RESPMEM_MUST_COPY);
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
        if(isCrashUpload && !handler->crashUploadAuthorized(*connection)){
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
        TelemetryIngestResult result = ProcessTelemetryUpload(thisPtr->arena(), info->buffer, info->size, handler->m_telemetryIngestConfig);
        if(!result.storedRaw)
            responseStatus = info->size == 0u ? MHD_HTTP_BAD_REQUEST : MHD_HTTP_SERVICE_UNAVAILABLE;
        if(result.rawCleanupError){
            thisPtr->enqueue(StringFormat(thisPtr->arena(), NWB_TEXT("Telemetry raw stream was stored but staging cleanup failed: raw='{}' staging_retained={} error={}"), PathToString<tchar>(result.rawPath), result.rawStagingRetained, result.rawCleanupError.value()), Core::Common::LogType::Error);
        }
        thisPtr->enqueue(Move(result.message), result.type);
    }
    else{
        auto message = MessagePayloadCodec::parse(thisPtr->arena(), info->buffer, info->size);
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


ServerHttpHandler::ServerHttpHandler(Server& server, LogArena& arena)
    : m_server(server)
    , m_telemetryIngestConfig(arena)
    , m_crashUploadToken(arena)
    , m_readinessToken(arena)
{}

ServerHttpHandler::~ServerHttpHandler()noexcept{
    stop();
}

void ServerHttpHandler::configure(const AStringView crashUploadToken, const AStringView readinessToken){
    m_crashUploadToken.assign(crashUploadToken.data(), crashUploadToken.size());
    m_readinessToken.assign(readinessToken.data(), readinessToken.size());
}

b8 ServerHttpHandler::start(const u16 port){
    sockaddr_in listenAddress = {};
    listenAddress.sin_family = AF_INET;
    listenAddress.sin_port = htons(port);
    listenAddress.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    m_daemon = MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD
        , 0u
        , nullptr
        , nullptr
        , &ServerHttpHandler::RequestCallback
        , this
        , MHD_OPTION_SOCK_ADDR_LEN
        , static_cast<socklen_t>(sizeof(listenAddress))
        , &listenAddress
        , MHD_OPTION_NOTIFY_COMPLETED
        , &__hidden_logger_server::RequestCompleted
        , nullptr
        , MHD_OPTION_END
    );

    return m_daemon != nullptr;
}

void ServerHttpHandler::stop()noexcept{
    if(m_daemon){
        MHD_stop_daemon(m_daemon);
        m_daemon = nullptr;
    }
}

b8 ServerHttpHandler::crashUploadAuthorized(MHD_Connection& connection)const noexcept{
    const char* authorizationHeader = MHD_lookup_connection_value(&connection, MHD_HEADER_KIND, "Authorization");
    return CrashUploadAuthorizationMatches(AStringView(m_crashUploadToken.data(), m_crashUploadToken.size()), authorizationHeader ? AStringView(authorizationHeader) : AStringView{});
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

