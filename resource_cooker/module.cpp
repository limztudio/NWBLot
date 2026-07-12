
#include "module.h"
#include "command_line.h"

#include <global/core/alloc/thread.h>
#include <global/core/common/log.h>
#include <global/core/assets/auto_registration.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_resource_cooker{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_CookArena("resource_cooker/cook");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u32 QueryCookWorkerThreadCount(){
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
        const CommandLineParseResult::Enum parseResult = ParseCommandLine(argc, argv, options, errorMessage);
        if(parseResult != CommandLineParseResult::Success){
            if(!errorMessage.empty())
                NWB_LOGGER_WARNING(NWB_TEXT("Failed to parse command line: {}"), StringConvert(errorMessage));
            PrintUsage();
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

