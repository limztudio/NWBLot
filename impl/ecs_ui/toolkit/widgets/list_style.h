// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "style.h"

#include <impl/assets_ui_skin/region_names.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SelectableStyle{
    Name normal = UiSkinToolkitRegions::s_ListRowNormalRegionName;
    Name hover = UiSkinToolkitRegions::s_ListRowHoverRegionName;
    Name selected = UiSkinToolkitRegions::s_ListRowSelectedRegionName;
    Name disabled = UiSkinToolkitRegions::s_ListRowDisabledRegionName;
    Name fallback = UiSkinToolkitRegions::s_ButtonNormalRegionName;
    Name hoverFallback = UiSkinToolkitRegions::s_ButtonHoverRegionName;
    Name selectedFallback = UiSkinToolkitRegions::s_ButtonPressedRegionName;
    Name disabledFallback = UiSkinToolkitRegions::s_ButtonDisabledRegionName;
    Name focus = UiSkinToolkitRegions::s_FocusOverlayRegionName;
    Insets padding = { s_StyleSpacingMedium, s_StyleSpacingSmall, s_StyleSpacingMedium, s_StyleSpacingSmall };
};

struct ListStyle{
    SelectableStyle row;
    Name background = UiSkinToolkitRegions::s_ListBackgroundRegionName;
    Name backgroundFallback = UiSkinToolkitRegions::s_PanelNormalRegionName;
    Name track = UiSkinToolkitRegions::s_ScrollTrackRegionName;
    Name trackFallback = UiSkinToolkitRegions::s_PanelNormalRegionName;
    Name thumb = UiSkinToolkitRegions::s_ScrollThumbRegionName;
    Name thumbFallback = UiSkinToolkitRegions::s_ButtonNormalRegionName;
    Name thumbHover = UiSkinToolkitRegions::s_ButtonHoverRegionName;
    Insets padding = { s_StyleSpacingSmall, s_StyleSpacingSmall, s_StyleSpacingSmall, s_StyleSpacingSmall };
    f32 scrollbarWidth = s_StyleScrollbarThickness;
    f32 minimumThumb = s_StyleScrollbarMinimumThumb;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

