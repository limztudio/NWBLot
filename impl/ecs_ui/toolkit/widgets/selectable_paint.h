// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/widgets/list_style.adhd.h>
#include "../text/service.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Stable across interaction states so hover, selection and disabling cannot change text placement.
struct SelectableMetrics{
    Insets padding;
    Point minimum;
};

struct SelectablePaintFlags{
    b8 enabled = true;
    b8 selected = false;
    b8 hover = false;
    b8 focused = false;
};

class SelectablePainter final{
public:
    [[nodiscard]] static Expected<SelectableMetrics> Measure(const UiSkin& skin, const SelectableStyle& style)noexcept;
    // Use the same display-scale visual bounds and resolved padding that sized the control.
    [[nodiscard]] static b8 Paint(
        PaintBuilder& paint, TextService& text, const UiSkin& skin, const TextLayout& layout,
        const Rect& textBounds, const Insets& padding, const Rect& bounds, const Rect& clip, const SelectableStyle& style,
        const SelectablePaintFlags& flags, const Color& textColor, const Color& disabledTextColor
    );
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

