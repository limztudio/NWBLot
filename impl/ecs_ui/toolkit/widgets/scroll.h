// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/paint.h>
#include <impl/ecs_ui/toolkit/widgets/scroll_viewport_placement.adhd.h>

#include <global/atomic_identity.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Hosts own scroll state; its offset and identity contain no retained application or OS references.
class ScrollState final : NoCopy{
private:
    [[nodiscard]] static u64 NextIdentity()noexcept{
        static Atomic<u64> s_NextIdentity{ 1u };
        return NextNonWrappingIdentity(s_NextIdentity);
    }


public:
    ScrollState()noexcept
        : m_instanceGeneration(NextIdentity())
    {}
    ScrollState(ScrollState&&) = delete;


public:
    ScrollState& operator=(ScrollState&&) = delete;


public:
    [[nodiscard]] u64 instanceGeneration()const noexcept{ return m_instanceGeneration; }

    [[nodiscard]] f64 offset()const noexcept{ return m_offset; }
    [[nodiscard]] b8 setOffset(const f64 offset)noexcept{
        const auto next = TrySetScrollOffset(offset);
        if(!next)
            return false;
        m_offset = *next;
        return true;
    }
    [[nodiscard]] b8 ensureVisible(const f64 start, const f64 end, const f64 viewportHeight)noexcept{
        const auto next = TryEnsureScrollVisible(m_offset, start, end, viewportHeight);
        if(!next)
            return false;
        m_offset = *next;
        return true;
    }
    [[nodiscard]] b8 scrollBy(const f64 delta, const f64 contentHeight, const f64 viewportHeight)noexcept{
        const auto next = TryScrollByOffset(m_offset, delta, contentHeight, viewportHeight);
        if(!next)
            return false;
        m_offset = *next;
        return true;
    }
    [[nodiscard]] b8 clamp(const f64 contentHeight, const f64 viewportHeight)noexcept{
        const auto next = TryClampScrollOffset(m_offset, contentHeight, viewportHeight);
        if(!next)
            return false;
        m_offset = *next;
        return true;
    }


private:
    const u64 m_instanceGeneration;
    f64 m_offset = 0.0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

