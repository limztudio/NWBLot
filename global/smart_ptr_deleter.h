// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "type.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SmartPtrInternalDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename P1, typename P2, b8 = IsSame_V<RemoveCV_T<typename PointerTraits<P1>::element_type>, RemoveCV_T<typename PointerTraits<P2>::element_type>>>
struct IsArrayCvConvertibleImplementation : public IsConvertible<P1, P2>{};
template<typename P1, typename P2>
struct IsArrayCvConvertibleImplementation<P1, P2, false> : public FalseType{};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


namespace SmartPtrDetail{


template<typename P1, typename P2, b8 = IsScalar_V<P1> && !IsPointer_V<P1>>
struct IsArrayCvConvertible : public SmartPtrInternalDetail::IsArrayCvConvertibleImplementation<P1, P2>{};
template<typename P1, typename P2>
struct IsArrayCvConvertible<P1, P2, true> : public FalseType{};


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
    template<typename, typename> friend struct ArenaDeleter;


private:
    using DeallocateFunction = void (*)(POOL& pool, void* allocation)noexcept;


public:
    POOL* pool = nullptr;


private:
    DeallocateFunction m_deallocate = nullptr;
    usize m_offset = 0u;


private:
    static consteval b8 CanDeallocate()noexcept{
        return requires(POOL& inPool, void* allocation){ inPool.template deallocate<T>(static_cast<T*>(allocation), 1); } || requires(POOL& inPool, void* allocation){ inPool.template deallocate<T>(static_cast<RemoveCV_T<T>*>(allocation), 1); };
    }

    static void Deallocate(POOL& inPool, void* allocation)noexcept{
        if constexpr(requires{ inPool.template deallocate<T>(static_cast<T*>(allocation), 1); })
            inPool.template deallocate<T>(static_cast<T*>(allocation), 1);
        else
            inPool.template deallocate<T>(static_cast<RemoveCV_T<T>*>(allocation), 1);
    }


public:
    constexpr ArenaDeleter()noexcept = default;
    constexpr ArenaDeleter(POOL& inPool)noexcept
        : pool(&inPool)
    {}
    template<typename U>
    ArenaDeleter(const ArenaDeleter<U, POOL>& other, typename EnableIf<IsConvertible<U*, T*>::value && IsSame_V<RemoveCV_T<U>, RemoveCV_T<T>>>::type* = 0)noexcept
        : pool(other.pool)
        , m_deallocate(other.m_deallocate)
        , m_offset(other.m_offset)
    {}
    // Cross-type ownership needs the live source pointer before its base adjustment is erased.
    template<typename U>
    ArenaDeleter(const ArenaDeleter<U, POOL>& other, TypeIdentity<U*> source, typename EnableIf<IsConvertible<U*, T*>::value && !IsSame_V<RemoveCV_T<U>, RemoveCV_T<T>>>::type* = 0)noexcept
        : pool(other.pool)
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
        POOL* const targetPool = pool;
        const DeallocateFunction deallocate = m_deallocate;
        void* const allocation = reinterpret_cast<void*>(reinterpret_cast<usize>(p) - m_offset);
        p->~T();
        if(deallocate)
            deallocate(*targetPool, allocation);
        else{
            if constexpr(requires{ targetPool->template deallocate<T>(p, 1); })
                targetPool->template deallocate<T>(p, 1);
            else
                targetPool->template deallocate<T>(const_cast<RemoveCV_T<T>*>(p), 1);
        }
    }
};
template<typename T, typename POOL>
struct ArenaDeleter<T[], POOL>{
    POOL* pool = nullptr;
    usize size = 0;


public:
    constexpr ArenaDeleter()noexcept = default;
    constexpr ArenaDeleter(POOL& inPool, usize inSize)noexcept
        : pool(&inPool)
        , size(inSize)
    {}
    template<typename U>
    ArenaDeleter(const ArenaDeleter<U[], POOL>& other, typename EnableIf<SmartPtrDetail::IsArrayCvConvertible<U*, T*>::value>::type* = 0)noexcept
        : pool(other.pool)
        , size(other.size)
    {}


public:
    void operator()(T* p)const noexcept{
        POOL* const targetPool = pool;
        const usize count = size;
        for(usize i = 0u; i < count; ++i)
            p[i].~T();
        targetPool->template deallocate<T>(p, count);
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
    usize size = 0;


public:
    constexpr EmptyDeleter()noexcept = default;
    constexpr EmptyDeleter(usize inSize)noexcept
        : size(inSize)
    {}
    template<typename U>
    EmptyDeleter(const EmptyDeleter<U[]>& other, typename EnableIf<SmartPtrDetail::IsArrayCvConvertible<U*, T*>::value>::type* = 0)noexcept
        : size(other.size)
    {}

    void operator()(T* p)const noexcept{
        const usize count = size;
        for(usize i = 0u; i < count; ++i)
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
    constexpr BlankDeleter([[maybe_unused]] usize size)noexcept{}
    template<typename U>
    BlankDeleter(const BlankDeleter<U[]>&, typename EnableIf<SmartPtrDetail::IsArrayCvConvertible<U*, T*>::value>::type* = 0)noexcept{}

    void operator()([[maybe_unused]] T* p)const noexcept{
        static_assert(IsCompleteType_V<T>, "Attempting to call the destructor of an incomplete type");
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

