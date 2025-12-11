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
template <typename F>
friend class RefCountPtr;
    
    
public:
    typedef T element_type;
    
    
public:
    RefCountPtr()noexcept : instance(nullptr){}
    RefCountPtr(std::nullptr_t)noexcept : instance(nullptr){}
    template <typename F>
    RefCountPtr(F* ptr)noexcept : instance(ptr){
        internalAddRef();
    }
    RefCountPtr(const RefCountPtr& rhs)noexcept : instance(rhs.instance){
        internalAddRef();
    }
    template <typename F>
    RefCountPtr(const RefCountPtr<F>& rhs, typename EnableIf<IsConvertible<F*, T*>::value, void*>::type* = nullptr)noexcept : instance(rhs.instance){
        internalAddRef();
    }
    RefCountPtr(RefCountPtr&& rhs)noexcept : instance(nullptr){
        
    }
    
    
protected:
    void internalAddRef()const noexcept{
        if(instance)
            instance->addReference();
    }
    u32 internalRelease()noexcept{
        u32 ref = 0;
        T* temp = instance;
        if(temp){
            instance = nullptr;
            ref = temp->release();
        }
        return ref;
    }
    
    
protected:
    element_type* instance;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

