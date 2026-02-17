// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "compressed_pair.h"
#include "smart_ptr.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
concept RefCountClass = IsClass_V<T> && requires(T& obj){
    { obj.addReference() }->SameAs<u32>;
    { obj.release() }->SameAs<u32>;
};

struct AdoptRefT { explicit AdoptRefT() = default; };
inline constexpr AdoptRefT AdoptRef{};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<RefCountClass T, typename Deleter = DefaultDeleter<T>>
class RefCountPtr{
    static_assert(!IsRValueReference<Deleter>::value, "The supplied Deleter cannot be a r-value reference.");
public:
    typedef Deleter deleter_type;
    typedef T element_type;
    typedef RefCountPtr<element_type, deleter_type> this_type;
    typedef typename __hidden_smart_ptr::UniquePointerType<element_type, deleter_type>::type pointer;
private:
    template<typename U>
    using EnableUFromRawPtr = typename EnableIf<!IsArray<U>::value && IsConvertible<U*, pointer>::value>::type;

public:
    constexpr RefCountPtr()noexcept : mPair(pointer()){
        static_assert(!IsPointer<deleter_type>::value, "RefCountPtr deleter default-constructed with null pointer. Use a different constructor or change your deleter to a class.");
    }
    constexpr RefCountPtr(std::nullptr_t)noexcept : mPair(pointer()){
        static_assert(!IsPointer<deleter_type>::value, "RefCountPtr deleter default-constructed with null pointer. Use a different constructor or change your deleter to a class.");
    }
    RefCountPtr(pointer pValue)noexcept : mPair(pValue){
        static_assert(!IsPointer<deleter_type>::value, "RefCountPtr deleter default-constructed with null pointer. Use a different constructor or change your deleter to a class.");
        internalAddRef(pValue);
    }
    constexpr RefCountPtr(pointer pValue, AdoptRefT)noexcept : mPair(pValue){
        static_assert(!IsPointer<deleter_type>::value, "RefCountPtr deleter default-constructed with null pointer. Use a different constructor or change your deleter to a class.");
    }
    RefCountPtr(pointer pValue, typename Conditional<IsLValueReference<deleter_type>::value, deleter_type, typename AddLValueReference<const deleter_type>::type>::type deleter)noexcept : mPair(pValue, deleter){
        internalAddRef(pValue);
    }
    constexpr RefCountPtr(pointer pValue, typename Conditional<IsLValueReference<deleter_type>::value, deleter_type, typename AddLValueReference<const deleter_type>::type>::type deleter, AdoptRefT)noexcept : mPair(pValue, deleter){}
    RefCountPtr(pointer pValue, typename RemoveReference<deleter_type>::type&& deleter)noexcept : mPair(pValue, Move(deleter)){
        static_assert(!IsLValueReference<deleter_type>::value, "deleter_type reference refers to an rvalue deleter. The reference will probably become invalid before used. Change the deleter_type to not be a reference or construct with permanent deleter.");
        internalAddRef(pValue);
    }
    constexpr RefCountPtr(pointer pValue, typename RemoveReference<deleter_type>::type&& deleter, AdoptRefT)noexcept : mPair(pValue, Move(deleter)){
        static_assert(!IsLValueReference<deleter_type>::value, "deleter_type reference refers to an rvalue deleter. The reference will probably become invalid before used. Change the deleter_type to not be a reference or construct with permanent deleter.");
    }
    template<typename U, typename = EnableUFromRawPtr<U>>
    explicit RefCountPtr(U* p)noexcept : mPair(pointer(p)){
        static_assert(!IsPointer<deleter_type>::value, "RefCountPtr deleter default-constructed with null pointer. Use a different constructor or change your deleter to a class.");
        internalAddRef(mPair.first());
    }
    template<typename U, typename = EnableUFromRawPtr<U>>
    constexpr RefCountPtr(U* p, AdoptRefT)noexcept : mPair(pointer(p)){
        static_assert(!IsPointer<deleter_type>::value, "RefCountPtr deleter default-constructed with null pointer. Use a different constructor or change your deleter to a class.");
    }
    template<typename U, typename = EnableUFromRawPtr<U>>
    RefCountPtr(U* pValue, typename Conditional<IsLValueReference<deleter_type>::value, deleter_type, typename AddLValueReference<const deleter_type>::type>::type deleter)noexcept : mPair(pValue, deleter){
        internalAddRef(pValue);
    }
    template<typename U, typename = EnableUFromRawPtr<U>>
    constexpr RefCountPtr(U* pValue, typename Conditional<IsLValueReference<deleter_type>::value, deleter_type, typename AddLValueReference<const deleter_type>::type>::type deleter, AdoptRefT)noexcept : mPair(pValue, deleter){}
    template<typename U, typename = EnableUFromRawPtr<U>>
    RefCountPtr(U* pValue, typename RemoveReference<deleter_type>::type&& deleter)noexcept : mPair(pValue, Move(deleter)){
        static_assert(!IsLValueReference<deleter_type>::value, "deleter_type reference refers to an rvalue deleter. The reference will probably become invalid before used. Change the deleter_type to not be a reference or construct with permanent deleter.");
        internalAddRef(pValue);
    }
    template<typename U, typename = EnableUFromRawPtr<U>>
    constexpr RefCountPtr(U* pValue, typename RemoveReference<deleter_type>::type&& deleter, AdoptRefT)noexcept : mPair(pValue, Move(deleter)){
        static_assert(!IsLValueReference<deleter_type>::value, "deleter_type reference refers to an rvalue deleter. The reference will probably become invalid before used. Change the deleter_type to not be a reference or construct with permanent deleter.");
    }
    constexpr RefCountPtr(this_type&& x)noexcept : mPair(x.detach(), Forward<deleter_type>(x.get_deleter())){}
    template<typename U, typename E>
    constexpr RefCountPtr(RefCountPtr<U, E>&& u, typename EnableIf<!IsArray<U>::value && IsConvertible<typename RefCountPtr<U, E>::pointer, pointer>::value && IsConvertible<E, deleter_type>::value && (IsSame<deleter_type, E>::value || !IsLValueReference<deleter_type>::value)>::type* = 0)noexcept : mPair(u.detach(), Forward<E>(u.get_deleter())){}
    RefCountPtr(const RefCountPtr& rhs)noexcept : mPair(rhs){
        internalAddRef(mPair.first());
    }
    template<typename U, typename E>
    RefCountPtr(const RefCountPtr<U, E>& u, typename EnableIf<!IsArray<U>::value && IsConvertible<typename RefCountPtr<U, E>::pointer, pointer>::value && IsConvertible<E, deleter_type>::value && (IsSame<deleter_type, E>::value || !IsLValueReference<deleter_type>::value)>::type* = 0)noexcept : mPair(u.get(), Forward<E>(u.get_deleter())){
        internalAddRef(mPair.first());
    }

