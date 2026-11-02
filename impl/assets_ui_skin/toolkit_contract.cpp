// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "toolkit_contract.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_skin_toolkit_contract{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_UI_SKIN_REQUIRED_REGION(Text) UiSkinRequiredRegion(Name(Text), Text),
static const Array<UiSkinRequiredRegion, 30u> s_RequiredRegions = {
    NWB_UI_SKIN_REQUIRED_REGION("panel.normal")
    NWB_UI_SKIN_REQUIRED_REGION("window.normal")
    NWB_UI_SKIN_REQUIRED_REGION("window.title")
    NWB_UI_SKIN_REQUIRED_REGION("window.collapse")
    NWB_UI_SKIN_REQUIRED_REGION("separator")
    NWB_UI_SKIN_REQUIRED_REGION("button.normal")
    NWB_UI_SKIN_REQUIRED_REGION("checkbox.normal")
    NWB_UI_SKIN_REQUIRED_REGION("checkbox.mark")
    NWB_UI_SKIN_REQUIRED_REGION("edit.normal")
    NWB_UI_SKIN_REQUIRED_REGION("list.background")
    NWB_UI_SKIN_REQUIRED_REGION("list.row.normal")
    NWB_UI_SKIN_REQUIRED_REGION("list.row.hover")
    NWB_UI_SKIN_REQUIRED_REGION("list.row.selected")
    NWB_UI_SKIN_REQUIRED_REGION("list.row.disabled")
    NWB_UI_SKIN_REQUIRED_REGION("scroll.track")
    NWB_UI_SKIN_REQUIRED_REGION("scroll.thumb")
    NWB_UI_SKIN_REQUIRED_REGION("scrollbar.track")
    NWB_UI_SKIN_REQUIRED_REGION("scrollbar.thumb.normal")
    NWB_UI_SKIN_REQUIRED_REGION("popup.normal")
    NWB_UI_SKIN_REQUIRED_REGION("tooltip.normal")
    NWB_UI_SKIN_REQUIRED_REGION("combo.normal")
    NWB_UI_SKIN_REQUIRED_REGION("combo.arrow")
    NWB_UI_SKIN_REQUIRED_REGION("radio.normal")
    NWB_UI_SKIN_REQUIRED_REGION("radio.checked")
    NWB_UI_SKIN_REQUIRED_REGION("radio.mark")
    NWB_UI_SKIN_REQUIRED_REGION("slider.track")
    NWB_UI_SKIN_REQUIRED_REGION("slider.thumb.normal")
    NWB_UI_SKIN_REQUIRED_REGION("progress.track")
    NWB_UI_SKIN_REQUIRED_REGION("progress.fill")
    NWB_UI_SKIN_REQUIRED_REGION("focus.overlay")
};
#undef NWB_UI_SKIN_REQUIRED_REGION


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ValidateUiSkinToolkitContract(const UiSkin& skin){
    if(!skin.validatePayload())
        return false;
    for(const auto& required : __hidden_ui_skin_toolkit_contract::s_RequiredRegions){
        if(!skin.findRegion(required.name)){
            NWB_LOGGER_ERROR(NWB_TEXT("UI skin toolkit contract 'widgets' failed: missing required region '{}'"), ConvertString(required.text));
            return false;
        }
    }
    if(!skin.findRegion(s_WindowResizeRegionName) && !skin.findRegion(s_WhiteRegionName)){
        NWB_LOGGER_ERROR(NWB_TEXT("UI skin toolkit contract 'widgets' failed: window resizing requires 'window.resize' or 'white'"));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

