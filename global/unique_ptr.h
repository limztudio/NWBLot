// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "assert.h"
#include "compressed_pair.h"
#include "smart_ptr.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UniquePtrDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T, typename Deleter>
class Storage{
public:
    typedef Deleter deleter_type;
    typedef T element_type;
    typedef typename SmartPtrDetail::UniquePointerType<element_type, deleter_type>::type pointer;


protected:
    constexpr Storage()noexcept : mPair(pointer()){}
    explicit Storage(pointer pValue)noexcept : mPair(pValue){}
    template<typename DeleterArg>
    Storage(pointer pValue, DeleterArg&& deleter)noexcept : mPair(pValue, Forward<DeleterArg>(deleter)){}


public:
    void reset(pointer pValue = pointer())noexcept{
        if(pValue != mPair.first()){
            if(auto first = std::exchange(mPair.first(), pValue))
                get_deleter()(first);
        }
    }

    pointer release()noexcept{
        pointer const pTemp = mPair.first();
        mPair.first() = pointer();
        return pTemp;
    }
    pointer detach()noexcept{ return release(); }

    pointer get()const noexcept{ return mPair.first(); }

    deleter_type& get_deleter()noexcept{ return mPair.second(); }
    const deleter_type& get_deleter()const noexcept{ return mPair.second(); }


protected:
    void swapStorage(Storage& x)noexcept{ mPair.swap(x.mPair); }


private:
    CompressedPair<pointer, deleter_type> mPair;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T, typename Deleter = DefaultDeleter<T>>
class UniquePtr : private UniquePtrDetail::Storage<T, Deleter>{
    static_assert(!IsRValueReference<Deleter>::value, "The supplied Deleter cannot be a r-value reference.");
public:
    typedef Deleter deleter_type;
    typedef T element_type;
    typedef UniquePtr<element_type, deleter_type> this_type;
    typedef typename SmartPtrDetail::UniquePointerType<element_type, deleter_type>::type pointer;
    typedef UniquePtrDetail::Storage<element_type, deleter_type> base_type;


public:
    constexpr UniquePtr()noexcept : base_type(pointer()){
        static_assert(!IsPointer<deleter_type>::value, "UniquePtr deleter default-constructed with null pointer. Use a different constructor or change your deleter to a class.");
    }
    constexpr UniquePtr(std::nullptr_t)noexcept : base_type(pointer()){
        static_assert(!IsPointer<deleter_type>::value, "UniquePtr deleter default-constructed with null pointer. Use a different constructor or change your deleter to a class.");
    }
    explicit UniquePtr(pointer pValue)noexcept : base_type(pValue){
        static_assert(!IsPointer<deleter_type>::value, "UniquePtr deleter default-constructed with null pointer. Use a different constructor or change your deleter to a class.");
    }
    UniquePtr(pointer pValue, typename Conditional<IsLValueReference<deleter_type>::value, deleter_type, typename AddLValueReference<const deleter_type>::type>::type deleter)noexcept : base_type(pValue, deleter){}
    UniquePtr(pointer pValue, typename RemoveReference<deleter_type>::type&& deleter)noexcept : base_type(pValue, Move(deleter)){
        static_assert(!IsLValueReference<deleter_type>::value, "deleter_type reference refers to an rvalue deleter. The reference will probably become invalid before used. Change the deleter_type to not be a reference or construct with permanent deleter.");
    }
    UniquePtr(this_type&& x)noexcept : base_type(x.release(), Forward<deleter_type>(x.get_deleter())){}
    template<typename U, typename E>
    UniquePtr(UniquePtr<U, E>&& u, typename EnableIf<!IsArray<U>::value&& IsConvertible<typename UniquePtr<U, E>::pointer, pointer>::value&& IsConvertible<E, deleter_type>::value && (IsSame<deleter_type, E>::value || !IsLValueReference<deleter_type>::value)>::type* = 0)noexcept : base_type(u.release(), Forward<E>(u.get_deleter())){}
    UniquePtr(const this_type&) = delete;
    UniquePtr& operator=(const this_type&) = delete;
    UniquePtr& operator=(pointer pValue) = delete;