    ~RefCountPtr()noexcept{ reset(); }

    
public:
    this_type& operator=(this_type&& x)noexcept{
        if(this != &x){
            reset();
            mPair.first() = x.detach();
            mPair.second() = Move(Forward<deleter_type>(x.get_deleter()));
        }
        return *this;
    }
    template<typename U, typename E>
    typename EnableIf<!IsArray<U>::value && IsConvertible<typename RefCountPtr<U, E>::pointer, pointer>::value && IsAssignable<deleter_type&, E&&>::value, this_type&>::type operator=(RefCountPtr<U, E>&& u)noexcept{
        reset();
        mPair.first() = u.detach();
        mPair.second() = Move(Forward<E>(u.get_deleter()));
        return *this;
    }
    this_type& operator=(const this_type& x)noexcept{
        if(this != &x){
            pointer newp = x.get();
            internalAddRef(newp);

            pointer oldp = std::exchange(mPair.first(), newp);
            internalRelease(oldp);

            mPair.second() = x.get_deleter();
        }
        return *this;
    }
    template<typename U, typename E>
    typename EnableIf<!IsArray<U>::value && IsConvertible<typename RefCountPtr<U, E>::pointer, pointer>::value && IsAssignable<deleter_type&, const E&>::value, this_type&>::type operator=(const RefCountPtr<U, E>& u)noexcept{
        pointer newp = u.get();
        internalAddRef(newp);

        pointer oldp = std::exchange(mPair.first(), newp);
        internalRelease(oldp);

        mPair.second() = u.get_deleter();
        return *this;
    }
    this_type& operator=(T* newp){
        reset(newp);
        return *this;
    }
    template<typename U>
    typename EnableIf<!IsArray<U>::value && IsConvertible<U*, pointer>::value, this_type&>::type operator=(U* newp){
        reset(newp);
        return *this;
    }
    this_type& operator=(std::nullptr_t)noexcept{
        reset();
        return *this;
    }

