// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "numeric_edit_scene.h"

#include "../smoke_environment.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiNumericEditSmokeScene::UiNumericEditSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input)
    : m_arena(arena)
    , m_input(input)
    , m_integer(arena)
    , m_float(arena)
    , m_clipboard(arena)
{
    resetModels();
    m_input.addHandlerToBack(*this);
}

UiNumericEditSmokeScene::~UiNumericEditSmokeScene(){
    m_input.removeHandler(*this);
}

bool UiNumericEditSmokeScene::paint(Impl::UiPaintContext& context){
    using namespace Impl::Ui;
    Builder& ui = context.ui;
    ui.style().fontSize = 16.0f;
    ui.style().gap = 8.0f;
    if(!ui.beginPanel("numeric_edit_fixture", { 24.0f, 24.0f, 480.0f, 340.0f }))
        return false;
    const WidgetOptions title{ {}, { LayoutSizePolicy::Fixed, 24.0f } };
    const WidgetOptions caption{ {}, { LayoutSizePolicy::Fixed, 18.0f } };
    if(!ui.label("title", "Integer and decimal editors", title) || !ui.label("integer_caption", "Signed 64-bit integer", caption))
        return false;
    EditBoxOptions edit;
    edit.width = { LayoutSizePolicy::Fixed, 400.0f };
    edit.height = { LayoutSizePolicy::Fixed, 40.0f };
    edit.enabled = m_enabled;
    edit.readOnly = m_readOnly;
    IntegerEditOptions integer;
    integer.edit = edit;
    const NumericEditBoxResult first = ui.integerEdit("integer", m_integer, m_states[0u], integer);
    if(!first.edit.valid)
        return false;
    count(first.numeric);
    if(!ui.label("float_caption", "Decimal, range -10 to 10", caption))
        return false;
    FloatEditOptions floating;
    floating.edit = edit;
    floating.bounds = floatBounds();
    const NumericEditBoxResult second = ui.floatEdit("float", m_float, m_states[1u], floating);
    if(!second.edit.valid)
        return false;
    count(second.numeric);
    if(!ui.label("clipboard_caption", "Clipboard destination", caption))
        return false;
    edit.enabled = true;
    edit.readOnly = false;
    if(!ui.editBox("clipboard", m_clipboard, m_states[2u], edit).valid)
        return false;
    if(
        !ui.label("policy", m_clamp ? "Range policy: clamp" : "Range policy: reject", caption)
        || !ui.label("hint", "Enter: commit / Escape: restore", caption)
        || !ui.endPanel()
    )
        return false;
    observeState(context.display, context.text);
    paintMarkers(context);
    return true;
}

bool UiNumericEditSmokeScene::keyboardUpdate(const i32 key, const i32 scancode, const i32 action, const i32 mods){
    static_cast<void>(scancode);
    static_cast<void>(mods);
    if(key < Core::Key::F5 || key > Core::Key::F9)
        return false;
    if(action == Core::InputAction::Press){
        if(key == Core::Key::F5)
            m_readOnly = !m_readOnly;
        else if(key == Core::Key::F6)
            m_enabled = !m_enabled;
        else if(key == Core::Key::F7){
            const bool assigned = m_integer.setValue(9007199254740995ll) && m_float.setValue(2.5);
            GLB_FATAL_ASSERT(assigned);
        }
        else if(key == Core::Key::F8)
            m_clamp = !m_clamp;
        else
            resetModels();
    }
    return true;
}

void UiNumericEditSmokeScene::resetModels(){
    const bool reset = m_integer.setValue(7ll) && m_float.setValue(1.25) && m_clipboard.setText("");
    GLB_FATAL_ASSERT(reset);
    m_enabled = true;
    m_readOnly = false;
    m_clamp = true;
}

void UiNumericEditSmokeScene::count(const Impl::Ui::NumericEditResult& result){
    m_commits += result.committed ? 1u : 0u;
    m_cancels += result.cancelled ? 1u : 0u;
    m_rejects += result.rejected ? 1u : 0u;
    m_clamps += result.clamped ? 1u : 0u;
    m_restored += result.restored ? 1u : 0u;
}

Impl::Ui::FloatBounds UiNumericEditSmokeScene::floatBounds()const{
    return { -10.0, 10.0, m_clamp ? Impl::Ui::NumericBoundsPolicy::Clamp : Impl::Ui::NumericBoundsPolicy::Reject };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool IsUiLayerNumericEditSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_NUMERIC_EDIT");
}

bool IsUiLayerNumericEditSkinSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_NUMERIC_EDIT_SKIN");
}

SharedUiNumericEditSmokeScene CreateUiNumericEditSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input){
    return SharedUiNumericEditSmokeScene(
        NewArenaObject<RefCounter<UiNumericEditSmokeScene>>(arena, arena, input),
        ArenaRefDeleter<RefCounter<UiNumericEditSmokeScene>, Core::Alloc::GlobalArena>(&arena),
        AdoptRef
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

