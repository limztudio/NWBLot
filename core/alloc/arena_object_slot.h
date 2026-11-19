// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "general.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
class ArenaObjectSlot final : public ::NoCopy{
public:
    using uses_leading_arena_t = ::ArenaBase;

    explicit ArenaObjectSlot(::ArenaBase& arena)noexcept
        : m_value(nullptr, ::ArenaDeleter<T, ::ArenaBase>(arena))
    {}
    ArenaObjectSlot(ArenaObjectSlot&&) = delete;
    ArenaObjectSlot& operator=(ArenaObjectSlot&&) = delete;

    template<typename... Args>
    [[nodiscard]] b8 tryEmplace(Args&&... args){
        if(m_value)
            return false;
        ::ArenaBase& arena = *::MakeNotNull(m_value.getDeleter().pool);
        if constexpr(::LeadingArenaConstructible<T>)
            m_value = ::MakeArenaUnique<T>(arena, arena, Forward<Args>(args)...);
        else
            m_value = ::MakeArenaUnique<T>(arena, Forward<Args>(args)...);
        return static_cast<b8>(m_value);
    }

    [[nodiscard]] b8 valid()const noexcept{ return static_cast<b8>(m_value); }
    [[nodiscard]] T& value()noexcept{ return *::MakeNotNull(m_value.get()); }
    [[nodiscard]] const T& value()const noexcept{ return read(); }
    [[nodiscard]] const T& read()const noexcept{ return *::MakeNotNull(m_value.get()); }

private:
    ::ArenaUniquePtr<T, ::ArenaBase> m_value;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

