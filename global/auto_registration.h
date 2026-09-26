// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "containers.h"
#include "name.h"
#include "sync.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Registration may race snapshots; keep sync and duplicate handling here.
template<typename ValueT, typename ArenaT>
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
    Futex m_mutex;
    ArenaT m_arena;
    Vector<ValueT, ArenaT> m_values;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename QueueT, typename FactoryT>
inline void RegisterAutoFactory(QueueT& queue, const FactoryT factory){
    if(factory == nullptr)
        return;

    queue.appendUnique(factory, [](const FactoryT lhs, const FactoryT rhs){ return lhs == rhs; });
}

// Shared auto-collection runner: snapshot the registration queue into the caller's scratch vector, reject null
// functions, run each function against the context, and report failures through the caller's log callbacks.
// Both cook-entry registration and volume preparation share this shape with different function/context/queue types.
template<typename QueueT, typename ContextT, typename FunctionVectorT, typename LogNullT, typename LogFailureT>
[[nodiscard]] inline bool RunAutoCollectedFunctions(
    QueueT& queue,
    ContextT& context,
    FunctionVectorT& scratchFunctions,
    LogNullT&& logNull,
    LogFailureT&& logFailure
){
    queue.copyTo(scratchFunctions);

    for(const auto function : scratchFunctions){
        if(!function){
            logNull();
            return false;
        }
        if(function(context))
            continue;

        logFailure();
        return false;
    }

    return true;
}


template<typename FactoryVectorT, typename CreateProductT, typename RegisterProductT, typename LogNullProductT, typename LogRegisterFailureT>
inline void RegisterAutoFactoryProducts(
    const FactoryVectorT& factories,
    CreateProductT&& createProduct,
    RegisterProductT&& registerProduct,
    LogNullProductT&& logNullProduct,
    LogRegisterFailureT&& logRegisterFailure
){
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