    typename AddLValueReference<T>::type operator*()const{ return *mPair.first(); }
    pointer operator->()const noexcept{ return mPair.first(); }

    explicit operator bool()const noexcept{ return (mPair.first() != pointer()); }

    
public:
    u32 reset(pointer pValue = pointer())noexcept{
        u32 ref = 0;
        if (pValue != mPair.first()) {
            internalAddRef(pValue);

            pointer oldp = std::exchange(mPair.first(), pValue);
            ref = internalRelease(oldp);
        }
        return ref;
    }
    u32 resetAdopt(pointer pValue)noexcept{
        u32 ref = 0;
        if (pValue != mPair.first()){
            pointer oldp = std::exchange(mPair.first(), pValue);
            ref = internalRelease(oldp);
        }
        return ref;
    }
    
    pointer release()noexcept{
        return std::exchange(mPair.first(), pointer());
    }
    u32 drop()noexcept{
        pointer oldp = std::exchange(mPair.first(), pointer());
        return internalRelease(oldp);
    }
    pointer detach()noexcept{ return release(); }

    void swap(this_type& x)noexcept{ mPair.swap(x.mPair); }

    pointer get()const noexcept{ return mPair.first(); }

    deleter_type& get_deleter()noexcept{ return mPair.second(); }
    const deleter_type& get_deleter()const noexcept{ return mPair.second(); }

    
protected:
    static void internalAddRef(pointer p)noexcept{
        if(p)
            p->addReference();
    }
    u32 internalRelease(pointer& target)noexcept{
        u32 newCount = 0;
        if(target){
            pointer p = target;
            target = nullptr;

            newCount = p->release();
            if(!newCount)
                get_deleter()(p);
        }
        return newCount;
    }

    
protected:
    CompressedPair<pointer, deleter_type> mPair;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
class RefCounter : public T{
public:
    u32 addReference()noexcept{ return m_referenceCount.fetch_add(1, std::memory_order_relaxed) + 1; }
    u32 release()noexcept{
        u32 old = m_referenceCount.fetch_sub(1, std::memory_order_release);
        if (old == 1) {
            std::atomic_thread_fence(std::memory_order_acquire);
            return 0;
        }
        return old - 1;
    }
    
