
#pragma once


#include "assert.h"
#include "compile.h"
#include "generic.h"
#include "type.h"
#include "type_properties.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<usize InlineStorageSize>
class InplaceFunction : NoCopy{
private:
    struct Storage{
        alignas(MaxAlign) u8 data[InlineStorageSize];
    };

    using InvokeFn = void(*)(void*);
    using DestroyFn = void(*)(void*);
    using MoveFn = void(*)(void*, void*);


public:
    inline InplaceFunction() = default;
    inline ~InplaceFunction(){ reset(); }

    inline InplaceFunction(InplaceFunction&& rhs)noexcept{
        moveFrom(Move(rhs));
    }

    inline InplaceFunction& operator=(InplaceFunction&& rhs)noexcept{
        if(this != &rhs){
            reset();
            moveFrom(Move(rhs));
        }
        return *this;
    }

    template<typename Func, typename = EnableIf_T<!IsSame_V<Decay_T<Func>, InplaceFunction>>>
    inline explicit InplaceFunction(Func&& func){
        assign(Forward<Func>(func));
    }

    template<typename Func, typename = EnableIf_T<!IsSame_V<Decay_T<Func>, InplaceFunction>>>
    inline InplaceFunction& operator=(Func&& func){
        reset();
        assign(Forward<Func>(func));
        return *this;
    }

    inline explicit operator bool()const{ return m_invoke != nullptr; }

    inline void operator()(){
        NWB_ASSERT_MSG(m_invoke != nullptr, NWB_TEXT("InplaceFunction invoked without target"));
        m_invoke(storagePtr());
    }

    inline void reset(){
        if(!m_destroy)
            return;

        m_destroy(storagePtr());
        m_invoke = nullptr;
        m_destroy = nullptr;
        m_move = nullptr;
    }


private:
    template<typename Func>
    inline void assign(Func&& func){
        using FuncType = Decay_T<Func>;

        static_assert(sizeof(FuncType) <= InlineStorageSize, "InplaceFunction capture size exceeds inline storage");
        static_assert(alignof(FuncType) <= alignof(Storage), "InplaceFunction capture alignment exceeds inline storage");

        new(storagePtr()) FuncType(Forward<Func>(func));

        m_invoke = [](void* storage){
            FuncType* f = static_cast<FuncType*>(storage);
            (*f)();
        };
        m_destroy = [](void* storage){
            FuncType* f = static_cast<FuncType*>(storage);
            f->~FuncType();
        };
        m_move = [](void* dst, void* src){
            FuncType* source = static_cast<FuncType*>(src);
            new(dst) FuncType(Move(*source));
            source->~FuncType();
        };
    }

    inline void moveFrom(InplaceFunction&& rhs)noexcept{
        if(!rhs.m_invoke)
            return;

        rhs.m_move(storagePtr(), rhs.storagePtr());
        m_invoke = rhs.m_invoke;
        m_destroy = rhs.m_destroy;
        m_move = rhs.m_move;

        rhs.m_invoke = nullptr;
        rhs.m_destroy = nullptr;
        rhs.m_move = nullptr;
    }

    inline void* storagePtr(){
        return static_cast<void*>(m_storage.data);
    }


private:
    Storage m_storage = {};
    InvokeFn m_invoke = nullptr;
    DestroyFn m_destroy = nullptr;
    MoveFn m_move = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

