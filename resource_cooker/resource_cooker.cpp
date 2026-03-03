// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "resource_cooker.h"

#include <core/assets/asset_module.h>

#include <logger/client/logger.h>


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
        NWB::Log::Client logger;
        NWB_LOGGER_REGISTER(&logger);

        NWB::Core::Alloc::CustomArena cookArena(
            __hidden_resource_cooker::ResourceCookAlloc,
            __hidden_resource_cooker::ResourceCookFree,
            __hidden_resource_cooker::ResourceCookAllocAligned,
            __hidden_resource_cooker::ResourceCookFreeAligned
        );

        CookOptions options;
        options.cookArena = &cookArena;
        AString errorMessage;
        NWB::Core::Assets::AssetCookerRegistry assetCookerRegistry;
        NWB::Core::Assets::RegisterDomainAssetCookers(assetCookerRegistry);
        if(!ParseCommandLine(argc, argv, options, errorMessage)){
            if(!errorMessage.empty())
                NWB_CERR << errorMessage << '\n';
            PrintUsage();
            return -1;
        }

        if(!assetCookerRegistry.cook(options, errorMessage)){
            NWB_CERR << errorMessage << '\n';
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

