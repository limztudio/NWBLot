// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "toolkit_contract.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_skin_toolkit_contract{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr Name s_RequiredRegions[] = {
    Name("panel.normal"),
    Name("window.normal"), Name("window.title"), Name("window.collapse"),
    Name("separator"), Name("button.normal"), Name("checkbox.normal"), Name("checkbox.mark"),
    Name("edit.normal"),
    Name("list.background"), Name("list.row.normal"), Name("list.row.hover"),
    Name("list.row.selected"), Name("list.row.disabled"),
    Name("scroll.track"), Name("scroll.thumb"),
    Name("scrollbar.track"), Name("scrollbar.thumb.normal"),
    Name("popup.normal"), Name("tooltip.normal"),
    Name("combo.normal"), Name("combo.arrow"),
    Name("radio.normal"), Name("radio.checked"), Name("radio.mark"),
    Name("slider.track"), Name("slider.thumb.normal"),
    Name("progress.track"), Name("progress.fill"),
    Name("focus.overlay"),
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ValidateUiSkinToolkitContract(const UiSkin& skin){
    if(!skin.validatePayload())
        return false;
    for(const Name& name : __hidden_ui_skin_toolkit_contract::s_RequiredRegions){
        if(!skin.findRegion(name)){
            NWB_LOGGER_ERROR(NWB_TEXT("UI skin toolkit contract 'widgets_v1' failed: missing required region '{}'")
                , StringConvert(name.c_str())
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

