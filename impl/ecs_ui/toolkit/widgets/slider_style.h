// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "style.h"
#include "../paint.h"

#include <impl/assets_ui_skin/region_names.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SliderStyle{
    Name track = UiSkinToolkitRegions::s_SliderTrackRegionName;
    Name normal = UiSkinToolkitRegions::s_SliderThumbNormalRegionName;
    Name hover = UiSkinToolkitRegions::s_SliderThumbHoverRegionName;
    Name pressed = UiSkinToolkitRegions::s_SliderThumbPressedRegionName;
    Name disabled = UiSkinToolkitRegions::s_SliderThumbDisabledRegionName;
    Name trackFallback = UiSkinToolkitRegions::s_ScrollbarTrackRegionName;
    Name thumbFallback = UiSkinToolkitRegions::s_ScrollbarThumbNormalRegionName;
    Name focus = UiSkinToolkitRegions::s_FocusOverlayRegionName;
    Color hoverTint = s_StyleHoverTint;
    Color pressedTint = s_StylePressedTint;
    Color disabledTint = s_StyleDisabledTint;
    Insets padding = { s_StyleSpacingSmall, s_StyleSpacingSmall, s_StyleSpacingSmall, s_StyleSpacingSmall };
    Point thumbExtent = { s_StyleControlExtent, s_StyleControlExtent };
    f32 trackHeight = s_StyleSliderTrackHeight;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

