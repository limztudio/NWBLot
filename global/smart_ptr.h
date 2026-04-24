// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "type_properties.h"


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


template<typename T, typename Deleter>
class UniquePointerType{
private:
    template<typename U>
    static typename U::pointer test(typename U::pointer*);

    template<typename U>
    static T* test(...);


public:
    typedef decltype(test<typename RemoveReference<Deleter>::type>(0)) type;
};

template<typename P1, typename P2, bool = IsScalar_V<P1> && !IsPointer_V<P1>>
struct IsArrayCvConvertible : public SmartPtrInternalDetail::IsArrayCvConvertibleImplementation<P1, P2>{};
template<typename P1, typename P2>
struct IsArrayCvConvertible<P1, P2, true> : public FalseType{};

template<typename Base, typename Derived>
struct IsDerived : public IntegralConstant<bool, IsBaseOf<Base, Derived>::value && !IsSame<typename RemoveCV<Base>::type, typename RemoveCV<Derived>::type>::value>{};

template<typename T, typename T_pointer, typename U, typename U_pointer>
struct IsSafeArrayConversion : public IntegralConstant<bool, IsConvertible<U_pointer, T_pointer>::value && IsArray<U>::value && (!IsPointer<U_pointer>::value || !IsPointer<T_pointer>::value || !IsDerived<T, typename RemoveExtent<U>::type>::value)>{};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


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
    POOL* m_pool = nullptr;

    constexpr ArenaDeleter()noexcept = default;
    constexpr ArenaDeleter(POOL& pool)noexcept : m_pool(&pool){}
    template<typename U>
    ArenaDeleter(const ArenaDeleter<U, POOL>& other, typename EnableIf<IsConvertible<U*, T*>::value>::type* = 0)noexcept
        : m_pool(other.m_pool){}

    void operator()(T* p)const noexcept{
        static_assert(IsCompleteType_V<T>, "Attempting to call the destructor of an incomplete type");
        p->~T();
        m_pool->template deallocate<T>(p, 1);
    }
};
template<typename T, typename POOL>
struct ArenaDeleter<T[], POOL>{
    POOL* m_pool = nullptr;
    usize m_size = 0;

    constexpr ArenaDeleter()noexcept = default;
    constexpr ArenaDeleter(POOL& pool, usize size)noexcept : m_pool(&pool), m_size(size){}
    template<typename U>
    ArenaDeleter(const ArenaDeleter<U[], POOL>& other, typename EnableIf<SmartPtrDetail::IsArrayCvConvertible<U*, T*>::value>::type* = 0)noexcept
        : m_pool(other.m_pool), m_size(other.m_size){}

    void operator()(T* p)const noexcept{
        for(usize i = 0; i < m_size; ++i)
            p[i].~T();
        m_pool->template deallocate<T>(p, m_size);
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

    constexpr EmptyDeleter()noexcept = default;
    constexpr EmptyDeleter(usize size)noexcept : m_size(size){}
    template<typename U>
    EmptyDeleter(const EmptyDeleter<U[]>&, typename EnableIf<SmartPtrDetail::IsArrayCvConvertible<U*, T*>::value>::type* = 0)noexcept{}

    void operator()(T* p)const noexcept{
        for(usize i = 0; i < m_size; ++i)
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
        (void)size;
    }
    template<typename U>
    BlankDeleter(const BlankDeleter<U[]>&, typename EnableIf<SmartPtrDetail::IsArrayCvConvertible<U*, T*>::value>::type* = 0)noexcept{}

    void operator()(T* p)const noexcept{
        static_assert(IsCompleteType_V<T>, "Attempting to call the destructor of an incomplete type");
        (void)p;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

