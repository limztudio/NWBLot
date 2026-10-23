// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "type.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T, typename POOL>
struct ArenaDeleter;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SmartPtrInternalDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename P1, typename P2, bool = IsSame_V<RemoveCV_T<typename PointerTraits<P1>::element_type>, RemoveCV_T<typename PointerTraits<P2>::element_type>>>
struct IsArrayCvConvertibleImplementation : public IsConvertible<P1, P2>{};
template<typename P1, typename P2>
struct IsArrayCvConvertibleImplementation<P1, P2, false> : public FalseType{};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SmartPtrDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Source, typename Target>
struct IsArenaDeleterConversion : FalseType{};
template<typename U, typename T, typename POOL>
struct IsArenaDeleterConversion<ArenaDeleter<U, POOL>, ArenaDeleter<T, POOL>> : IntegralConstant<bool, !IsArray_V<U> && !IsArray_V<T> && !IsSame_V<RemoveCV_T<U>, RemoveCV_T<T>> && IsConvertible_V<U*, T*>>{};

template<typename Source, typename Target>
inline constexpr bool IsArenaDeleterConversion_V = IsArenaDeleterConversion<RemoveCVRef_T<Source>, RemoveCVRef_T<Target>>::value;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T, typename Deleter>
class UniquePointerType{
private:
    struct PointerProbe{
        template<typename U>
        static typename U::pointer Test(typename U::pointer*);
        template<typename U>
        static T* Test(...);
    };


public:
    typedef decltype(PointerProbe::template Test<typename RemoveReference<Deleter>::type>(0)) type;
};

template<typename P1, typename P2, bool = IsScalar_V<P1> && !IsPointer_V<P1>>
struct IsArrayCvConvertible : public SmartPtrInternalDetail::IsArrayCvConvertibleImplementation<P1, P2>{};
template<typename P1, typename P2>
struct IsArrayCvConvertible<P1, P2, true> : public FalseType{};

template<typename Base, typename Derived>
struct IsDerived : public IntegralConstant<bool, IsBaseOf<Base, Derived>::value && !IsSame<typename RemoveCV<Base>::type, typename RemoveCV<Derived>::type>::value>{};

template<typename T, typename TPointer, typename U, typename UPointer>
struct IsSafeArrayConversion : public IntegralConstant<bool, IsConvertible<UPointer, TPointer>::value && IsArray<U>::value && (!IsPointer<UPointer>::value || !IsPointer<TPointer>::value || !IsDerived<T, typename RemoveExtent<U>::type>::value)>{};

template<typename OwnerA, typename OwnerB>
using OwnerPointerCompareResult_T = CompareThreeWayResult_T<typename OwnerA::pointer, typename OwnerB::pointer>;

template<typename OwnerA, typename OwnerB>
[[nodiscard]] inline bool OwnerPointerEqual(const OwnerA& a, const OwnerB& b)noexcept(
    IsPointer_V<typename OwnerA::pointer>
    && IsPointer_V<typename OwnerB::pointer>
    && IsSame_V<decltype(a.get()), typename OwnerA::pointer>
    && IsSame_V<decltype(b.get()), typename OwnerB::pointer>
    && noexcept(a.get())
    && noexcept(b.get())
){
    return a.get() == b.get();
}

template<typename OwnerA, typename OwnerB>
[[nodiscard]] inline OwnerPointerCompareResult_T<OwnerA, OwnerB> OwnerPointerCompare(const OwnerA& a, const OwnerB& b)noexcept(
    IsPointer_V<typename OwnerA::pointer>
    && IsPointer_V<typename OwnerB::pointer>
    && IsSame_V<decltype(a.get()), typename OwnerA::pointer>
    && IsSame_V<decltype(b.get()), typename OwnerB::pointer>
    && noexcept(a.get())
    && noexcept(b.get())
){
    return a.get() <=> b.get();
}

template<typename OwnerA, typename OwnerB>
[[nodiscard]] inline bool OwnerPointerLess(const OwnerA& a, const OwnerB& b)noexcept(
    IsPointer_V<typename OwnerA::pointer>
    && IsPointer_V<typename OwnerB::pointer>
    && IsSame_V<decltype(a.get()), typename OwnerA::pointer>
    && IsSame_V<decltype(b.get()), typename OwnerB::pointer>
    && noexcept(a.get())
    && noexcept(b.get())
    && IsPointer_V<typename CommonType<typename OwnerA::pointer, typename OwnerB::pointer>::type>
    && noexcept(static_cast<bool>(LessThan<typename CommonType<typename OwnerA::pointer, typename OwnerB::pointer>::type>{}(a.get(), b.get())))
){
    typedef typename OwnerA::pointer P1;
    typedef typename OwnerB::pointer P2;
    typedef typename CommonType<P1, P2>::type PCommon;
    PCommon pT1 = a.get();
    PCommon pT2 = b.get();
    return LessThan<PCommon>()(pT1, pT2);
}

