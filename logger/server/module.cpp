// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"
#include "frame.h"

#include <logger/server/arena_names.adhd.h>
#include <logger/server/server_values.adhd.h>

#include <core/alloc/module.h>
#include <core/common/name_symbols.h>



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Server::Server()
    : UpdateBaseType("NWB::Log::Server")
    , m_processedMessageFile(BaseType::arena())
    , m_crashIngestWorker(BaseType::arena(), *this)
    , m_httpHandler(*this, BaseType::arena())
{}

Server::~Server()noexcept{
    m_httpHandler.stop();

    m_crashIngestWorker.stop();
    stopWorker();
    m_processedMessageFile.close();
}

b8 Server::internalInit(u16 port, BasicStringView<tchar> logFileNameBase, AStringView crashSymbolStoreDirectory, CrashRetentionConfig crashRetentionConfig, AStringView crashUploadToken, AStringView readinessToken){
    m_crashIngestWorker.configure(crashSymbolStoreDirectory, crashRetentionConfig);
    m_httpHandler.configure(crashUploadToken, readinessToken);
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

    if(!m_httpHandler.start(port))
        return false;

    m_crashIngestWorker.start();
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
    if(m_crashIngestWorker.enqueue(path, scratchArena))
        return true;
    enqueue(BasicStringView<tchar>(NWB_TEXT("Crash upload path exceeds queue capacity")), Core::Common::LogType::Error);
    return false;
}

void Server::deliverCrashIngestMessage(LogString&& message, const Core::Common::LogType::Enum type){
    enqueue(Move(message), type);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

