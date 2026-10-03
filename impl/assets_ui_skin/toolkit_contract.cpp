// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "toolkit_contract.h"

#include "region_names.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_skin_toolkit_contract{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct RequiredRegion{
    Name name;
    StringView text;
};

#define NWB_UI_SKIN_REQUIRED_REGION(Key, Text) { UiSkinToolkitRegions::s_##Key##RegionName, UiSkinToolkitRegions::s_##Key##RegionText },
static constexpr RequiredRegion s_RequiredRegions[] = {
    NWB_UI_SKIN_REQUIRED_REGION(PanelNormal, "panel.normal")
    NWB_UI_SKIN_REQUIRED_REGION(WindowNormal, "window.normal")
    NWB_UI_SKIN_REQUIRED_REGION(WindowTitle, "window.title")
    NWB_UI_SKIN_REQUIRED_REGION(WindowCollapse, "window.collapse")
    NWB_UI_SKIN_REQUIRED_REGION(Separator, "separator")
    NWB_UI_SKIN_REQUIRED_REGION(ButtonNormal, "button.normal")
    NWB_UI_SKIN_REQUIRED_REGION(CheckboxNormal, "checkbox.normal")
    NWB_UI_SKIN_REQUIRED_REGION(CheckboxMark, "checkbox.mark")
    NWB_UI_SKIN_REQUIRED_REGION(EditNormal, "edit.normal")
    NWB_UI_SKIN_REQUIRED_REGION(ListBackground, "list.background")
    NWB_UI_SKIN_REQUIRED_REGION(ListRowNormal, "list.row.normal")
    NWB_UI_SKIN_REQUIRED_REGION(ListRowHover, "list.row.hover")
    NWB_UI_SKIN_REQUIRED_REGION(ListRowSelected, "list.row.selected")
    NWB_UI_SKIN_REQUIRED_REGION(ListRowDisabled, "list.row.disabled")
    NWB_UI_SKIN_REQUIRED_REGION(ScrollTrack, "scroll.track")
    NWB_UI_SKIN_REQUIRED_REGION(ScrollThumb, "scroll.thumb")
    NWB_UI_SKIN_REQUIRED_REGION(ScrollbarTrack, "scrollbar.track")
    NWB_UI_SKIN_REQUIRED_REGION(ScrollbarThumbNormal, "scrollbar.thumb.normal")
    NWB_UI_SKIN_REQUIRED_REGION(PopupNormal, "popup.normal")
    NWB_UI_SKIN_REQUIRED_REGION(TooltipNormal, "tooltip.normal")
    NWB_UI_SKIN_REQUIRED_REGION(ComboNormal, "combo.normal")
    NWB_UI_SKIN_REQUIRED_REGION(ComboArrow, "combo.arrow")
    NWB_UI_SKIN_REQUIRED_REGION(RadioNormal, "radio.normal")
    NWB_UI_SKIN_REQUIRED_REGION(RadioChecked, "radio.checked")
    NWB_UI_SKIN_REQUIRED_REGION(RadioMark, "radio.mark")
    NWB_UI_SKIN_REQUIRED_REGION(SliderTrack, "slider.track")
    NWB_UI_SKIN_REQUIRED_REGION(SliderThumbNormal, "slider.thumb.normal")
    NWB_UI_SKIN_REQUIRED_REGION(ProgressTrack, "progress.track")
    NWB_UI_SKIN_REQUIRED_REGION(ProgressFill, "progress.fill")
    NWB_UI_SKIN_REQUIRED_REGION(FocusOverlay, "focus.overlay")
};
#undef NWB_UI_SKIN_REQUIRED_REGION


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ValidateUiSkinToolkitContract(const UiSkin& skin){
    if(!skin.validatePayload())
        return false;
    for(const auto& required : __hidden_ui_skin_toolkit_contract::s_RequiredRegions){
        if(!skin.findRegion(required.name)){
            NWB_LOGGER_ERROR(NWB_TEXT("UI skin toolkit contract 'widgets_v1' failed: missing required region '{}'")
                , StringConvert(required.text)
            );
            return false;
        }
    }
    if(!skin.findRegion(UiSkinToolkitRegions::s_WindowResizeRegionName) && !skin.findRegion(UiSkinToolkitRegions::s_WhiteRegionName)){
        NWB_LOGGER_ERROR(NWB_TEXT("UI skin toolkit contract 'widgets_v1' failed: window resizing requires 'window.resize' or 'white'"));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

