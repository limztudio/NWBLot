// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "style.h"

#include <impl/assets_ui_skin/region_names.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Color s_ScrollbarThumbColor = { 0.35f, 0.40f, 0.48f, 1.0f };
inline constexpr Color s_ScrollbarDisabledColor = { 0.25f, 0.28f, 0.32f, 1.0f };

struct ScrollbarStyle{
    Name track = UiSkinToolkitRegions::s_ScrollTrackRegionName;
    Name trackFallback = UiSkinToolkitRegions::s_PanelNormalRegionName;
    Name thumb = UiSkinToolkitRegions::s_ScrollThumbRegionName;
    Name thumbHover = UiSkinToolkitRegions::s_ButtonHoverRegionName;
    Name thumbPressed = UiSkinToolkitRegions::s_ButtonPressedRegionName;
    Name thumbDisabled = UiSkinToolkitRegions::s_ButtonDisabledRegionName;
    Name thumbFallback = UiSkinToolkitRegions::s_ButtonNormalRegionName;
    Color trackColor = s_StyleInsetColor;
    Color thumbColor = s_ScrollbarThumbColor;
    Color disabledColor = s_ScrollbarDisabledColor;
    f32 thickness = s_StyleScrollbarThickness;
    f32 minimumThumb = s_StyleScrollbarMinimumThumb;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

