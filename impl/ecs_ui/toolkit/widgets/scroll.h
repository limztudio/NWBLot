// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/paint.h>
#include <impl/ecs_ui/toolkit/widgets/scrollbar_style.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Hosts own scroll state; its offset and identity contain no retained application or OS references.
class ScrollState final : NoCopy{
public:
    ScrollState()noexcept;
    ScrollState(ScrollState&&) = delete;


public:
    ScrollState& operator=(ScrollState&&) = delete;


public:
    [[nodiscard]] u64 instanceGeneration()const noexcept{ return m_instanceGeneration; }

    [[nodiscard]] f64 offset()const noexcept{ return m_offset; }
    [[nodiscard]] b8 setOffset(f64 offset)noexcept;
    [[nodiscard]] b8 ensureVisible(const f64 start, const f64 end, const f64 viewportHeight)noexcept{ return TryEnsureScrollVisible(m_offset, start, end, viewportHeight, m_offset); }
    [[nodiscard]] b8 scrollBy(const f64 delta, const f64 contentHeight, const f64 viewportHeight)noexcept{ return TryScrollByOffset(m_offset, delta, contentHeight, viewportHeight, m_offset); }
    [[nodiscard]] b8 clamp(const f64 contentHeight, const f64 viewportHeight)noexcept{ return TryClampScrollOffset(m_offset, contentHeight, viewportHeight, m_offset); }


private:
    const u64 m_instanceGeneration;
    f64 m_offset = 0.0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

