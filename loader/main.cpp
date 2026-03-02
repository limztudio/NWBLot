// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "pch.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <CLI.hpp>

#include <global.h>

#include <command.h>

#include <logger/client/logger.h>
#include <core/common/common.h>
#include <core/frame/frame.h>
#include <core/graphics/shader_archive.h>
#include <core/filesystem/filesystem.h>

#include "project_entry.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_loader{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CallbackShutdownGuard{
    NWB::IProjectEntryCallbacks* callbacks = nullptr;
    bool active = false;

    ~CallbackShutdownGuard(){
        if(!active || !callbacks)
            return;

        try{
            callbacks->onShutdown();
        }
        catch(...){
        }
    }
};

struct UpdateCallbackContext{
    NWB::IProjectEntryCallbacks* callbacks = nullptr;
};

bool ProjectTickCallback(void* userData, f32 delta){
    NWB_ASSERT(userData);
    auto* updateContext = static_cast<UpdateCallbackContext*>(userData);
    NWB_ASSERT(updateContext->callbacks);
    return updateContext->callbacks->onUpdate(delta);
}

Path ResolveShaderMountDirectory(){
    return Path("res");
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
            NWB::Core::Filesystem::VolumeSession graphicsVolume(frame.projectObjectArena());
            HashSet<AString> missingVirtualPaths;
            NWB::ProjectRuntimeContext context = {
                frame.graphics(),
                frame.projectObjectArena(),
                frame.projectThreadPool(),
                frame.projectJobSystem(),
                {},
            };
            context.shaderBinaryLookup = [&graphicsVolume, &missingVirtualPaths](const AStringView shaderName, const AStringView variantName, Vector<u8>& outBinary){
                const AString virtualPath = NWB::Core::ShaderArchive::buildVirtualPath(shaderName, variantName);

                if(missingVirtualPaths.find(virtualPath) != missingVirtualPaths.end())
                    return false;

                AString errorMessage;
                if(graphicsVolume.loadData(virtualPath, outBinary, errorMessage))
                    return true;

                missingVirtualPaths.insert(virtualPath);
                NWB_LOGGER_WARNING(
                    NWB_TEXT("Shader lookup miss: shader='{}', variant='{}', virtualPath='{}' ({})"),
                    StringConvert(shaderName),
                    StringConvert(variantName),
                    StringConvert(virtualPath),
                    StringConvert(errorMessage)
                );
                return false;
            };

            if(!frame.init())
                return -1;

            const Path shaderMountDirectory = __hidden_loader::ResolveShaderMountDirectory();
            AString mountError;
            if(!graphicsVolume.load("graphics", shaderMountDirectory, mountError)){
                NWB_LOGGER_FATAL(
                    NWB_TEXT("Failed to load graphics volume from '{}': {}"),
                    StringConvert(shaderMountDirectory.string()),
                    StringConvert(mountError)
                );
                return -1;
            }

            auto callbacks = NWB::CreateProjectEntryCallbacks(context);
            if(!callbacks){
                NWB_LOGGER_FATAL(NWB_TEXT("CreateProjectEntryCallbacks failed: callback instance is null"));
                return -1;
            }
            __hidden_loader::CallbackShutdownGuard callbackShutdownGuard{ callbacks.get() };
            __hidden_loader::UpdateCallbackContext updateCallbackContext{ callbacks.get() };

            callbackShutdownGuard.active = true;
            if(!callbacks->onStartup())
                return -1;
            frame.setProjectUpdateCallback(&__hidden_loader::ProjectTickCallback, &updateCallbackContext);

            if(!frame.showFrame())
                return -1;

            if(!frame.mainLoop())
                return -1;
        }
        catch(const GeneralException& e){
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

