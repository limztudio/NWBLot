// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "text_edit_session.h"

#include <impl/ecs_ui/toolkit/input/popup.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiEditClickKind{
    enum Enum : u8{ Caret, Word };
};

// Editor-local policy: a released second press within 500 ms and four logical units selects a word run.
class UiEditClickTracker final{
public:
    [[nodiscard]] UiEditClickKind::Enum press(const UiTextEditOwner& owner, const Ui::PopupToken& popup,
        u64 revision, u64 externalRevision, Ui::Point position, u64 timestampMs, bool shift)noexcept;
    void move(Ui::Point position)noexcept;
    void release(const UiTextEditOwner& owner)noexcept;
    void retainFocus(const UiTextEditOwner* owner)noexcept;
    void cancel()noexcept;


private:
    UiTextEditOwner m_owner;
    Ui::PopupToken m_popup;
    Ui::Point m_position;
    u64 m_revision = 0u;
    u64 m_externalRevision = 0u;
    u64 m_pressTimestampMs = 0u;
    u8 m_clickCount = 0u;
    bool m_down = false;
    bool m_released = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

