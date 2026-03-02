// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "resource_cooker.h"

#include <core/assets/asset_module.h>

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int ResourceCookerMain(int argc, char** argv){
    try{
        NWB::Log::Client logger;
        NWB_LOGGER_REGISTER(&logger);

        CookOptions options;
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