template<typename Owner>
[[nodiscard]] inline bool OwnerPointerLessNull(const Owner& a)noexcept(
    IsPointer_V<typename Owner::pointer>
    && IsSame_V<decltype(a.get()), typename Owner::pointer>
    && noexcept(a.get())
    && noexcept(static_cast<bool>(LessThan<typename Owner::pointer>{}(a.get(), nullptr)))
){
    typedef typename Owner::pointer pointer;
    return LessThan<pointer>()(a.get(), nullptr);
}

template<typename Owner>
[[nodiscard]] inline bool NullLessOwnerPointer(const Owner& b)noexcept(
    IsPointer_V<typename Owner::pointer>
    && IsSame_V<decltype(b.get()), typename Owner::pointer>
    && noexcept(b.get())
    && noexcept(static_cast<bool>(LessThan<typename Owner::pointer>{}(nullptr, b.get())))
){
    typedef typename Owner::pointer pointer;
    pointer pT = b.get();
    return LessThan<pointer>()(nullptr, pT);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_SMART_PTR_COMPARISON_OPERATORS(OwnerType) \
template<typename T1, typename D1, typename T2, typename D2> \
inline bool operator==(const OwnerType<T1, D1>& a, const OwnerType<T2, D2>& b)noexcept(noexcept(SmartPtrDetail::OwnerPointerEqual(a, b))){ return SmartPtrDetail::OwnerPointerEqual(a, b); } \
template<typename T1, typename D1, typename T2, typename D2> \
requires ThreeWayComparableWith<typename OwnerType<T1, D1>::pointer, typename OwnerType<T2, D2>::pointer> \
inline CompareThreeWayResult_T<typename OwnerType<T1, D1>::pointer, typename OwnerType<T2, D2>::pointer> \
operator<=>(const OwnerType<T1, D1>& a, const OwnerType<T2, D2>& b)noexcept(noexcept(SmartPtrDetail::OwnerPointerCompare(a, b))){ return SmartPtrDetail::OwnerPointerCompare(a, b); } \
template<typename T1, typename D1, typename T2, typename D2> \
inline bool operator<(const OwnerType<T1, D1>& a, const OwnerType<T2, D2>& b)noexcept(noexcept(SmartPtrDetail::OwnerPointerLess(a, b))){ return SmartPtrDetail::OwnerPointerLess(a, b); } \
template<typename T1, typename D1, typename T2, typename D2> \
inline bool operator>(const OwnerType<T1, D1>& a, const OwnerType<T2, D2>& b)noexcept(noexcept(DeclVal<void(*)(bool)noexcept>()((b < a)))){ return (b < a); } \
template<typename T1, typename D1, typename T2, typename D2> \
inline bool operator<=(const OwnerType<T1, D1>& a, const OwnerType<T2, D2>& b)noexcept(noexcept(DeclVal<void(*)(bool)noexcept>()(!(b < a)))){ return !(b < a); } \
template<typename T1, typename D1, typename T2, typename D2> \
inline bool operator>=(const OwnerType<T1, D1>& a, const OwnerType<T2, D2>& b)noexcept(noexcept(DeclVal<void(*)(bool)noexcept>()(!(a < b)))){ return !(a < b); } \
template<typename T, typename D> \
inline bool operator==(const OwnerType<T, D>& a, std::nullptr_t)noexcept(noexcept(DeclVal<void(*)(bool)noexcept>()(!a))){ return !a; } \
template<typename T, typename D> \
requires ThreeWayComparableWith<typename OwnerType<T, D>::pointer, std::nullptr_t> \
inline CompareThreeWayResult_T<typename OwnerType<T, D>::pointer, std::nullptr_t> operator<=>(const OwnerType<T, D>& a, std::nullptr_t)noexcept(IsPointer_V<typename OwnerType<T, D>::pointer> && noexcept(a.get())){ return a.get() <=> nullptr; } \
template<typename T, typename D> \
inline bool operator<(const OwnerType<T, D>& a, std::nullptr_t)noexcept(noexcept(SmartPtrDetail::OwnerPointerLessNull(a))){ return SmartPtrDetail::OwnerPointerLessNull(a); } \
template<typename T, typename D> \
inline bool operator<(std::nullptr_t, const OwnerType<T, D>& b)noexcept(noexcept(SmartPtrDetail::NullLessOwnerPointer(b))){ return SmartPtrDetail::NullLessOwnerPointer(b); } \
template<typename T, typename D> \
inline bool operator>(const OwnerType<T, D>& a, std::nullptr_t)noexcept(noexcept(DeclVal<void(*)(bool)noexcept>()((nullptr < a)))){ return (nullptr < a); } \
template<typename T, typename D> \
inline bool operator>(std::nullptr_t, const OwnerType<T, D>& b)noexcept(noexcept(DeclVal<void(*)(bool)noexcept>()((b < nullptr)))){ return (b < nullptr); } \
template<typename T, typename D> \
inline bool operator<=(const OwnerType<T, D>& a, std::nullptr_t)noexcept(noexcept(DeclVal<void(*)(bool)noexcept>()(!(nullptr < a)))){ return !(nullptr < a); } \
template<typename T, typename D> \
inline bool operator<=(std::nullptr_t, const OwnerType<T, D>& b)noexcept(noexcept(DeclVal<void(*)(bool)noexcept>()(!(b < nullptr)))){ return !(b < nullptr); } \
template<typename T, typename D> \
inline bool operator>=(const OwnerType<T, D>& a, std::nullptr_t)noexcept(noexcept(DeclVal<void(*)(bool)noexcept>()(!(a < nullptr)))){ return !(a < nullptr); } \
template<typename T, typename D> \
inline bool operator>=(std::nullptr_t, const OwnerType<T, D>& b)noexcept(noexcept(DeclVal<void(*)(bool)noexcept>()(!(nullptr < b)))){ return !(nullptr < b); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
struct DefaultDeleter{
    constexpr DefaultDeleter()noexcept = default;
    template<typename U>
    DefaultDeleter(const DefaultDeleter<U>&, typename EnableIf<IsConvertible<U*, T*>::value>::type* = 0)noexcept{}

    void operator()(T* p)const noexcept{
        static_assert(IsCompleteType_V<T>, "Attempting to call the destructor of an incomplete type");
        delete p;
    }
};
template<typename T>
struct DefaultDeleter<T[]>{
    constexpr DefaultDeleter()noexcept = default;
    template<typename U>
    DefaultDeleter(const DefaultDeleter<U[]>&, typename EnableIf<SmartPtrDetail::IsArrayCvConvertible<U*, T*>::value>::type* = 0)noexcept{}

    void operator()(T* p)const noexcept{
        delete[] p;
    }
};

template<typename T, typename POOL>
struct ArenaDeleter{
    template<typename, typename> friend struct ArenaDeleter;


private:
    using DeallocateFunction = void (*)(POOL& pool, void* allocation)noexcept;


public:
    POOL* m_pool = nullptr;


private:
    DeallocateFunction m_deallocate = nullptr;
    usize m_offset = 0u;


private:
    static consteval bool CanDeallocate()noexcept{
        return requires(POOL& pool, void* allocation){ pool.template deallocate<T>(static_cast<T*>(allocation), 1); } || requires(POOL& pool, void* allocation){ pool.template deallocate<T>(static_cast<RemoveCV_T<T>*>(allocation), 1); };
    }

    static void Deallocate(POOL& pool, void* allocation)noexcept{
        if constexpr(requires{ pool.template deallocate<T>(static_cast<T*>(allocation), 1); })
            pool.template deallocate<T>(static_cast<T*>(allocation), 1);
        else
            pool.template deallocate<T>(static_cast<RemoveCV_T<T>*>(allocation), 1);
    }


public:
    constexpr ArenaDeleter()noexcept = default;
    constexpr ArenaDeleter(POOL& pool)noexcept
        : m_pool(&pool)
    {}
    template<typename U>
    ArenaDeleter(const ArenaDeleter<U, POOL>& other, typename EnableIf<IsConvertible<U*, T*>::value && IsSame_V<RemoveCV_T<U>, RemoveCV_T<T>>>::type* = 0)noexcept
        : m_pool(other.m_pool)
        , m_deallocate(other.m_deallocate)
        , m_offset(other.m_offset)
    {}
    // Cross-type ownership needs the live source pointer before its base adjustment is erased.
    template<typename U>
    ArenaDeleter(const ArenaDeleter<U, POOL>& other, TypeIdentity<U*> source, typename EnableIf<IsConvertible<U*, T*>::value && !IsSame_V<RemoveCV_T<U>, RemoveCV_T<T>>>::type* = 0)noexcept
        : m_pool(other.m_pool)
    {
        if(source){
            m_deallocate = other.m_deallocate;
            // Callable source policies retain the allocation type; other private pools keep their existing target-operator contract.
            if(!m_deallocate){
                if constexpr(ArenaDeleter<U, POOL>::CanDeallocate())
                    m_deallocate = &ArenaDeleter<U, POOL>::Deallocate;
            }
            if(m_deallocate){
                const usize allocation = reinterpret_cast<usize>(source) - other.m_offset;
                m_offset = reinterpret_cast<usize>(static_cast<T*>(source)) - allocation;
            }
        }
    }


public:
    // Raw reset or copied-functor reuse must match the retained concrete public-pool allocation policy.
    void operator()(T* p)const noexcept{
        static_assert(IsCompleteType_V<T>, "Attempting to call the destructor of an incomplete type");
        // Destruction can reenter a surviving owner and replace its deleter.
        POOL* const pool = m_pool;
        const DeallocateFunction deallocate = m_deallocate;
        void* const allocation = reinterpret_cast<void*>(reinterpret_cast<usize>(p) - m_offset);
        p->~T();
        if(deallocate)
            deallocate(*pool, allocation);
        else{
            if constexpr(requires{ pool->template deallocate<T>(p, 1); })
                pool->template deallocate<T>(p, 1);
            else
                pool->template deallocate<T>(const_cast<RemoveCV_T<T>*>(p), 1);
        }
    }
};
template<typename T, typename POOL>
struct ArenaDeleter<T[], POOL>{
    POOL* m_pool = nullptr;
    usize m_size = 0;


public:
    constexpr ArenaDeleter()noexcept = default;

    constexpr ArenaDeleter(POOL& pool, usize size)noexcept
        : m_pool(&pool)
        , m_size(size)
    {}
    template<typename U>
    ArenaDeleter(const ArenaDeleter<U[], POOL>& other, typename EnableIf<SmartPtrDetail::IsArrayCvConvertible<U*, T*>::value>::type* = 0)noexcept
        : m_pool(other.m_pool)
        , m_size(other.m_size)
    {}


public:
    void operator()(T* p)const noexcept{
        POOL* const pool = m_pool;
        const usize size = m_size;
        for(usize i = 0u; i < size; ++i)
            p[i].~T();
        pool->template deallocate<T>(p, size);
    }
};

template<typename T>
struct EmptyDeleter{
    constexpr EmptyDeleter()noexcept = default;
    template<typename U>
    EmptyDeleter(const EmptyDeleter<U>&, typename EnableIf<IsConvertible<U*, T*>::value>::type* = 0)noexcept{}

    void operator()(T* p)const noexcept{
        static_assert(IsCompleteType_V<T>, "Attempting to call the destructor of an incomplete type");
        p->~T();
    }
};
template<typename T>
struct EmptyDeleter<T[]>{
    usize m_size = 0;


public:
    constexpr EmptyDeleter()noexcept = default;

    constexpr EmptyDeleter(usize size)noexcept
        : m_size(size)
    {}
    template<typename U>
    EmptyDeleter(const EmptyDeleter<U[]>& other, typename EnableIf<SmartPtrDetail::IsArrayCvConvertible<U*, T*>::value>::type* = 0)noexcept
        : m_size(other.m_size)
    {}

    void operator()(T* p)const noexcept{
        const usize size = m_size;
        for(usize i = 0u; i < size; ++i)
            p[i].~T();
    }
};

template<typename T>
struct BlankDeleter{
    constexpr BlankDeleter()noexcept = default;
    template<typename U>
    BlankDeleter(const BlankDeleter<U>&, typename EnableIf<IsConvertible<U*, T*>::value>::type* = 0)noexcept{}

    void operator()(T*)const noexcept{
        static_assert(IsCompleteType_V<T>, "Attempting to call the destructor of an incomplete type");
    }
};
template<typename T>
struct BlankDeleter<T[]>{
    constexpr BlankDeleter()noexcept = default;

    constexpr BlankDeleter(usize size)noexcept{
        static_cast<void>(size);
    }
    template<typename U>
    BlankDeleter(const BlankDeleter<U[]>&, typename EnableIf<SmartPtrDetail::IsArrayCvConvertible<U*, T*>::value>::type* = 0)noexcept{}

    void operator()(T* p)const noexcept{
        static_assert(IsCompleteType_V<T>, "Attempting to call the destructor of an incomplete type");
        static_cast<void>(p);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

