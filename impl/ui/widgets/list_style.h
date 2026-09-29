// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "style.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SelectableStyle{
    Name normal = Name("list.row.normal");
    Name hover = Name("list.row.hover");
    Name selected = Name("list.row.selected");
    Name disabled = Name("list.row.disabled");
    Name fallback = Name("button.normal");
    Name hoverFallback = Name("button.hover");
    Name selectedFallback = Name("button.pressed");
    Name disabledFallback = Name("button.disabled");
    Name focus = Name("focus.overlay");
    Insets padding = { 8.0f, 4.0f, 8.0f, 4.0f };
};

struct ListStyle{
    SelectableStyle row;
    Name background = Name("list.background");
    Name backgroundFallback = Name("panel.normal");
    Name track = Name("scroll.track");
    Name trackFallback = Name("panel.normal");
    Name thumb = Name("scroll.thumb");
    Name thumbFallback = Name("button.normal");
    Name thumbHover = Name("button.hover");
    Insets padding = { 4.0f, 4.0f, 4.0f, 4.0f };
    f32 scrollbarWidth = 14.0f;
    f32 minimumThumb = 20.0f;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

