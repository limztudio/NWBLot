// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/widgets/popup_style.adhd.h>
#include "../paint.h"
#include <impl/ecs_ui/toolkit/input/popup.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The host lends state through the outermost popup end; each opening binds to its exact parent lifetime.
class PopupState final : NoCopy{
    friend class Builder;


public:
    PopupState()noexcept;
    PopupState(PopupState&&) = delete;


public:
    PopupState& operator=(PopupState&&) = delete;


public:
    void open()noexcept;
    void close()noexcept{ m_open = false; }

    [[nodiscard]] b8 isOpen()const noexcept{ return m_open; }

    [[nodiscard]] u64 instanceGeneration()const noexcept{ return m_instanceGeneration; }

    [[nodiscard]] u64 openGeneration()const noexcept{ return m_openGeneration; }

    [[nodiscard]] const PopupPlacement& placement()const noexcept{ return m_placement; }


private:
    void bindParent(const PopupToken& parent)noexcept;


private:
    const u64 m_instanceGeneration;
    u64 m_openGeneration = 0u;
    PopupPlacement m_placement;
    b8 m_parentBound = false;
    b8 m_open = false;
    PopupToken m_parent;
    u64 m_parentOpenGeneration = 0u;
};

class PopupLayout final{
public:
    // Anchored placement flips on its requested axis; oversized content shrinks to the logical viewport.
    [[nodiscard]] static Expected<PopupPlacement> Place(const PopupOptions& options, const DisplayMetrics& display)noexcept;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

