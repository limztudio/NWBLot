// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../layout/tree.h"
#include <impl/assets_ui_skin/region_names.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Semantic atlas names let another skin replace imagery and control metrics without application changes.
struct WidgetStyle{
    Name panel = UiSkinToolkitRegions::s_PanelNormalRegionName;
    Name window = UiSkinToolkitRegions::s_WindowNormalRegionName;
    Name windowTitle = UiSkinToolkitRegions::s_WindowTitleRegionName;
    Name windowCollapse = UiSkinToolkitRegions::s_WindowCollapseRegionName;
    Name windowResize = UiSkinToolkitRegions::s_WindowResizeRegionName;
    Name separator = UiSkinToolkitRegions::s_SeparatorRegionName;
    Name white = UiSkinToolkitRegions::s_WhiteRegionName;
    Name button = UiSkinToolkitRegions::s_ButtonNormalRegionName;
    Name buttonHover = UiSkinToolkitRegions::s_ButtonHoverRegionName;
    Name buttonPressed = UiSkinToolkitRegions::s_ButtonPressedRegionName;
    Name buttonDisabled = UiSkinToolkitRegions::s_ButtonDisabledRegionName;
    Name checkbox = UiSkinToolkitRegions::s_CheckboxNormalRegionName;
    Name checkboxHover = UiSkinToolkitRegions::s_CheckboxHoverRegionName;
    Name checkboxChecked = UiSkinToolkitRegions::s_CheckboxCheckedRegionName;
    Name checkboxDisabled = UiSkinToolkitRegions::s_CheckboxDisabledRegionName;
    Name checkboxMark = UiSkinToolkitRegions::s_CheckboxMarkRegionName;
    Name focus = UiSkinToolkitRegions::s_FocusOverlayRegionName;
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

