// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


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
    Color hoverTint = { 1.08f, 1.08f, 1.08f, 1.0f };
    Color pressedTint = { 0.85f, 0.85f, 0.85f, 1.0f };
    Color disabledTint = { 0.55f, 0.55f, 0.55f, 0.6f };
    Insets padding = { 4.0f, 4.0f, 4.0f, 4.0f };
    f32 rowGap = 4.0f;
    f32 indicatorExtent = 24.0f;
    f32 gap = 8.0f;
    // Fraction of the indicator extent inset on each side of its checked mark.
    f32 markInset = 0.3f;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

