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
    void copyTo(OutputVectorT& outValues)noexcept(noexcept(AssignTriviallyCopyableVector(outValues, m_values))){
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


namespace AutoRegistrationDetail{


template<typename FactoryT>
constexpr auto MakeFactoryEqual()noexcept{
    return [](const FactoryT lhs, const FactoryT rhs)noexcept(
        IsNothrowDestructible_V<FactoryT>
        && noexcept(DeclVal<const FactoryT&>() == DeclVal<const FactoryT&>())
        && (!IsReference_V<decltype(DeclVal<const FactoryT&>() == DeclVal<const FactoryT&>())>
            || IsNothrowConvertible_V<decltype(DeclVal<const FactoryT&>() == DeclVal<const FactoryT&>()), Decay_T<decltype(DeclVal<const FactoryT&>() == DeclVal<const FactoryT&>())>>)
    ){
        return lhs == rhs;
    };
}


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename QueueT, typename FactoryT>
inline void RegisterAutoFactory(QueueT& queue, const FactoryT factory)noexcept(IsNothrowDestructible_V<FactoryT> && noexcept(static_cast<bool>(factory == nullptr)) && noexcept(queue.appendUnique(factory, AutoRegistrationDetail::MakeFactoryEqual<FactoryT>()))){
    if(factory == nullptr)
        return;

    queue.appendUnique(factory, AutoRegistrationDetail::MakeFactoryEqual<FactoryT>());
}

template<typename QueueT, typename ContextT, typename FunctionVectorT, typename LogNullT, typename LogFailureT>
[[nodiscard]] inline bool RunAutoCollectedFunctions(QueueT& queue, ContextT& context, FunctionVectorT& scratchFunctions, LogNullT&& logNull, LogFailureT&& logFailure)noexcept(
    noexcept(queue.copyTo(scratchFunctions))
    && noexcept(logNull())
    && noexcept(logFailure())
    && []()constexpr noexcept{
    using Range = FunctionVectorT;
    if constexpr(requires(Range& values){ values.begin(); values.end(); }){
        using Iterator = RemoveCVRef_T<decltype(DeclVal<Range&>().begin())>;
        using Sentinel = RemoveCVRef_T<decltype(DeclVal<Range&>().end())>;
        using Returned = decltype(*DeclVal<Iterator&>());
        using Value = Decay_T<Returned>;
        using Item = const Value&;
        return
            noexcept(DeclVal<void(*)(Iterator)noexcept>()(DeclVal<Range&>().begin()))
            && noexcept(DeclVal<void(*)(Sentinel)noexcept>()(DeclVal<Range&>().end()))
            && IsNothrowDestructible_V<Iterator>
            && IsNothrowDestructible_V<Sentinel>
            && noexcept(static_cast<bool>(DeclVal<Iterator&>() != DeclVal<Sentinel&>()))
            && noexcept(++DeclVal<Iterator&>())
            && noexcept(*DeclVal<Iterator&>())
            && (!IsReference_V<Returned> || IsNothrowConvertible_V<Returned, Value>)
            && IsNothrowDestructible_V<Value>
            && noexcept(static_cast<bool>(!DeclVal<Item>()))
            && noexcept(static_cast<bool>(DeclVal<Item>()(DeclVal<ContextT&>())))
        ;
    }
    else
        return false;
}()
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
inline void RegisterAutoFactoryProducts(const FactoryVectorT& factories, CreateProductT&& createProduct, RegisterProductT&& registerProduct, LogNullProductT&& logNullProduct, LogRegisterFailureT&& logRegisterFailure)noexcept(
    noexcept(logNullProduct())
    && noexcept(logRegisterFailure())
    && []()constexpr noexcept{
    using Range = const FactoryVectorT;
    if constexpr(requires(Range& values){ values.begin(); values.end(); }){
        using Iterator = RemoveCVRef_T<decltype(DeclVal<Range&>().begin())>;
        using Sentinel = RemoveCVRef_T<decltype(DeclVal<Range&>().end())>;
        using Returned = decltype(*DeclVal<Iterator&>());
        using Value = Decay_T<Returned>;
        using Item = const Value&;
        return
            noexcept(DeclVal<void(*)(Iterator)noexcept>()(DeclVal<Range&>().begin()))
            && noexcept(DeclVal<void(*)(Sentinel)noexcept>()(DeclVal<Range&>().end()))
            && IsNothrowDestructible_V<Iterator>
            && IsNothrowDestructible_V<Sentinel>
            && noexcept(static_cast<bool>(DeclVal<Iterator&>() != DeclVal<Sentinel&>()))
            && noexcept(++DeclVal<Iterator&>())
            && noexcept(*DeclVal<Iterator&>())
            && (!IsReference_V<Returned> || IsNothrowConvertible_V<Returned, Value>)
            && IsNothrowDestructible_V<Value>
            && noexcept(static_cast<bool>(DeclVal<Item>() == nullptr))
            && []()constexpr noexcept{
                using ReturnedProduct = decltype(DeclVal<CreateProductT&>()(DeclVal<Item>()));
                using Product = Decay_T<ReturnedProduct>;
                return
                    noexcept(DeclVal<CreateProductT&>()(DeclVal<Item>()))
                    && (!IsReference_V<ReturnedProduct> || IsNothrowConvertible_V<ReturnedProduct, Product>)
                    && IsNothrowDestructible_V<Product>
                    && noexcept(static_cast<bool>(!DeclVal<Product&>()))
                    && noexcept(static_cast<bool>(!DeclVal<RegisterProductT&>()(DeclVal<Product&&>())))
                ;
            }()
        ;
    }
    else
        return false;
}()
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