    ~UniquePtr()noexcept{ reset(); }


public:
    this_type& operator=(this_type&& x)noexcept{
        reset(x.release());
        get_deleter() = Move(Forward<deleter_type>(x.get_deleter()));
        return *this;
    }
    template<typename U, typename E>
    typename EnableIf<!IsArray<U>::value&& IsConvertible<typename UniquePtr<U, E>::pointer, pointer>::value&& IsAssignable<deleter_type&, E&&>::value, this_type&>::type operator=(UniquePtr<U, E>&& u)noexcept{
        reset(u.release());
        get_deleter() = Move(Forward<E>(u.get_deleter()));
        return *this;
    }
    this_type& operator=(std::nullptr_t)noexcept{
        reset();
        return *this;
    }

    typename AddLValueReference<T>::type operator*()const{ return *get(); }
    pointer operator->()const noexcept{ return get(); }

    explicit operator bool()const noexcept{ return (get() != pointer()); }


public:
    using base_type::reset;
    using base_type::release;
    using base_type::detach;
    using base_type::get;
    using base_type::get_deleter;

    void swap(this_type& x)noexcept{ base_type::swapStorage(x); }
};
template<typename T, typename Deleter>
class UniquePtr<T[], Deleter> : private UniquePtrDetail::Storage<T, Deleter>{
public:
    typedef Deleter deleter_type;
    typedef T element_type;
    typedef UniquePtr<element_type[], deleter_type> this_type;
    typedef typename SmartPtrDetail::UniquePointerType<element_type, deleter_type>::type pointer;
    typedef UniquePtrDetail::Storage<element_type, deleter_type> base_type;


public:
    constexpr UniquePtr()noexcept : base_type(pointer()){
        static_assert(!IsPointer<deleter_type>::value, "UniquePtr deleter default-constructed with null pointer. Use a different constructor or change your deleter to a class.");
    }
    constexpr UniquePtr(std::nullptr_t)noexcept : base_type(pointer()){
        static_assert(!IsPointer<deleter_type>::value, "UniquePtr deleter default-constructed with null pointer. Use a different constructor or change your deleter to a class.");
    }
    template<typename P, typename = EnableIf_T<SmartPtrDetail::IsArrayCvConvertible<P, pointer>::value>>
    explicit UniquePtr(P pArray)noexcept : base_type(pArray){
        static_assert(!IsPointer<deleter_type>::value, "UniquePtr deleter default-constructed with null pointer. Use a different constructor or change your deleter to a class.");
    }
    template<typename P>
    UniquePtr(P pArray, typename Conditional<IsLValueReference<deleter_type>::value, deleter_type, typename AddLValueReference<const deleter_type>::type>::type deleter, typename EnableIf<SmartPtrDetail::IsArrayCvConvertible<P, pointer>::value>::type* = 0)noexcept : base_type(pArray, deleter){}
    template<typename P>
    UniquePtr(P pArray, typename RemoveReference<deleter_type>::type&& deleter, EnableIf_T<SmartPtrDetail::IsArrayCvConvertible<P, pointer>::value>* = 0)noexcept : base_type(pArray, Move(deleter)){
        static_assert(!IsLValueReference<deleter_type>::value, "deleter_type reference refers to an rvalue deleter. The reference will probably become invalid before used. Change the deleter_type to not be a reference or construct with permanent deleter.");
    }
    UniquePtr(this_type&& x)noexcept : base_type(x.release(), Forward<deleter_type>(x.get_deleter())){}
    template<typename U, typename E>
    UniquePtr(UniquePtr<U, E>&& u, typename EnableIf<SmartPtrDetail::IsSafeArrayConversion<T, pointer, U, typename UniquePtr<U, E>::pointer>::value && IsConvertible<E, deleter_type>::value && (!IsLValueReference<deleter_type>::value || IsSame<E, deleter_type>::value)>::type* = 0)noexcept : base_type(u.release(), Forward<E>(u.get_deleter())){}
    UniquePtr(const this_type&) = delete;
    UniquePtr& operator=(const this_type&) = delete;
    UniquePtr& operator=(pointer pArray) = delete;

