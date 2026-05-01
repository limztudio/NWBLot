// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset_auto_registration.h"

#include <core/alloc/scratch.h>
#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_asset_auto_registration{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AutoFactoryQueue{
    Futex mutex;
    Vector<AssetCodecFactory> codecFactories;
    Vector<AssetCookerFactory> cookerFactories;
};

AutoFactoryQueue& QueryAutoFactoryQueue(){
    static AutoFactoryQueue autoFactoryQueue;
    return autoFactoryQueue;
}

template<typename FactoryT>
bool ContainsFactory(const Vector<FactoryT>& factories, const FactoryT factory){
    for(const FactoryT current : factories){
        if(current == factory)
            return true;
    }

    return false;
}

template<typename FactoryT>
using ScratchFactoryVector = Vector<FactoryT, Alloc::ScratchAllocator<FactoryT>>;

template<typename FactoryT>
void CopyQueuedFactories(const Vector<FactoryT>& queuedFactories, ScratchFactoryVector<FactoryT>& outFactories){
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

    auto& autoFactoryQueue = __hidden_asset_auto_registration::QueryAutoFactoryQueue();
    ScopedLock lock(autoFactoryQueue.mutex);
    if(!__hidden_asset_auto_registration::ContainsFactory(autoFactoryQueue.codecFactories, m_factory))
        autoFactoryQueue.codecFactories.push_back(m_factory);

    return true;
}

bool AssetCookerAutoRegistrar::initialize(){
    if(m_factory == nullptr)
        return true;

    auto& autoFactoryQueue = __hidden_asset_auto_registration::QueryAutoFactoryQueue();
    ScopedLock lock(autoFactoryQueue.mutex);
    if(!__hidden_asset_auto_registration::ContainsFactory(autoFactoryQueue.cookerFactories, m_factory))
        autoFactoryQueue.cookerFactories.push_back(m_factory);

    return true;
}


void RegisterAutoCollectedAssetCodecs(AssetRegistry& outRegistry){
    Alloc::ScratchArena<> scratchArena;
    __hidden_asset_auto_registration::ScratchFactoryVector<AssetCodecFactory> codecFactories{Alloc::ScratchAllocator<AssetCodecFactory>(scratchArena)};
    {
        auto& autoFactoryQueue = __hidden_asset_auto_registration::QueryAutoFactoryQueue();
        ScopedLock lock(autoFactoryQueue.mutex);
        __hidden_asset_auto_registration::CopyQueuedFactories(autoFactoryQueue.codecFactories, codecFactories);
    }

    __hidden_asset_auto_registration::RegisterFactoryProducts(
        codecFactories,
        [](const AssetCodecFactory factory){ return factory(); },
        [&](UniquePtr<IAssetCodec> codec){ return outRegistry.registerCodec(Move(codec)); },
        [](){ NWB_LOGGER_ERROR(NWB_TEXT("RegisterAutoCollectedAssetCodecs: codec factory returned null codec")); },
        [](){ NWB_LOGGER_ERROR(NWB_TEXT("RegisterAutoCollectedAssetCodecs: failed to register codec")); }
    );
}

void RegisterAutoCollectedAssetCookers(AssetCookerRegistry& outRegistry, Alloc::CustomArena& arena){
    Alloc::ScratchArena<> scratchArena;
    __hidden_asset_auto_registration::ScratchFactoryVector<AssetCookerFactory> cookerFactories{Alloc::ScratchAllocator<AssetCookerFactory>(scratchArena)};
    {
        auto& autoFactoryQueue = __hidden_asset_auto_registration::QueryAutoFactoryQueue();
        ScopedLock lock(autoFactoryQueue.mutex);
        __hidden_asset_auto_registration::CopyQueuedFactories(autoFactoryQueue.cookerFactories, cookerFactories);
    }

    __hidden_asset_auto_registration::RegisterFactoryProducts(
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

