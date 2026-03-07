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

struct CommonInitializerGuard{
    bool active = false;

    ~CommonInitializerGuard(){
        if(active)
            NWB::Core::Common::Initializer::instance().finalize();
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int ResourceCookerMain(int argc, char** argv){
    try{
        __hidden_resource_cooker::CommonInitializerGuard commonInitializerGuard;
        if(!NWB::Core::Common::Initializer::instance().initialize())
            return -1;
        commonInitializerGuard.active = true;

        NWB::Log::ClientStandalone logger;
        if(!logger.init(NWB_TEXT("resource_cooker")))
            return -1;
        NWB_LOGGER_REGISTER(&logger);

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

        if(!assetCookerRegistry.cook(options)){
            return -1;
        }

        return 0;
    }
    catch(const GeneralException& e){
        const AString exceptionMessage = StringFormat("Unhandled exception: {}", e.what());
        NWB_CERR << exceptionMessage << '\n';
        return -1;
    }
    catch(...){
        NWB_CERR << "Unhandled exception" << '\n';
        return -1;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

