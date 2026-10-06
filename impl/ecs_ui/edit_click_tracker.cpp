// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_click_tracker.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiEditClickKind::Enum UiEditClickTracker::press(const UiTextEditOwner& owner, const Ui::PopupToken& popup,
    const u64 revision, const u64 externalRevision, const Ui::Point position, const u64 timestampMs, const bool shift)noexcept{
    if(!owner.valid() || timestampMs == 0u || shift || !IsFinite(position.x) || !IsFinite(position.y)){
        cancel();
        return UiEditClickKind::Caret;
    }
    const f64 dx = static_cast<f64>(position.x) - static_cast<f64>(m_position.x);
    const f64 dy = static_cast<f64>(position.y) - static_cast<f64>(m_position.y);
    const bool second = m_released && m_clickCount == 1u && owner == m_owner && popup == m_popup
        && revision == m_revision && externalRevision == m_externalRevision
        && timestampMs >= m_pressTimestampMs && timestampMs - m_pressTimestampMs <= 500u
        && dx * dx + dy * dy <= 16.0;
    m_owner = owner;
    m_popup = popup;
    m_position = position;
    m_revision = revision;
    m_externalRevision = externalRevision;
    m_pressTimestampMs = timestampMs;
    m_clickCount = second ? 2u : 1u;
    m_down = true;
    m_released = false;
    return second ? UiEditClickKind::Word : UiEditClickKind::Caret;
}

void UiEditClickTracker::move(const Ui::Point position)noexcept{
    if(!m_down)
        return;
    if(!IsFinite(position.x) || !IsFinite(position.y)){
        cancel();
        return;
    }
    const f64 dx = static_cast<f64>(position.x) - static_cast<f64>(m_position.x);
    const f64 dy = static_cast<f64>(position.y) - static_cast<f64>(m_position.y);
    if(dx * dx + dy * dy > 16.0)
        cancel();
}

void UiEditClickTracker::release(const UiTextEditOwner& owner)noexcept{
    if(m_down && m_owner == owner){
        m_down = false;
        m_released = true;
    }
    else
        cancel();
}

void UiEditClickTracker::retainFocus(const UiTextEditOwner* owner)noexcept{
    if(m_clickCount != 0u && (!owner || *owner != m_owner))
        cancel();
}

void UiEditClickTracker::cancel()noexcept{
    m_owner = {};
    m_popup = {};
    m_position = {};
    m_revision = 0u;
    m_externalRevision = 0u;
    m_pressTimestampMs = 0u;
    m_clickCount = 0u;
    m_down = false;
    m_released = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

