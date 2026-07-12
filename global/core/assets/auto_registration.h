// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "registry.h"
#include "cooker.h"
#include <global/core/common/module.h>
#include <global/sync.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using AssetCodecFactory = UniquePtr<IAssetCodec>(*)();
using AssetCookerFactory = UniquePtr<IAssetCooker>(*)(AssetArena& arena);


// Cook-time extension points may be registered during static initialization while a cooker takes a stable snapshot.
// Keep the synchronization and duplicate handling in one place so each extension point only owns its value semantics.
template<typename ValueT>
class AutoRegistrationQueue final : NoCopy{
public:
    explicit AutoRegistrationQueue(const Name& arenaName)
        : m_arena(arenaName)
        , m_values(m_arena)
    {}

public:
    template<typename EqualT>
    void appendUnique(const ValueT value, EqualT&& equal){
        ScopedLock lock(m_mutex);
        for(const ValueT current : m_values){
            if(equal(current, value))
                return;
        }

        m_values.push_back(value);
    }

    template<typename OutputVectorT>
    void copyTo(OutputVectorT& outValues){
        static_assert(IsSame_V<typename OutputVectorT::value_type, ValueT>, "auto-registration queue value types must match");
        static_assert(IsTriviallyCopyable_V<ValueT>, "auto-registration queue values must be trivially copyable");

        ScopedLock lock(m_mutex);
        AssignTriviallyCopyableVector(outValues, m_values);
    }

private:
    AssetArena m_arena;
    Futex m_mutex;
    AssetVector<ValueT> m_values;
};


template<typename AssetCodecT>
[[nodiscard]] inline UniquePtr<IAssetCodec> CreateAssetCodec(){
    return MakeUnique<AssetCodecT>();
}

template<typename AssetCookerT>
[[nodiscard]] inline UniquePtr<IAssetCooker> CreateAssetCooker(AssetArena& arena){
    return MakeUnique<AssetCookerT>(arena);
}

class AssetCodecAutoRegistrar final : public Core::Common::Initializerable{
public:
    explicit AssetCodecAutoRegistrar(AssetCodecFactory factory)
        : m_factory(factory)
    {}


public:
    virtual bool initialize()override;
    virtual void finalize()override{}


private:
    AssetCodecFactory m_factory = nullptr;
};

class AssetCookerAutoRegistrar final : public Core::Common::Initializerable{
public:
    explicit AssetCookerAutoRegistrar(AssetCookerFactory factory)
        : m_factory(factory)
    {}


public:
    virtual bool initialize()override;
    virtual void finalize()override{}


private:
    AssetCookerFactory m_factory = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RegisterAutoCollectedAssetCodecs(AssetRegistry& outRegistry);
void RegisterAutoCollectedAssetCookers(AssetCookerRegistry& outRegistry, AssetArena& arena);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