    ~UniquePtr()noexcept{ reset(); }


public:
    this_type& operator=(this_type&& x)noexcept{
        reset(x.release());
        get_deleter() = Move(Forward<deleter_type>(x.get_deleter()));
        return *this;
    }
    template<typename U, typename E>
    typename EnableIf<SmartPtrDetail::IsSafeArrayConversion<T, pointer, U, typename UniquePtr<U, E>::pointer>::value&& IsAssignable<deleter_type&, E&&>::value, this_type&>::type operator=(UniquePtr<U, E>&& u)noexcept{
        reset(u.release());
        get_deleter() = Move(Forward<E>(u.get_deleter()));
        return *this;
    }
    this_type& operator=(std::nullptr_t)noexcept{
        reset();
        return *this;
    }

    explicit operator bool()const noexcept{ return (get() != pointer()); }

    typename AddLValueReference<T>::type operator[](ptrdiff_t i)const{ return get()[i]; }


public:
    using base_type::reset;
    using base_type::release;
    using base_type::detach;
    using base_type::get;
    using base_type::get_deleter;

    void swap(this_type& x)noexcept{ base_type::swapStorage(x); }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T, typename Deleter = DefaultDeleter<T>>
class NotNullUniquePtr{
    static_assert(!IsArray<T>::value, "NotNullUniquePtr does not support array types.");


public:
    typedef T element_type;
    typedef Deleter deleter_type;
    typedef UniquePtr<element_type, deleter_type> owner_type;
    typedef typename owner_type::pointer pointer;


public:
    NotNullUniquePtr() = delete;
    NotNullUniquePtr(std::nullptr_t) = delete;
    explicit NotNullUniquePtr(owner_type&& owner)noexcept
        : m_owner(Move(owner))
    {
        NWB_ASSERT_MSG(m_owner.get(), "NotNullUniquePtr requires non-null owner");
    }

    NotNullUniquePtr(NotNullUniquePtr&&)noexcept = default;
    NotNullUniquePtr& operator=(NotNullUniquePtr&&)noexcept = default;

    NotNullUniquePtr(const NotNullUniquePtr&) = delete;
    NotNullUniquePtr& operator=(const NotNullUniquePtr&) = delete;
    NotNullUniquePtr& operator=(std::nullptr_t) = delete;


public:
    typename AddLValueReference<element_type>::type operator*()const{
        return *m_owner;
    }
    pointer operator->()const noexcept{
        return m_owner.get();
    }


public:
    [[nodiscard]] pointer get()const noexcept{
        return m_owner.get();
    }

