// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/input/input.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class InputHoverState final{
public:
    void setTarget(Span<const HitTarget> targets, Span<const TargetLookup> lookup, Span<const PopupRecord> popups, const HitTarget* hovered)noexcept;
    void advanceActivity()noexcept;
    void clearTarget()noexcept;
    void forgetSource()noexcept;

    [[nodiscard]] u64 activityGeneration()const noexcept{ return m_activityGeneration; }
    [[nodiscard]] WidgetId& target()noexcept{ return m_target; }
    [[nodiscard]] WidgetId target()const noexcept{ return m_target; }
    [[nodiscard]] HoverIdentity& identity()noexcept{ return m_identity; }
    [[nodiscard]] Point& position()noexcept{ return m_position; }
    [[nodiscard]] const Point& position()const noexcept{ return m_position; }
    [[nodiscard]] InputSource& source()noexcept{ return m_source; }
    [[nodiscard]] b8& known()noexcept{ return m_known; }
    [[nodiscard]] b8 known()const noexcept{ return m_known; }


private:
    u64 m_activityGeneration = 1u;
    WidgetId m_target;
    HoverIdentity m_identity;
    Point m_position;
    InputSource m_source;
    b8 m_known = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

