// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "type.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <typename T>
concept RefCountClass = IsClass_V<T> && requires(T& obj){
    { obj.addReference() }->SameAs<u32>;
    { obj.release() }->SameAs<u32>;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <RefCountClass T>
class RefCountPtr{
template <typename U>
friend class RefCountPtr;
    
    
public:
    typedef T element_type;
    typedef RefCountPtr<element_type> this_type;
    typedef T* pointer;
    
    
public:
    static this_type create(pointer ptr){
        this_type output;
        output.attach(ptr);
        return output;
    }
    
    
public:
    constexpr RefCountPtr()noexcept : instance(nullptr){}
    constexpr RefCountPtr(std::nullptr_t)noexcept : instance(nullptr){}
    template <typename U>
    RefCountPtr(U* ptr)noexcept : instance(ptr){ internalAddRef(); }
    RefCountPtr(const RefCountPtr& rhs)noexcept : instance(rhs.instance){ internalAddRef(); }
    template <typename U>
    RefCountPtr(const RefCountPtr<U>& rhs, typename EnableIf<IsConvertible<U*, T*>::value, void*>::type* = nullptr)noexcept : instance(rhs.instance){ internalAddRef(); }
    RefCountPtr(this_type&& rhs)noexcept : instance(nullptr){
        if(this != &rhs)
            swap(rhs);
    }
    template <typename U>
    RefCountPtr(RefCountPtr<U>&& rhs, typename EnableIf<IsConvertible)<U*, T*>::value, void*>::type* = nullptr)noexcept : instance(rhs.instance){ rhs.instance = nullptr; }
    
    ~RefCountPtr(){ internalRelease(); }
    
    
public:
    this_type& operator=(std::nullptr_t)noexcept{
        internalRelease();
        return *this;
    }
    this_type& operator=(pointer rhs)noexcept{
        if(instance != rhs)
            this_type(rhs).swap(*this);
        return *this;
    }
    template <typename U>
    this_type& operator=(U* rhs)noexcept{
        this_type(rhs).swap(*this);
        return *this;
    }
    this_type& operator=(const this_type& rhs)noexcept{
        if(this != &rhs)
            this_type(rhs).swap(*this);
        return *this;
    }
    template <typename U>
    this_type& operator=(const RefCountPtr<U>& rhs)noexcept{
        this_type(rhs).swap(*this);
        return *this;
    }
    this_type& operator=(this_type&& rhs)noexcept{
        this_type(static_cast<this_type&&>(rhs)).swap(*this);
        return *this;
    }
    template <typename U>
    this_type& operator=(RefCountPtr<U>&& rhs)noexcept{
        this_type(static_cast<RefCountPtr<U>&&>(rhs)).swap(*this);
        return *this;
    }
    
    typename AddLValueReference<T>::type operator*()const{ return *instance; }
    pointer operator->()const noexcept{ return instance; }
    pointer* operator&()noexcept{ return &instance; }
    
    explicit operator bool()const noexcept{ return (instance != nullptr); }


public:
    pointer detach()noexcept{
        pointer output = instance;
        instance = nullptr;
        return output;
    }
    void attach(pointer ptr){
        if(ptr){
            auto ref = instance->release()
            (void)ref;
            NWB_ASSERT(ref != 0 || instance != ptr);
        }
        instance = ptr;
    }
    
    u32 release(){ return internalRelease(); }
    void reset(pointer ptr = pointer()){
        if(instance != ptr){
            this_type(ptr).swap(*this);
        }
    }
    
    pointer get()const noexcept{ return instance; }
    [[nodiscard]] pointer const* getAddressOf()const noexcept{ return &instance; }
    [[nodiscard]] pointer* getAddressOf()noexcept{ return &instance; }
    [[nodiscard]] pointer** releaseAndGetAddressOf()noexcept{
        internalRelease();
        return &instance;
    }

    void swap(RefCountPtr& rhs)noexcept{
        pointer temp = instance;
        instance = rhs.instance;
        rhs.instance = temp;
    }
    void swap(RefCountPtr&& rhs)noexcept{
        pointer temp = instance;
        instance = rhs.instance;
        rhs.instance = temp;
    }


protected:
    void internalAddRef()const noexcept{
        if(instance)
            instance->addReference();
    }
    u32 internalRelease()noexcept{
        u32 ref = 0;
        pointer temp = instance;
        if(temp){
            instance = nullptr;
            ref = temp->release();
        }
        return ref;
    }


protected:
    pointer instance;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <typename T>
class RefCounter : public T{
public:
    u32 addReference(){ return m_referenceCount.fetch_add(1, std::memory_order_relaxed) + 1; }
    u32 release(){
        u32 old = m_referenceCount.fetch_sub(1, std::memory_order_release);
        if (old == 1) {
            std::atomic_thread_fence(std::memory_order_acquire);
            delete this;
            return 0;
        }
        return old - 1;
    }
    
    u32 getReferenceCount()const noexcept{ return m_referenceCount.load(std::memory_order_relaxed); }
    
    
private:
    Atomic<u32> m_referenceCount{1};
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <typename T>
inline RefCountPtr<T> MakeRefCount(T* ptr){
    return RefCountPtr<T>::create(ptr);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace std{
    template <typename T>
    struct hash<RefCountPtr<T>>{
        size_t operator()(const RefCountPtr<T>& x) const noexcept{ return std::hash<typename RefCountPtr<T>::pointer>()(x.get()); }
    };

    template <typename T>
    inline void swap(RefCountPtr<T>& a, RefCountPtr<T>& b)noexcept{ a.swap(b); }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <typename T1, typename T2>
inline bool operator==(const RefCountPtr<T1>& a, const RefCountPtr<T2>& b){ return (a.get() == b.get()); }

template <typename T1, typename T2>
requires ThreeWayComparableWith<typename RefCountPtr<T1>::pointer, typename RefCountPtr<T2>::pointer>
inline CompareThreeWayResult_T<typename RefCountPtr<T1>::pointer, typename RefCountPtr<T2>::pointer> operator<=>(const RefCountPtr<T1>& a, const RefCountPtr<T2>& b){ return a.get() <=> b.get(); }

template <typename T1, typename T2>
inline bool operator<(const RefCountPtr<T1>& a, const RefCountPtr<T2>& b){
    typedef typename RefCountPtr<T1>::pointer P1;
    typedef typename RefCountPtr<T2>::pointer P2;
    typedef typename CommonType<P1, P2>::type PCommon;
    PCommon pT1 = a.get();
    PCommon pT2 = b.get();
    return LessThan<PCommon>()(pT1, pT2);
}
template <typename T1, typename T2>
inline bool operator>(const RefCountPtr<T1>& a, const RefCountPtr<T2>& b){ return (b < a); }
template <typename T1, typename T2>
inline bool operator<=(const RefCountPtr<T1>& a, const RefCountPtr<T2>& b){ return !(b < a); }
template <typename T1, typename T2>
inline bool operator>=(const RefCountPtr<T1>& a, const RefCountPtr<T2>& b){ return !(a < b); }

template <typename T>
inline bool operator==(const RefCountPtr<T>& a, std::nullptr_t)noexcept{ return !a; }
template <typename T>
requires ThreeWayComparableWith<typename RefCountPtr<T>::pointer, std::nullptr_t>
inline CompareThreeWayResult_T<typename RefCountPtr<T>::pointer, std::nullptr_t> operator<=>(const RefCountPtr<T>& a, std::nullptr_t){return a.get() <=> nullptr; }

template <typename T>
inline bool operator<(const RefCountPtr<T>& a, std::nullptr_t){
    typedef typename RefCountPtr<T>::pointer pointer;
    return LessThan<pointer>()(a.get(), nullptr);
}
template <typename T>
inline bool operator<(std::nullptr_t, const RefCountPtr<T>& b){
    typedef typename RefCountPtr<T>::pointer pointer;
    pointer pT = b.get();
    return LessThan<pointer>()(nullptr, pT);
}
template <typename T>
inline bool operator>(const RefCountPtr<T>& a, std::nullptr_t){ return (nullptr < a); }
template <typename T>
inline bool operator>(std::nullptr_t, const RefCountPtr<T>& b){ return (b < nullptr); }
template <typename T>
inline bool operator<=(const RefCountPtr<T>& a, std::nullptr_t){ return !(nullptr < a); }
template <typename T>
inline bool operator<=(std::nullptr_t, const RefCountPtr<T>& b){ return !(b < nullptr); }
template <typename T>
inline bool operator>=(const RefCountPtr<T>& a, std::nullptr_t){ return !(a < nullptr); }
template <typename T>
inline bool operator>=(std::nullptr_t, const RefCountPtr<T>& b){ return !(nullptr < b); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