    [[nodiscard]] owner_type& owner()noexcept{
        return m_owner;
    }
    [[nodiscard]] const owner_type& owner()const noexcept{
        return m_owner;
    }


private:
    owner_type m_owner;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T, typename... Args>
inline typename EnableIf<!IsArray<T>::value, UniquePtr<T>>::type MakeUnique(Args&&... args){
    return UniquePtr<T>(new T(Forward<Args>(args)...));
}
template<typename T>
inline typename EnableIf<IsUnboundedArray<T>::value, UniquePtr<T>>::type MakeUnique(usize n){
    typedef typename RemoveExtent<T>::type TBase;
    return UniquePtr<T>(new TBase[n]);
}
template<typename T, typename... Args>
typename EnableIf<IsBoundedArray<T>::value>::type
MakeUnique(Args&&...) = delete;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T, typename Deleter>
[[nodiscard]] inline NotNullUniquePtr<T, Deleter> MakeNotNullUnique(UniquePtr<T, Deleter>&& owner){
    return NotNullUniquePtr<T, Deleter>(Move(owner));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace std{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T, typename D>
struct hash<UniquePtr<T, D>>{
    size_t operator()(const UniquePtr<T, D>& x) const noexcept{ return std::hash<typename UniquePtr<T, D>::pointer>()(x.get()); }
};

template<typename T, typename D>
inline void swap(UniquePtr<T, D>& a, UniquePtr<T, D>& b)noexcept{ a.swap(b); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T1, typename D1, typename T2, typename D2>
inline bool operator==(const UniquePtr<T1, D1>& a, const UniquePtr<T2, D2>& b){ return (a.get() == b.get()); }

template<typename T1, typename D1, typename T2, typename D2>
requires ThreeWayComparableWith<typename UniquePtr<T1, D1>::pointer, typename UniquePtr<T2, D2>::pointer>
inline CompareThreeWayResult_T<typename UniquePtr<T1, D1>::pointer, typename UniquePtr<T2, D2>::pointer> operator<=>(const UniquePtr<T1, D1>& a, const UniquePtr<T2, D2>& b){ return a.get() <=> b.get(); }

template<typename T1, typename D1, typename T2, typename D2>
inline bool operator<(const UniquePtr<T1, D1>& a, const UniquePtr<T2, D2>& b){
    typedef typename UniquePtr<T1, D1>::pointer P1;
    typedef typename UniquePtr<T2, D2>::pointer P2;
    typedef typename CommonType<P1, P2>::type PCommon;
    PCommon pT1 = a.get();
    PCommon pT2 = b.get();
    return LessThan<PCommon>()(pT1, pT2);
}
template<typename T1, typename D1, typename T2, typename D2>
inline bool operator>(const UniquePtr<T1, D1>& a, const UniquePtr<T2, D2>& b){ return (b < a); }
template<typename T1, typename D1, typename T2, typename D2>
inline bool operator<=(const UniquePtr<T1, D1>& a, const UniquePtr<T2, D2>& b){ return !(b < a); }
template<typename T1, typename D1, typename T2, typename D2>
inline bool operator>=(const UniquePtr<T1, D1>& a, const UniquePtr<T2, D2>& b){ return !(a < b); }

template<typename T, typename D>
inline bool operator==(const UniquePtr<T, D>& a, std::nullptr_t)noexcept{ return !a; }
template<typename T, typename D>
requires ThreeWayComparableWith<typename UniquePtr<T, D>::pointer, std::nullptr_t>
inline CompareThreeWayResult_T<typename UniquePtr<T, D>::pointer, std::nullptr_t> operator<=>(const UniquePtr<T, D>& a, std::nullptr_t){return a.get() <=> nullptr; }

template<typename T, typename D>
inline bool operator<(const UniquePtr<T, D>& a, std::nullptr_t){
    typedef typename UniquePtr<T, D>::pointer pointer;
    return LessThan<pointer>()(a.get(), nullptr);
}
template<typename T, typename D>
inline bool operator<(std::nullptr_t, const UniquePtr<T, D>& b){
    typedef typename UniquePtr<T, D>::pointer pointer;
    pointer pT = b.get();
    return LessThan<pointer>()(nullptr, pT);
}
template<typename T, typename D>
inline bool operator>(const UniquePtr<T, D>& a, std::nullptr_t){ return (nullptr < a); }
template<typename T, typename D>
inline bool operator>(std::nullptr_t, const UniquePtr<T, D>& b){ return (b < nullptr); }
template<typename T, typename D>
inline bool operator<=(const UniquePtr<T, D>& a, std::nullptr_t){ return !(nullptr < a); }
template<typename T, typename D>
inline bool operator<=(std::nullptr_t, const UniquePtr<T, D>& b){ return !(b < nullptr); }
template<typename T, typename D>
inline bool operator>=(const UniquePtr<T, D>& a, std::nullptr_t){ return !(a < nullptr); }
template<typename T, typename D>
inline bool operator>=(std::nullptr_t, const UniquePtr<T, D>& b){ return !(nullptr < b); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

