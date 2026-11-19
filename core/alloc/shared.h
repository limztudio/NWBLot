// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "general.h"

#include <global/expected.h>
#include <global/refcount_ptr.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SharedDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct PoolStorage{
    explicit PoolStorage(const Name& owner) : arena(owner){}

    GlobalArena arena;
};

using Pool = RefCountPtr<RefCounter<PoolStorage>>;

struct PreparedValue{};

class Control{
public:
    virtual void destroy()noexcept = 0;

protected:
    ~Control()noexcept = default;
};

using CountedControl = RefCounter<Control>;

struct ControlDeleter{
    void operator()(CountedControl* control)const noexcept{ control->destroy(); }
};

template<typename T>
class ValueStorage final : public CountedControl{
public:
    ValueStorage(PreparedValue, const Pool& pool, T& prepared)
        : m_owner(pool)
        , value(Move(prepared))
    {}

    template<typename... Args>
        requires LeadingArenaConstructible<T>
    ValueStorage(const Pool& pool, Args&&... args)
        : m_owner(pool)
        , value(pool->arena, Forward<Args>(args)...)
    {}

    template<typename... Args>
        requires (!LeadingArenaConstructible<T>)
    ValueStorage(const Pool& pool, Args&&... args)
        : m_owner(pool)
        , value(Forward<Args>(args)...)
    {}

    void destroy()noexcept override{
        const Pool owner = m_owner;
        DestroyArenaObject(owner->arena, this);
    }


private:
    Pool m_owner;
public:
    T value;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
class Shared{
    friend class SharedPool;

    template<typename U>
    friend class Shared;


private:
    Shared(SharedDetail::CountedControl& control, T& value)noexcept
        : m_control(&control, s_AdoptRef)
        , m_value(AddressOf(value))
    {}


public:
    Shared()noexcept = default;
    Shared(Nullptr)noexcept{}

    template<typename U>
        requires IsConvertible_V<U*, T*>
    Shared(const Shared<U>& other)noexcept
        : m_control(other.m_control)
        , m_value(other.get())
    {}

    [[nodiscard]] b8 valid()const noexcept{ return m_control.valid(); }
    [[nodiscard]] explicit operator b8()const noexcept{ return valid(); }
    [[nodiscard]] u32 referenceCount()const noexcept{ return m_control ? m_control->getReferenceCount() : 0u; }
    [[nodiscard]] T* get()const noexcept{ return valid() ? m_value : nullptr; }
    [[nodiscard]] T& deref()const noexcept{
        FOUNDATION_ASSERT_MSG(valid(), FOUNDATION_TEXT("Shared requires a live value"));
        return *m_value;
    }
    [[nodiscard]] T& operator*()const noexcept{ return deref(); }
    [[nodiscard]] T* operator->()const noexcept{ return AddressOf(deref()); }
    [[nodiscard]] b8 operator==(const Shared& other)const noexcept{ return get() == other.get(); }
    [[nodiscard]] usize hash()const noexcept{ return Hasher<T*>{}(get()); }
    void reset()noexcept{
        m_control.reset();
        m_value = nullptr;
    }


private:
    RefCountPtr<SharedDetail::CountedControl, SharedDetail::ControlDeleter> m_control;
    T* m_value = nullptr;
};

class SharedPool{
public:
    using uses_leading_arena_t = ::ArenaBase;

    explicit SharedPool(::ArenaBase& owner)
        : m_pool(MakeRefCount<RefCounter<SharedDetail::PoolStorage>>(owner.allocationLog()))
    {}

    SharedPool(::ArenaBase&, const SharedPool& other)noexcept : m_pool(other.m_pool){}
    SharedPool(::ArenaBase&, SharedPool&& other)noexcept : m_pool(Move(other.m_pool)){}

    [[nodiscard]] ArenaMemoryStats memoryStats()const noexcept{ return m_pool ? m_pool->arena.memoryStats() : ArenaMemoryStats{}; }

    template<typename T, typename... Args>
    [[nodiscard]] Expected<Shared<T>> make(Args&&... args)const{
        return allocate<T>(m_pool, Forward<Args>(args)...);
    }

    template<typename T>
    [[nodiscard]] Expected<Shared<T>> makePrepared(T& prepared)const{
        return allocate<T>(SharedDetail::PreparedValue{}, m_pool, prepared);
    }


private:
    template<typename T, typename... Args>
    [[nodiscard]] Expected<Shared<T>> allocate(Args&&... args)const{
        if(!m_pool)
            return MakeUnexpected(Failure{});
        auto* const block = NewArenaObject<SharedDetail::ValueStorage<T>>(m_pool->arena, Forward<Args>(args)...);
        if(!block)
            return MakeUnexpected(Failure{});
        return Shared<T>(*block, block->value);
    }


private:
    SharedDetail::Pool m_pool;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

