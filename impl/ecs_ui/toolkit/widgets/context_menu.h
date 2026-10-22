// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "list.h"
#include "popup.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ContextMenuOptions{
    Point size = s_PopupDefaultSize;
    f32 wheelRows = s_StyleScrollWheelStep;
    bool enabled = true;
};

struct ContextMenuResult{
    bool valid = false;
    bool opened = false;
    bool closed = false;
    bool activated = false;
    u64 key = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ContextMenuState final : NoCopy{
    friend class Builder;


public:
    ContextMenuState()noexcept = default;
    ContextMenuState(ContextMenuState&&) = delete;


public:
    ContextMenuState& operator=(ContextMenuState&&) = delete;


public:
    [[nodiscard]] bool open(const Rect& anchor)noexcept;
    void close()noexcept;
    [[nodiscard]] bool isOpen()const noexcept{ return m_popup.isOpen(); }

    [[nodiscard]] u64 instanceGeneration()const noexcept{ return m_popup.instanceGeneration(); }

    [[nodiscard]] u64 revision()const noexcept{ return m_revision; }

    [[nodiscard]] u64 cursorKey()const noexcept{ return m_list.cursorKey(); }

    [[nodiscard]] const ListState& listState()const noexcept{ return m_list; }

    [[nodiscard]] const PopupPlacement& placement()const noexcept{ return m_popup.placement(); }


private:
    void advanceRevision()noexcept;


private:
    PopupState m_popup;
    ListState m_list;
    WidgetId m_owner;
    WidgetId m_anchorWidget;
    u64 m_ownerDeclaration = 0u;
    u64 m_anchorDeclaration = 0u;
    u64 m_revision = 1u;
    Rect m_anchor;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

