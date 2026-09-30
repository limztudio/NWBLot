// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "list.h"
#include "popup.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ContextMenuOptions{
    Point size = { 220.0f, 160.0f };
    f32 rowHeight = 28.0f;
    f32 wheelRows = 3.0f;
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
    ContextMenuState() = default;
    ContextMenuState(ContextMenuState&&) = delete;
    ContextMenuState& operator=(ContextMenuState&&) = delete;


public:
    [[nodiscard]] bool open(const Rect& anchor);
    void close();
    [[nodiscard]] bool isOpen()const{ return m_popup.isOpen(); }
    [[nodiscard]] u64 instanceGeneration()const{ return m_popup.instanceGeneration(); }
    [[nodiscard]] u64 revision()const{ return m_revision; }
    [[nodiscard]] u64 cursorKey()const{ return m_list.cursorKey(); }
    [[nodiscard]] const ListState& listState()const{ return m_list; }
    [[nodiscard]] const PopupPlacement& placement()const{ return m_popup.placement(); }


private:
    void advanceRevision();


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

