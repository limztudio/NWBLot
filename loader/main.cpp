// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "pch.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <exception>

#include <CLI.hpp>

#include <global.h>

#include <command.h>

#include <logger/client/logger.h>
#include <core/common/common.h>
#include <core/frame/frame.h>

#include "project_entry.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_loader{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CallbackShutdownGuard{
    NWB::IProjectEntryCallbacks* callbacks = nullptr;
    NWB::ProjectRuntimeContext& context;
    bool active = false;

    ~CallbackShutdownGuard(){
        if(!active || !callbacks)
            return;

        try{
            callbacks->onShutdown(context);
        }
        catch(...){
        }
    }
};

struct UpdateCallbackContext{
    NWB::IProjectEntryCallbacks* callbacks = nullptr;
    NWB::ProjectRuntimeContext& context;
};

bool ProjectTickCallback(void* userData, f32 delta){
    NWB_ASSERT(userData);
    auto* updateContext = static_cast<UpdateCallbackContext*>(userData);
    NWB_ASSERT(updateContext->callbacks);
    return updateContext->callbacks->onUpdate(updateContext->context, delta);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static int MainLogic(NotNull<const char*> logAddress, void* inst){
    {
        NWB::Log::Client logger;
        if(!logger.init(logAddress))
            return -1;
        NWB_LOGGER_REGISTER(&logger);

        try{
            const NWB::ProjectFrameClientSize frameClientSize = NWB::QueryProjectFrameClientSize();
            if(frameClientSize.width == 0 || frameClientSize.height == 0){
                NWB_LOGGER_FATAL(NWB_TEXT("Invalid project frame client size: {}x{}"), frameClientSize.width, frameClientSize.height);
                return -1;
            }
            NWB::Core::Frame frame(inst, frameClientSize.width, frameClientSize.height);
            NWB::ProjectRuntimeContext context = {
                frame.graphics(),
                frame.projectObjectArena(),
                frame.projectThreadPool(),
                frame.projectJobSystem(),
            };

            if(!frame.init())
                return -1;

            auto callbacks = NWB::CreateProjectEntryCallbacks(context);
            if(!callbacks){
                NWB_LOGGER_FATAL(NWB_TEXT("CreateProjectEntryCallbacks failed: callback instance is null"));
                return -1;
            }
            __hidden_loader::CallbackShutdownGuard callbackShutdownGuard{ callbacks.get(), context };
            __hidden_loader::UpdateCallbackContext updateCallbackContext{ callbacks.get(), context };

            callbackShutdownGuard.active = true;
            if(!callbacks->onStartup(context))
                return -1;
            frame.setProjectUpdateCallback(&__hidden_loader::ProjectTickCallback, &updateCallbackContext);

            if(!frame.showFrame())
                return -1;

            if(!frame.mainLoop())
                return -1;
        }
        catch(const std::exception& e){
            NWB_LOGGER_FATAL(NWB_TEXT("Exception: {}"), StringConvert(e.what()));
            NWB_LOGGER_REGISTER(nullptr);
            return -1;
        }
        NWB_LOGGER_REGISTER(nullptr);
    }

    return 0;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT>
static int EntryPoint(isize argc, CharT** argv, void* inst){
    int ret;

    AString logAddress;
    {
        CLI::App app{ "loader" };

        {
            AString address;
            NWB::ArgAddOption<NWB::ArgCommand::LogAddress>(app, address);
            u16 port;
            NWB::ArgAddOption<NWB::ArgCommand::LogPort>(app, port);

            logAddress = StringFormat("{}:{}", address, port);
        }

        try{
            const bool hasValidArgv = argc > 0 && argv && argv[0];
            if(hasValidArgv)
                app.parse(static_cast<int>(argc), argv);
            else
                app.parse(std::vector<std::string>{});
        }
        catch(const CLI::ParseError& e){
            app.exit(e, std::cout, std::cerr);
            return -1;
        }
    }

    try{
        NWB::Core::Common::Initializer::instance().initialize();
        ret = MainLogic(MakeNotNull(logAddress.c_str()), inst);
        NWB::Core::Common::Initializer::instance().finalize();
    }
    catch(...){
        return -1;
    }

    return ret;
}


#if defined(NWB_PLATFORM_WINDOWS)
#include <windows.h>
int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow){
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;
    return EntryPoint(__argc, __wargv, hInstance);
}
int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow){
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;
    return EntryPoint(__argc, __argv, hInstance);
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

