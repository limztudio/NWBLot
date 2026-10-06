// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../id.h"
#include "../paint.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct PopupToken{
    WidgetId widget;
    u64 declarationGeneration = 0u;
    u64 instanceGeneration = 0u;
    u64 openGeneration = 0u;

    [[nodiscard]] bool valid()const noexcept{
        return widget.valid() && declarationGeneration != 0u && instanceGeneration != 0u && openGeneration != 0u;
    }
    [[nodiscard]] bool empty()const noexcept{
        return !widget.valid() && declarationGeneration == 0u && instanceGeneration == 0u && openGeneration == 0u;
    }
};

[[nodiscard]] inline bool operator==(const PopupToken& lhs, const PopupToken& rhs)noexcept{
    return
        lhs.widget == rhs.widget && lhs.declarationGeneration == rhs.declarationGeneration
        && lhs.instanceGeneration == rhs.instanceGeneration && lhs.openGeneration == rhs.openGeneration
    ;
}

namespace PopupDismissReason{
    enum Enum : u8{ None, Cancel, OutsideClick, FocusTraversal, FocusLost };
};

struct PopupScope{
    PopupToken token;
    Rect bounds;
    Rect viewport;
    u32 layer = 0u;
    bool modal = false;
    bool dismissOutside = true;
    bool dismissCancel = true;
    bool dismissFocusTraversal = false;
    bool autofocus = true;
    PopupToken parent;
    WidgetId focusAnchor;
    u64 focusAnchorDeclarationGeneration = 0u;
};

struct PopupDismissal{
    PopupToken token;
    PopupDismissReason::Enum reason = PopupDismissReason::None;
};

inline constexpr usize s_InputMaxPopups = 8u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

