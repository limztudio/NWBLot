// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "resource_cooker.h"

#include <logger/client/logger.h>
#include <core/common/common.h>
#include <core/assets/asset_auto_registration.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_resource_cooker{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void* ResourceCookAlloc(usize size){ return NWB::Core::Alloc::CoreAlloc(size, "resource_cook"); }
static void ResourceCookFree(void* ptr){ NWB::Core::Alloc::CoreFree(ptr, "resource_cook"); }
static void* ResourceCookAllocAligned(usize size, usize align){ return NWB::Core::Alloc::CoreAllocAligned(size, align, "resource_cook"); }
static void ResourceCookFreeAligned(void* ptr){ NWB::Core::Alloc::CoreFreeAligned(ptr, "resource_cook"); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int ResourceCookerMain(int argc, char** argv){
    try{
        NWB::Core::Common::InitializerGuard commonInitializerGuard;
        if(!commonInitializerGuard.initialize())
            return -1;

        NWB::Core::Alloc::CustomArena cookArena(
            __hidden_resource_cooker::ResourceCookAlloc,
            __hidden_resource_cooker::ResourceCookFree,
            __hidden_resource_cooker::ResourceCookAllocAligned,
            __hidden_resource_cooker::ResourceCookFreeAligned
        );

        NWB::Core::Assets::AssetCookerRegistry assetCookerRegistry;
        NWB::Core::Assets::RegisterAutoCollectedAssetCookers(assetCookerRegistry, cookArena);

        CookOptions options;
        AString errorMessage;
        if(!ParseCommandLine(argc, argv, options, errorMessage)){
            if(!errorMessage.empty())
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to parse command line: {}"), StringConvert(errorMessage));
            PrintUsage();
            return -1;
        }

        const char* requestedAssetType = options.assetType.empty() ? "auto" : options.assetType.c_str();
        NWB_LOGGER_ESSENTIAL_INFO(
            NWB_TEXT("Resource cooker: starting cook type='{}' configuration='{}' roots={} output='{}'"),
            StringConvert(requestedAssetType),
            StringConvert(options.configuration.c_str()),
            options.assetRoots.size(),
            StringConvert(options.outputDirectory.c_str())
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

