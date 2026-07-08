// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"

#include <global/environment.h>
#include <core/alloc/thread.h>
#include <core/common/log.h>
#include <core/assets/auto_registration.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_resource_cooker{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_CookArena("resource_cooker/cook");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u32 QueryCookWorkerThreadCount(){
    // QueryCoreCount ignores Linux affinity masks, so taskset cannot throttle multithreaded mesh cooking. The
    // standalone cooker can SIGTRAP inside the logger globalUpdate under worker-thread contention (the failure
    // moves between meshes), so allow forcing a worker count from the environment for diagnostics. Setting
    // NWB_COOK_WORKER_THREADS=0 pins cooking to the calling thread. A malformed or out-of-range value falls back
    // to the core-count default rather than aborting the cook.
    char envBuffer[32];
    if(ReadEnvironmentVariableBuffer("NWB_COOK_WORKER_THREADS", envBuffer, sizeof(envBuffer))){
        char* parseEnd = nullptr;
        const unsigned long parsed = ::strtoul(envBuffer, &parseEnd, 10);
        const bool wholeStringConsumed = (parseEnd != envBuffer) && (*parseEnd == 0);
        if(wholeStringConsumed && parsed <= 1024u){
            NWB_LOGGER_INFO(NWB_TEXT("Resource cooker: NWB_COOK_WORKER_THREADS={} overriding core-count worker pool"), parsed);
            return static_cast<u32>(parsed);
        }
        NWB_LOGGER_WARNING(NWB_TEXT("Resource cooker: ignoring malformed NWB_COOK_WORKER_THREADS='{}'"), StringConvert(envBuffer));
    }

    const u32 coreCount = NWB::Core::Alloc::QueryCoreCount(NWB::Core::Alloc::CoreAffinity::Any);
    return coreCount > 1u ? coreCount - 1u : 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int ResourceCookerMain(int argc, char** argv){
    try{
        NWB::Core::Alloc::GlobalArena cookArena(__hidden_resource_cooker::s_CookArena);
        NWB::Core::Alloc::ThreadPool cookThreadPool(
            __hidden_resource_cooker::QueryCookWorkerThreadCount(),
            NWB::Core::Alloc::CoreAffinity::Any
        );

        NWB::Core::Assets::AssetCookerRegistry assetCookerRegistry(cookArena);
        NWB::Core::Assets::RegisterAutoCollectedAssetCookers(assetCookerRegistry, cookArena);

        CookOptions options(cookArena, cookThreadPool);
        NWB::Core::Assets::AssetString errorMessage{cookArena};
        const CommandLineParseResult::Enum parseResult = ParseCommandLine(argc, argv, cookArena, options, errorMessage);
        if(parseResult != CommandLineParseResult::Success){
            if(!errorMessage.empty())
                NWB_LOGGER_WARNING(NWB_TEXT("Failed to parse command line: {}"), StringConvert(errorMessage));
            PrintUsage(cookArena);
            return parseResult == CommandLineParseResult::Help ? 0 : -1;
        }
        const char* requestedAssetType = options.assetType.empty() ? "auto" : options.assetType.c_str();
        NWB_LOGGER_ESSENTIAL_INFO(
            NWB_TEXT("Resource cooker: starting cook type='{}' configuration='{}' roots={} output='{}' worker_threads={}"),
            StringConvert(requestedAssetType),
            StringConvert(options.configuration.c_str()),
            options.assetRoots.size(),
            StringConvert(options.outputDirectory.c_str()),
            cookThreadPool.workerThreadCount()
        );

        if(!assetCookerRegistry.cook(options)){
            NWB_LOGGER_ERROR(NWB_TEXT("Resource cooker: asset cook failed"));
            return -1;
        }

        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Resource cooker: asset cook succeeded"));
        return 0;
    }
    catch(const GeneralException& e){
        NWB_LOGGER_FATAL(NWB_TEXT("Unhandled exception: {}"), StringConvert(e.what()));
        return -1;
    }
    catch(...){
        NWB_LOGGER_FATAL(NWB_TEXT("Unhandled exception"));
        return -1;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

