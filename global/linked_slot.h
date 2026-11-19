// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "assert.h"
#include "generic.h"
#include "optional.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// A pair follows its endpoints through moves. Copies own only the copied value.
template<typename T>
class LinkedSlot final{
    static_assert(IsNothrowMoveConstructible_V<T> && IsNothrowDestructible_V<T>);

public:
    LinkedSlot()noexcept = default;
    ~LinkedSlot()noexcept{ disconnect(); }
    LinkedSlot(const LinkedSlot& other) requires IsCopyConstructible_V<T>
        : m_value(other.m_value)
    {}
    LinkedSlot(LinkedSlot&& other)noexcept{ moveFrom(other); }

    LinkedSlot& operator=(const LinkedSlot& other) requires (IsCopyConstructible_V<T> && IsAssignable_V<T&, const T&>){
        if(this != &other){
            disconnect();
            m_value = other.m_value;
        }
        return *this;
    }
    LinkedSlot& operator=(LinkedSlot&& other)noexcept{
        if(this != &other){
            disconnect();
            moveFrom(other);
        }
        return *this;
    }

    void connect(LinkedSlot& other)noexcept{
        if(this == &other)
            return;
        disconnect();
        other.disconnect();
        m_peer = &other;
        other.m_peer = this;
    }
    void disconnect()noexcept{
        if(m_peer){
            m_peer->m_peer = nullptr;
            m_peer = nullptr;
        }
    }
    [[nodiscard]] b8 linked()const noexcept{ return m_peer != nullptr; }
    [[nodiscard]] LinkedSlot& peer()noexcept{ FOUNDATION_ASSERT(m_peer); return *m_peer; }

    [[nodiscard]] b8 hasValue()const noexcept{ return m_value.has_value(); }
    [[nodiscard]] T& value()noexcept{ FOUNDATION_ASSERT(m_value); return *m_value; }
    void resetValue()noexcept{ m_value.reset(); }
    void takeFrom(LinkedSlot& other)noexcept{
        if(this == &other)
            return;
        if(other.m_value){
            m_value.emplace(Move(*other.m_value));
            other.m_value.reset();
        }
        else
            m_value.reset();
    }

private:
    template<typename U>
    friend void TakeLinkedSlotValue(LinkedSlot<U>& slot, U& value)noexcept;

    void moveFrom(LinkedSlot& other)noexcept{
        takeFrom(other);
        m_peer = other.m_peer;
        if(m_peer){
            m_peer->m_peer = this;
            other.m_peer = nullptr;
        }
    }

    Optional<T> m_value;
    LinkedSlot* m_peer = nullptr;
};


template<typename T>
inline void TakeLinkedSlotValue(LinkedSlot<T>& slot, T& value)noexcept{
    if(!slot.m_value || &*slot.m_value != &value)
        slot.m_value.emplace(Move(value));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

