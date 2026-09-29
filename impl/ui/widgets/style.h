// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../layout/tree.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Semantic atlas names let another skin replace imagery and control metrics without application changes.
struct WidgetStyle{
    Name panel = Name("panel.normal");
    Name window = Name("window.normal");
    Name windowTitle = Name("window.title");
    Name windowCollapse = Name("window.collapse");
    Name windowResize = Name("window.resize");
    Name separator = Name("separator");
    Name white = Name("white");
    Name button = Name("button.normal");
    Name buttonHover = Name("button.hover");
    Name buttonPressed = Name("button.pressed");
    Name buttonDisabled = Name("button.disabled");
    Name checkbox = Name("checkbox.normal");
    Name checkboxHover = Name("checkbox.hover");
    Name checkboxChecked = Name("checkbox.checked");
    Name checkboxDisabled = Name("checkbox.disabled");
    Name checkboxMark = Name("checkbox.mark");
    Name focus = Name("focus.overlay");
    Color text = { 0.92f, 0.94f, 0.98f, 1.0f };
    Color disabledText = { 0.48f, 0.50f, 0.55f, 1.0f };
    f32 fontSize = 16.0f;
    f32 gap = 8.0f;
    f32 checkboxExtent = 24.0f;
    f32 windowCollapseExtent = 20.0f;
    f32 windowResizeExtent = 16.0f;
};

struct WidgetOptions{
    LayoutSize width;
    LayoutSize height;
    bool enabled = true;
};

struct ContainerOptions{
    LayoutSize width = { LayoutSizePolicy::Stretch, 1.0f };
    LayoutSize height;
    Insets padding;
    f32 gap = 8.0f;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