    u32 getReferenceCount()const noexcept{ return m_referenceCount.load(std::memory_order_relaxed); }
    
    
private:
    Atomic<u32> m_referenceCount{1};
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T, typename... Args>
inline typename EnableIf<!IsArray<T>::value, RefCountPtr<T>>::type MakeRefCount(Args&&... args){
    return RefCountPtr<T>(new T(Forward<Args>(args)...), AdoptRef);
}
template<typename T, typename... Args>
typename EnableIf<IsBoundedArray<T>::value>::type
MakeRefCount(Args&&...) = delete;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace std{
    template<typename T, typename D>
    struct hash<RefCountPtr<T, D>>{
        size_t operator()(const RefCountPtr<T, D>& x) const noexcept{ return std::hash<typename RefCountPtr<T, D>::pointer>()(x.get()); }
    };

    template<typename T, typename D>
    inline void swap(RefCountPtr<T, D>& a, RefCountPtr<T, D>& b)noexcept{ a.swap(b); }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T1, typename D1, typename T2, typename D2>
inline bool operator==(const RefCountPtr<T1, D1>& a, const RefCountPtr<T2, D2>& b){ return (a.get() == b.get()); }

template<typename T1, typename D1, typename T2, typename D2>
requires ThreeWayComparableWith<typename RefCountPtr<T1, D1>::pointer, typename RefCountPtr<T2, D2>::pointer>
inline CompareThreeWayResult_T<typename RefCountPtr<T1, D1>::pointer, typename RefCountPtr<T2, D2>::pointer> operator<=>(const RefCountPtr<T1, D1>& a, const RefCountPtr<T2, D2>& b){ return a.get() <=> b.get(); }

template<typename T1, typename D1, typename T2, typename D2>
inline bool operator<(const RefCountPtr<T1, D1>& a, const RefCountPtr<T2, D2>& b){
    typedef typename RefCountPtr<T1, D1>::pointer P1;
    typedef typename RefCountPtr<T2, D2>::pointer P2;
    typedef typename CommonType<P1, P2>::type PCommon;
    PCommon pT1 = a.get();
    PCommon pT2 = b.get();
    return LessThan<PCommon>()(pT1, pT2);
}
template<typename T1, typename D1, typename T2, typename D2>
inline bool operator>(const RefCountPtr<T1, D1>& a, const RefCountPtr<T2, D2>& b){ return (b < a); }
template<typename T1, typename D1, typename T2, typename D2>
inline bool operator<=(const RefCountPtr<T1, D1>& a, const RefCountPtr<T2, D2>& b){ return !(b < a); }
template<typename T1, typename D1, typename T2, typename D2>
inline bool operator>=(const RefCountPtr<T1, D1>& a, const RefCountPtr<T2, D2>& b){ return !(a < b); }

template<typename T, typename D>
inline bool operator==(const RefCountPtr<T, D>& a, std::nullptr_t)noexcept{ return !a; }
template<typename T, typename D>
requires ThreeWayComparableWith<typename RefCountPtr<T, D>::pointer, std::nullptr_t>
inline CompareThreeWayResult_T<typename RefCountPtr<T, D>::pointer, std::nullptr_t> operator<=>(const RefCountPtr<T, D>& a, std::nullptr_t){return a.get() <=> nullptr; }

template<typename T, typename D>
inline bool operator<(const RefCountPtr<T, D>& a, std::nullptr_t){
    typedef typename RefCountPtr<T, D>::pointer pointer;
    return LessThan<pointer>()(a.get(), nullptr);
}
template<typename T, typename D>
inline bool operator<(std::nullptr_t, const RefCountPtr<T, D>& b){
    typedef typename RefCountPtr<T, D>::pointer pointer;
    pointer pT = b.get();
    return LessThan<pointer>()(nullptr, pT);
}
template<typename T, typename D>
inline bool operator>(const RefCountPtr<T, D>& a, std::nullptr_t){ return (nullptr < a); }
template<typename T, typename D>
inline bool operator>(std::nullptr_t, const RefCountPtr<T, D>& b){ return (b < nullptr); }
template<typename T, typename D>
inline bool operator<=(const RefCountPtr<T, D>& a, std::nullptr_t){ return !(nullptr < a); }
template<typename T, typename D>
inline bool operator<=(std::nullptr_t, const RefCountPtr<T, D>& b){ return !(b < nullptr); }
template<typename T, typename D>
inline bool operator>=(const RefCountPtr<T, D>& a, std::nullptr_t){ return !(a < nullptr); }
template<typename T, typename D>
inline bool operator>=(std::nullptr_t, const RefCountPtr<T, D>& b){ return !(nullptr < b); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

