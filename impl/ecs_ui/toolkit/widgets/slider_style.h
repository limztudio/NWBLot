// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


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
    Color hoverTint = { 1.08f, 1.08f, 1.08f, 1.0f };
    Color pressedTint = { 0.85f, 0.85f, 0.85f, 1.0f };
    Color disabledTint = { 0.55f, 0.55f, 0.55f, 0.6f };
    Insets padding = { 4.0f, 4.0f, 4.0f, 4.0f };
    Point thumbExtent = { 24.0f, 24.0f };
    f32 trackHeight = 12.0f;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

