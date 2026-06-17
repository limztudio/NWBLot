// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "auto_registration.h"
#include "arena_names.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_auto_registration{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AutoFactoryQueue{
    AssetArena arena;
    Futex mutex;
    AssetVector<AssetCodecFactory> codecFactories;
    AssetVector<AssetCookerFactory> cookerFactories;

    AutoFactoryQueue()
        : arena(AssetsArenaScope::s_AutoFactoryQueueArena)
        , codecFactories(arena)
        , cookerFactories(arena)
    {}
};

AutoFactoryQueue& QueryAutoFactoryQueue(){
    static AutoFactoryQueue autoFactoryQueue;
    return autoFactoryQueue;
}

template<typename FactoryVector, typename FactoryT>
bool ContainsFactory(const FactoryVector& factories, const FactoryT factory){
    for(const FactoryT current : factories){
        if(current == factory)
            return true;
    }

    return false;
}

template<typename FactoryT>
using ScratchFactoryVector = Vector<FactoryT, Alloc::ScratchArena>;

template<typename FactoryT>
void CopyQueuedFactories(const AssetVector<FactoryT>& queuedFactories, ScratchFactoryVector<FactoryT>& outFactories){
    AssignTriviallyCopyableVector(outFactories, queuedFactories);
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
    if(m_factory == nullptr)
        return true;

    auto& autoFactoryQueue = __hidden_auto_registration::QueryAutoFactoryQueue();
    ScopedLock lock(autoFactoryQueue.mutex);
    if(!__hidden_auto_registration::ContainsFactory(autoFactoryQueue.codecFactories, m_factory))
        autoFactoryQueue.codecFactories.push_back(m_factory);

    return true;
}

bool AssetCookerAutoRegistrar::initialize(){
    if(m_factory == nullptr)
        return true;

    auto& autoFactoryQueue = __hidden_auto_registration::QueryAutoFactoryQueue();
    ScopedLock lock(autoFactoryQueue.mutex);
    if(!__hidden_auto_registration::ContainsFactory(autoFactoryQueue.cookerFactories, m_factory))
        autoFactoryQueue.cookerFactories.push_back(m_factory);

    return true;
}


void RegisterAutoCollectedAssetCodecs(AssetRegistry& outRegistry){
    Alloc::ScratchArena scratchArena(AssetsArenaScope::s_RegisterCodecsScratch);
    __hidden_auto_registration::ScratchFactoryVector<AssetCodecFactory> codecFactories{scratchArena};
    {
        auto& autoFactoryQueue = __hidden_auto_registration::QueryAutoFactoryQueue();
        ScopedLock lock(autoFactoryQueue.mutex);
        __hidden_auto_registration::CopyQueuedFactories(autoFactoryQueue.codecFactories, codecFactories);
    }

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
    __hidden_auto_registration::ScratchFactoryVector<AssetCookerFactory> cookerFactories{scratchArena};
    {
        auto& autoFactoryQueue = __hidden_auto_registration::QueryAutoFactoryQueue();
        ScopedLock lock(autoFactoryQueue.mutex);
        __hidden_auto_registration::CopyQueuedFactories(autoFactoryQueue.cookerFactories, cookerFactories);
    }

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

