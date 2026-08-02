// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cooker_registration.h"
#include "arena_names.h"
#include "registration_queue.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cooker_registration{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AutoRegistrationQueue<AssetCookerFactory>& QueryAutoCookerFactoryQueue(){
    static AutoRegistrationQueue<AssetCookerFactory> queue(AssetsArenaScope::s_AutoCookerFactoryQueueArena);
    return queue;
}

template<typename FactoryT>
bool InitializeAutoFactory(AutoRegistrationQueue<FactoryT>& queue, const FactoryT factory){
    if(factory == nullptr)
        return true;

    queue.appendUnique(factory, [](const FactoryT lhs, const FactoryT rhs){ return lhs == rhs; });
    return true;
}

template<typename FactoryVector, typename CreateProduct, typename RegisterProduct, typename LogNullProduct, typename LogRegisterFailure>
void RegisterFactoryProducts(
    const FactoryVector& factories,
    CreateProduct&& createProduct,
    RegisterProduct&& registerProduct,
    LogNullProduct&& logNullProduct,
    LogRegisterFailure&& logRegisterFailure){
    for(const auto factory : factories){
        if(factory == nullptr)
            continue;

        auto product = createProduct(factory);
        if(!product){
            logNullProduct();
            continue;
        }

        if(!registerProduct(Move(product)))
            logRegisterFailure();
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetCookerAutoRegistrar::initialize(){
    return __hidden_cooker_registration::InitializeAutoFactory(
        __hidden_cooker_registration::QueryAutoCookerFactoryQueue(),
        m_factory
    );
}


void RegisterAutoCollectedAssetCookers(AssetCookerRegistry& outRegistry, AssetArena& arena){
    Alloc::ScratchArena scratchArena(AssetsArenaScope::s_RegisterCookersScratch);
    Vector<AssetCookerFactory, Alloc::ScratchArena> cookerFactories{scratchArena};
    __hidden_cooker_registration::QueryAutoCookerFactoryQueue().copyTo(cookerFactories);

    __hidden_cooker_registration::RegisterFactoryProducts(
        cookerFactories,
        [&](const AssetCookerFactory factory){ return factory(arena); },
        [&](UniquePtr<IAssetCooker> cooker){ return outRegistry.registerCooker(Move(cooker)); },
        [](){ NWB_LOGGER_ERROR(NWB_TEXT("RegisterAutoCollectedAssetCookers: cooker factory returned null cooker")); },
        [](){ NWB_LOGGER_ERROR(NWB_TEXT("RegisterAutoCollectedAssetCookers: failed to register cooker")); }
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

