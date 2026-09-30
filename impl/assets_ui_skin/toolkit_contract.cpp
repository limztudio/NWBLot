// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "toolkit_contract.h"

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

static constexpr RequiredRegion s_RequiredRegions[] = {
    { Name("panel.normal"), "panel.normal" },
    { Name("window.normal"), "window.normal" },
    { Name("window.title"), "window.title" },
    { Name("window.collapse"), "window.collapse" },
    { Name("separator"), "separator" },
    { Name("button.normal"), "button.normal" },
    { Name("checkbox.normal"), "checkbox.normal" },
    { Name("checkbox.mark"), "checkbox.mark" },
    { Name("edit.normal"), "edit.normal" },
    { Name("list.background"), "list.background" },
    { Name("list.row.normal"), "list.row.normal" },
    { Name("list.row.hover"), "list.row.hover" },
    { Name("list.row.selected"), "list.row.selected" },
    { Name("list.row.disabled"), "list.row.disabled" },
    { Name("scroll.track"), "scroll.track" },
    { Name("scroll.thumb"), "scroll.thumb" },
    { Name("scrollbar.track"), "scrollbar.track" },
    { Name("scrollbar.thumb.normal"), "scrollbar.thumb.normal" },
    { Name("popup.normal"), "popup.normal" },
    { Name("tooltip.normal"), "tooltip.normal" },
    { Name("combo.normal"), "combo.normal" },
    { Name("combo.arrow"), "combo.arrow" },
    { Name("radio.normal"), "radio.normal" },
    { Name("radio.checked"), "radio.checked" },
    { Name("radio.mark"), "radio.mark" },
    { Name("slider.track"), "slider.track" },
    { Name("slider.thumb.normal"), "slider.thumb.normal" },
    { Name("progress.track"), "progress.track" },
    { Name("progress.fill"), "progress.fill" },
    { Name("focus.overlay"), "focus.overlay" },
};


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
    if(!skin.findRegion(Name("window.resize")) && !skin.findRegion(Name("white"))){
        NWB_LOGGER_ERROR(NWB_TEXT("UI skin toolkit contract 'widgets_v1' failed: window resizing requires 'window.resize' or 'white'"));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

