// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../layout/tree.h"
#include "../text/types.h"

#include <impl/assets_ui_skin/region_names.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Default metrics and colors shared by the widget styles and options.
inline constexpr f32 s_StyleSpacingSmall = 4.0f;
inline constexpr f32 s_StyleSpacingMedium = 8.0f;
inline constexpr f32 s_StyleFieldPaddingY = 6.0f;
inline constexpr f32 s_StyleControlExtent = 24.0f;
inline constexpr f32 s_StyleControlHeight = 32.0f;
inline constexpr f32 s_StyleMinimumFieldWidth = 120.0f;
inline constexpr f32 s_StyleSliderTrackHeight = 12.0f;
inline constexpr f32 s_StyleScrollbarThickness = 14.0f;
inline constexpr f32 s_StyleScrollbarMinimumThumb = 20.0f;
inline constexpr f32 s_StyleScrollWheelStep = 3.0f;
inline constexpr f32 s_StyleMarkInset = 0.3f;
inline constexpr f32 s_StyleDisabledIconAlpha = 0.5f;
inline constexpr Color s_StyleTextColor = { 0.92f, 0.94f, 0.98f, 1.0f };
inline constexpr Color s_StyleDisabledTextColor = { 0.48f, 0.50f, 0.55f, 1.0f };
inline constexpr Color s_StyleInsetColor = { 0.08f, 0.10f, 0.14f, 1.0f };
inline constexpr Color s_StyleHoverTint = { 1.08f, 1.08f, 1.08f, 1.0f };
inline constexpr Color s_StylePressedTint = { 0.85f, 0.85f, 0.85f, 1.0f };
inline constexpr Color s_StyleDisabledTint = { 0.55f, 0.55f, 0.55f, 0.6f };

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
    Color text = s_StyleTextColor;
    Color disabledText = s_StyleDisabledTextColor;
    f32 fontSize = s_TextDefaultFontSize;
    f32 gap = s_StyleSpacingMedium;
    f32 checkboxExtent = s_StyleControlExtent;
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
    f32 gap = s_StyleSpacingMedium;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

