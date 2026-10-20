// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "style.h"
#include "../paint.h"

#include <impl/assets_ui_skin/region_names.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct RadioGroupStyle{
    Name normal = UiSkinToolkitRegions::s_RadioNormalRegionName;
    Name hover = UiSkinToolkitRegions::s_RadioHoverRegionName;
    Name pressed = UiSkinToolkitRegions::s_RadioPressedRegionName;
    Name checked = UiSkinToolkitRegions::s_RadioCheckedRegionName;
    Name disabled = UiSkinToolkitRegions::s_RadioDisabledRegionName;
    Name mark = UiSkinToolkitRegions::s_RadioMarkRegionName;
    Name fallback = UiSkinToolkitRegions::s_CheckboxNormalRegionName;
    Name checkedFallback = UiSkinToolkitRegions::s_CheckboxCheckedRegionName;
    Name markFallback = UiSkinToolkitRegions::s_CheckboxMarkRegionName;
    Name focus = UiSkinToolkitRegions::s_FocusOverlayRegionName;
    Color hoverTint = s_StyleHoverTint;
    Color pressedTint = s_StylePressedTint;
    Color disabledTint = s_StyleDisabledTint;
    Insets padding = { s_StyleSpacingSmall, s_StyleSpacingSmall, s_StyleSpacingSmall, s_StyleSpacingSmall };
    f32 rowGap = s_StyleSpacingSmall;
    f32 indicatorExtent = s_StyleControlExtent;
    f32 gap = s_StyleSpacingMedium;
    // Fraction of the indicator extent inset on each side of its checked mark.
    f32 markInset = s_StyleMarkInset;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

