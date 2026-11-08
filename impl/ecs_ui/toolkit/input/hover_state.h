// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/input/input.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class InputHoverState final{
public:
    void setTarget(const Span<const HitTarget> targets, const Span<const TargetLookup> lookup, const Span<const PopupRecord> popups, const HitTarget* const hovered)noexcept{
        SetRouterHoverTarget(targets, lookup, popups, hovered != nullptr, hovered != nullptr ? *hovered : HitTarget{}, m_activityGeneration, m_identity, m_target);
    }
    void advanceActivity()noexcept{ AdvanceRouterHoverActivity(m_activityGeneration); }
    void clearTarget()noexcept{ ClearRouterHoverTarget(m_target, m_identity); }
    void forgetSource()noexcept{ ForgetRouterHoverSource(m_activityGeneration, m_target, m_identity, m_known, m_source); }

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

