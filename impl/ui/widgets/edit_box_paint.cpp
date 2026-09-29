// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_box.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_edit_box_paint{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ValidColor(const Color& color){
    return
        IsFinite(color.r) && IsFinite(color.g) && IsFinite(color.b) && IsFinite(color.a)
        && color.a >= 0.0f && color.a <= 1.0f
    ;
}

[[nodiscard]] static const UiSkinRegion* Background(const UiSkin& skin, const EditBoxStyle& style,
    const EditBoxPaintFlags& flags){
    const Name& preferred = !flags.enabled ? style.disabled : flags.focused ? style.focused
        : flags.hovered ? style.hover : style.normal;
    const UiSkinRegion* region = skin.findRegion(preferred);
    if(!region)
        region = skin.findRegion(style.normal);
    if(!region)
        region = skin.findRegion(style.fallback);
    return region;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool EditBoxView::paint(TextService& text, PaintBuilder& paint, const UiSkin& skin,
    const EditBoxPlacement& placement, const EditBoxStyle& style, const EditBoxPaintFlags& flags)const{
    if(
        !m_ready || !__hidden_ui_edit_box_paint::ValidColor(style.background)
        || !__hidden_ui_edit_box_paint::ValidColor(style.text) || !__hidden_ui_edit_box_paint::ValidColor(style.disabledText)
        || !__hidden_ui_edit_box_paint::ValidColor(style.selection)
        || !__hidden_ui_edit_box_paint::ValidColor(style.inactiveSelection)
        || !__hidden_ui_edit_box_paint::ValidColor(style.caret) || !__hidden_ui_edit_box_paint::ValidColor(style.preedit)
    )
        return false;
    paint.pushClip(placement.frameClip);
    bool painted = true;
    if(const UiSkinRegion* background = __hidden_ui_edit_box_paint::Background(skin, style, flags))
        painted = paint.drawRegion(background->name, placement.bounds);
    else
        paint.fillRect(placement.bounds, style.background);
    if(painted && flags.focused && flags.enabled && skin.findRegion(style.focus))
        painted = paint.drawRegion(style.focus, placement.bounds);
    paint.pushClip(placement.clip);
    if(painted){
        const Color& selection = flags.focused && flags.enabled ? style.selection : style.inactiveSelection;
        paint.fillRect(placement.selection, selection);
        painted = text.paint(paint, m_layout, placement.textOrigin, flags.enabled ? style.text : style.disabledText);
    }
    if(painted && m_composing && flags.focused && flags.enabled)
        paint.fillRect(placement.preeditUnderline, style.preedit);
    if(
        painted && flags.focused && flags.enabled && !flags.readOnly
        && (m_composing ? flags.preeditCaretVisible : flags.caretVisible)
    )
        paint.fillRect(placement.caret, style.caret);
    const bool contentPopped = paint.popClip();
    const bool framePopped = paint.popClip();
    return painted && contentPopped && framePopped;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

