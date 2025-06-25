// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "type_properties.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_internal_smart_ptr{
    template <typename P1, typename P2, bool = IsSame_V<RemoveCV_T<typename PointerTraits<P1>::element_type>, RemoveCV_T<typename PointerTraits<P2>::element_type>>>
    struct IsArrayCvConvertibleImplementation : public IsConvertible<P1, P2>{};
    template <typename P1, typename P2>
    struct IsArrayCvConvertibleImplementation<P1, P2, false> : public FalseType{};
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_smart_ptr{
    template <typename T, typename Deleter>
    class UniquePointerType{
    private:
        template <typename U>
        static typename U::pointer test(typename U::pointer*);

        template <typename U>
        static T* test(...);


    public:
        typedef decltype(test<typename RemoveReference<Deleter>::type>(0)) type;
    };

    template <typename P1, typename P2, bool = IsScalar_V<P1> && !IsPointer_V<P1>>
    struct IsArrayCvConvertible : public __hidden_internal_smart_ptr::IsArrayCvConvertibleImplementation<P1, P2>{};
    template <typename P1, typename P2>
    struct IsArrayCvConvertible<P1, P2, true> : public FalseType{};

    template <typename Base, typename Derived>
    struct IsDerived : public IntegralConstant<bool, IsBaseOf<Base, Derived>::value && !IsSame<typename RemoveCV<Base>::type, typename RemoveCV<Derived>::type>::value>{};

    template <typename T, typename T_pointer, typename U, typename U_pointer>
    struct IsSafeArrayConversion : public IntegralConstant<bool, IsConvertible<U_pointer, T_pointer>::value && IsArray<U>::value && (!IsPointer<U_pointer>::value || !IsPointer<T_pointer>::value || !IsDerived<T, typename RemoveExtent<U>::type>::value)>{};
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <typename T>
struct DefaultDeleter{
    constexpr DefaultDeleter()noexcept = default;
	template <typename U>
    DefaultDeleter(const DefaultDeleter<U>&, typename EnableIf<IsConvertible<U*, T*>::value>::type* = 0)noexcept{}

	void operator()(T* p)const noexcept{
		static_assert(IsCompleteType_V<T>, "Attempting to call the destructor of an incomplete type");
		delete p;
	}
};
template <typename T>
struct DefaultDeleter<T[]>{
    constexpr DefaultDeleter()noexcept = default;
	template <typename U>
    DefaultDeleter(const DefaultDeleter<U[]>&, typename EnableIf<__hidden_smart_ptr::IsArrayCvConvertible<U*, T*>::value>::type* = 0)noexcept{}

	void operator()(T* p)const noexcept{
		delete[] p;
	}
};

template <typename T, typename POOL>
struct ArenaDeleter{
    constexpr ArenaDeleter()noexcept = default;
    constexpr ArenaDeleter(POOL& pool)noexcept : mPool(&pool){}
    template <typename U, typename _POOL>
    ArenaDeleter(const ArenaDeleter<U, _POOL>&, typename EnableIf<IsConvertible<U*, T*>::value>::type* = 0)noexcept{}

    void operator()(T* p)const noexcept{
        static_assert(IsCompleteType_V<T>, "Attempting to call the destructor of an incomplete type");
        p->~T();
        mPool->template deallocate<T>(1);
    }

    POOL* mPool = nullptr;
};
template <typename T, typename POOL>
struct ArenaDeleter<T[], POOL>{
    constexpr ArenaDeleter()noexcept = default;
    constexpr ArenaDeleter(POOL& pool, usize size)noexcept : mPool(&pool), mSize(size){}
    template <typename U, typename _POOL>
    ArenaDeleter(const ArenaDeleter<U[], _POOL>&, typename EnableIf<__hidden_smart_ptr::IsArrayCvConvertible<U*, T*>::value>::type* = 0)noexcept{}

    void operator()(T* p)const noexcept{
        for(usize i = 0; i < mSize; ++i)
            p[i].~T();
        mPool->template deallocate<T>(mSize);
    }

    POOL* mPool = nullptr;
    usize mSize = 0;
};

template <typename T>
struct EmptyDeleter{
    constexpr EmptyDeleter()noexcept = default;
    template <typename U>
    EmptyDeleter(const EmptyDeleter<U>&, typename EnableIf<IsConvertible<U*, T*>::value>::type* = 0)noexcept{}

    void operator()(T* p)const noexcept{
        static_assert(IsCompleteType_V<T>, "Attempting to call the destructor of an incomplete type");
        p->~T();
    }
};
template <typename T>
struct EmptyDeleter<T[]>{
    constexpr EmptyDeleter()noexcept = default;
    constexpr EmptyDeleter(usize size)noexcept : mSize(size){}
    template <typename U>
    EmptyDeleter(const EmptyDeleter<U[]>&, typename EnableIf<__hidden_smart_ptr::IsArrayCvConvertible<U*, T*>::value>::type* = 0)noexcept{}

    void operator()(T* p)const noexcept{
        for(usize i = 0; i < mSize; ++i)
            p[i].~T();
    }

    usize mSize = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

