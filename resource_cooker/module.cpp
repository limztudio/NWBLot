// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"
#include "command_line.h"

#include <core/alloc/thread.h>
#include <core/common/log.h>
#include <core/assets/cooker_registration.h>
#include <global/cpu_topology.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_resource_cooker{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_CookArena("resource_cooker/cook");
inline constexpr u32 s_CookMainThreadReservedCoreCount = 1u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u32 QueryCookWorkerThreadCount(){
    const u32 coreCount = ::QueryCpuCoreCount(CpuAffinity::Any);
    return coreCount > s_CookMainThreadReservedCoreCount ? coreCount - s_CookMainThreadReservedCoreCount : 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int ResourceCookerMain(int argc, char** argv){
    NWB::Core::Alloc::GlobalArena cookArena(__hidden_resource_cooker::s_CookArena);
    NWB::Core::Alloc::ThreadPool cookThreadPool(
        __hidden_resource_cooker::QueryCookWorkerThreadCount(),
        CpuAffinity::Any
    );

    NWB::Core::Assets::AssetCookerRegistry assetCookerRegistry(cookArena);
    NWB::Core::Assets::RegisterAutoCollectedAssetCookers(assetCookerRegistry, cookArena);

    CookOptions options(cookArena, cookThreadPool);
    const CommandLineParseResult::Enum parseResult = ParseCommandLine(argc, argv, options);
    int result = 0;
    if(parseResult != CommandLineParseResult::Success){
        PrintUsage();
        result = parseResult == CommandLineParseResult::Help ? 0 : -1;
    }
    else{
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
            result = -1;
        }
        else
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Resource cooker: asset cook succeeded"));
    }

    cookThreadPool.finish();
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

