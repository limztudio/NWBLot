
#include "auto_registration.h"
#include "arena_names.h"

#include <global/core/alloc/scratch.h>
#include <global/core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_auto_registration{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AutoRegistrationQueue<AssetCodecFactory>& QueryAutoCodecFactoryQueue(){
    static AutoRegistrationQueue<AssetCodecFactory> queue(AssetsArenaScope::s_AutoCodecFactoryQueueArena);
    return queue;
}

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


bool AssetCodecAutoRegistrar::initialize(){
    return __hidden_auto_registration::InitializeAutoFactory(
        __hidden_auto_registration::QueryAutoCodecFactoryQueue(),
        m_factory
    );
}

bool AssetCookerAutoRegistrar::initialize(){
    return __hidden_auto_registration::InitializeAutoFactory(
        __hidden_auto_registration::QueryAutoCookerFactoryQueue(),
        m_factory
    );
}


void RegisterAutoCollectedAssetCodecs(AssetRegistry& outRegistry){
    Alloc::ScratchArena scratchArena(AssetsArenaScope::s_RegisterCodecsScratch);
    Vector<AssetCodecFactory, Alloc::ScratchArena> codecFactories{scratchArena};
    __hidden_auto_registration::QueryAutoCodecFactoryQueue().copyTo(codecFactories);

    __hidden_auto_registration::RegisterFactoryProducts(
        codecFactories,
        [](const AssetCodecFactory factory){ return factory(); },
        [&](UniquePtr<IAssetCodec> codec){ return outRegistry.registerCodec(Move(codec)); },
        [](){ NWB_LOGGER_ERROR(NWB_TEXT("RegisterAutoCollectedAssetCodecs: codec factory returned null codec")); },
        [](){ NWB_LOGGER_ERROR(NWB_TEXT("RegisterAutoCollectedAssetCodecs: failed to register codec")); }
    );
}

void RegisterAutoCollectedAssetCookers(AssetCookerRegistry& outRegistry, AssetArena& arena){
    Alloc::ScratchArena scratchArena(AssetsArenaScope::s_RegisterCookersScratch);
    Vector<AssetCookerFactory, Alloc::ScratchArena> cookerFactories{scratchArena};
    __hidden_auto_registration::QueryAutoCookerFactoryQueue().copyTo(cookerFactories);

    __hidden_auto_registration::RegisterFactoryProducts(
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

