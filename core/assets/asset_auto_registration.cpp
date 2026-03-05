// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset_auto_registration.h"

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets{


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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetCodecAutoRegistrar::initialize(){
    if(m_factory == nullptr)
        return true;

    auto& autoFactoryQueue = __hidden_assets::QueryAutoFactoryQueue();
    ScopedLock lock(autoFactoryQueue.mutex);
    if(!__hidden_assets::ContainsFactory(autoFactoryQueue.codecFactories, m_factory))
        autoFactoryQueue.codecFactories.push_back(m_factory);

    return true;
}

bool AssetCookerAutoRegistrar::initialize(){
    if(m_factory == nullptr)
        return true;

    auto& autoFactoryQueue = __hidden_assets::QueryAutoFactoryQueue();
    ScopedLock lock(autoFactoryQueue.mutex);
    if(!__hidden_assets::ContainsFactory(autoFactoryQueue.cookerFactories, m_factory))
        autoFactoryQueue.cookerFactories.push_back(m_factory);

    return true;
}


void RegisterAutoCollectedAssetCodecs(AssetRegistry& outRegistry){
    Vector<AssetCodecFactory> codecFactories;
    {
        auto& autoFactoryQueue = __hidden_assets::QueryAutoFactoryQueue();
        ScopedLock lock(autoFactoryQueue.mutex);
        codecFactories.reserve(autoFactoryQueue.codecFactories.size());
        for(const AssetCodecFactory factory : autoFactoryQueue.codecFactories)
            codecFactories.push_back(factory);
    }

    for(const AssetCodecFactory factory : codecFactories){
        if(factory == nullptr)
            continue;

        UniquePtr<IAssetCodec> codec = factory();
        if(!codec){
            NWB_LOGGER_ERROR(NWB_TEXT("RegisterAutoCollectedAssetCodecs: codec factory returned null codec"));
            continue;
        }

        if(!outRegistry.registerCodec(Move(codec))){
            NWB_LOGGER_ERROR(NWB_TEXT("RegisterAutoCollectedAssetCodecs: failed to register codec"));
        }
    }
}

void RegisterAutoCollectedAssetCookers(AssetCookerRegistry& outRegistry, Alloc::CustomArena& arena){
    Vector<AssetCookerFactory> cookerFactories;
    {
        auto& autoFactoryQueue = __hidden_assets::QueryAutoFactoryQueue();
        ScopedLock lock(autoFactoryQueue.mutex);
        cookerFactories.reserve(autoFactoryQueue.cookerFactories.size());
        for(const AssetCookerFactory factory : autoFactoryQueue.cookerFactories)
            cookerFactories.push_back(factory);
    }

    for(const AssetCookerFactory factory : cookerFactories){
        if(factory == nullptr)
            continue;

        UniquePtr<IAssetCooker> cooker = factory(arena);
        if(!cooker){
            NWB_LOGGER_ERROR(NWB_TEXT("RegisterAutoCollectedAssetCookers: cooker factory returned null cooker"));
            continue;
        }

        if(!outRegistry.registerCooker(Move(cooker))){
            NWB_LOGGER_ERROR(NWB_TEXT("RegisterAutoCollectedAssetCookers: failed to register cooker"));
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

