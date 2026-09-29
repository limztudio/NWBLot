// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "slider_scene.h"

#include "../smoke_environment.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiSliderSmokeScene::UiSliderSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input)
    : m_input(input)
{
    static_cast<void>(arena);
    static_cast<void>(m_state.setValue(0.25));
    static_cast<void>(m_disabled.setValue(0.75));
    static_cast<void>(m_constant.setValue(0.5));
    static_cast<void>(m_popupSlider.setValue(0.5));
    m_input.addHandlerToBack(*this);
}

UiSliderSmokeScene::~UiSliderSmokeScene(){
    m_input.removeHandler(*this);
}

bool UiSliderSmokeScene::paint(Impl::UiPaintContext& context){
    auto& ui = context.ui;
    ui.style().fontSize = 14.0f;
    ui.style().gap = 8.0f;
    const f32 leftWidth = Max(280.0f, (context.display.logicalWidth - 96.0f) * 0.5f);
    const f32 right = 24.0f + leftWidth + 32.0f;
    const f32 rightWidth = Max(240.0f, context.display.logicalWidth - right - 24.0f);
    if(
        !paintMain(ui, { 24.0f, 24.0f, leftWidth, 220.0f })
        || !paintOther(ui, { right, 24.0f, rightWidth, 180.0f }) || !paintPopup(ui, right) || ui.failed()
    )
        return false;
    observeState(context);
    paintMarkers(context);
    return true;
}

bool UiSliderSmokeScene::keyboardUpdate(const i32 key, const i32 scancode, const i32 action, const i32 mods){
    static_cast<void>(scancode);
    static_cast<void>(mods);
    if(key < Core::Key::F4 || key > Core::Key::F9)
        return false;
    if(action != Core::InputAction::Press)
        return true;
    switch(key){
    case Core::Key::F4:
        if(m_state.setValue(m_state.value()))
            ++m_externalIntents;
        break;
    case Core::Key::F5:
        m_narrowRange = !m_narrowRange;
        break;
    case Core::Key::F6:
        m_coarseStep = !m_coarseStep;
        break;
    case Core::Key::F7:
        m_enabled = !m_enabled;
        break;
    case Core::Key::F8:
        if(m_state.setValue(0.25))
            ++m_externalIntents;
        break;
    case Core::Key::F9:
        m_parent.close();
        break;
    default:
        break;
    }
    return true;
}

bool UiSliderSmokeScene::paintMain(Impl::Ui::Builder& ui, const Impl::Ui::Rect& bounds){
    using namespace Impl::Ui;
    if(!ui.beginPanel("slider_main", bounds))
        return false;
    const WidgetOptions caption{ {}, { LayoutSizePolicy::Fixed, 16.0f } };
    const WidgetOptions button{ { LayoutSizePolicy::Stretch, 1.0f }, { LayoutSizePolicy::Fixed, 28.0f } };
    if(!ui.label("title", "Continuous slider / exact double value", caption))
        return false;
    if(ui.button("before", "Before slider", button))
        ++m_beforeClicks;
    SliderOptions settings;
    settings.maximum = m_narrowRange ? 0.5 : 1.0;
    settings.keyStep = m_coarseStep ? 0.25 : 0.125;
    settings.enabled = m_enabled;
    if(!ui.slider("value", m_state, settings))
        return false;
    if(ui.button("after", "After slider", button))
        ++m_afterClicks;
    if(ui.button("open", "Open parent popup", button))
        m_parent.open();
    if(!ui.endPanel())
        return false;
    m_changes += static_cast<u32>(m_state.result().valueChanged);
    return m_state.result().valid;
}

bool UiSliderSmokeScene::paintOther(Impl::Ui::Builder& ui, const Impl::Ui::Rect& bounds){
    using namespace Impl::Ui;
    if(!ui.beginPanel("slider_other", bounds))
        return false;
    const WidgetOptions caption{ {}, { LayoutSizePolicy::Fixed, 16.0f } };
    if(!ui.label("disabled_title", "Disabled slider keeps its value", caption))
        return false;
    SliderOptions disabled;
    disabled.enabled = false;
    if(!ui.slider("disabled", m_disabled, disabled))
        return false;
    if(!ui.label("constant_title", "Constant range has no interaction", caption))
        return false;
    SliderOptions constant;
    constant.minimum = 0.5;
    constant.maximum = 0.5;
    if(!ui.slider("constant", m_constant, constant) || !ui.endPanel())
        return false;
    return m_disabled.result().valid && m_constant.result().valid;
}

bool UiSliderSmokeScene::paintPopup(Impl::Ui::Builder& ui, const f32 right){
    using namespace Impl::Ui;
    PopupOptions popup;
    popup.anchor = { right, 228.0f, 8.0f, 24.0f };
    popup.side = PopupPlacementSide::Below;
    popup.size = { 300.0f, 144.0f };
    if(!ui.beginPopup("slider_parent", m_parent, popup))
        return !ui.failed();
    const WidgetOptions caption{ {}, { LayoutSizePolicy::Fixed, 16.0f } };
    if(!ui.label("title", "Slider inside parent popup", caption))
        return false;
    SliderOptions settings;
    settings.keyStep = 0.125;
    if(!ui.slider("value", m_popupSlider, settings))
        return false;
    const WidgetOptions button{ { LayoutSizePolicy::Stretch, 1.0f }, { LayoutSizePolicy::Fixed, 28.0f } };
    if(ui.button("close", "Close parent", button))
        m_parent.close();
    if(!ui.endPopup())
        return false;
    if(m_parent.isOpen())
        m_popupChanges += static_cast<u32>(m_popupSlider.result().valueChanged);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool IsUiLayerSliderSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_SLIDER");
}

bool IsUiLayerSliderSkinSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_SLIDER_SKIN");
}

SharedUiSliderSmokeScene CreateUiSliderSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input){
    return SharedUiSliderSmokeScene(
        NewArenaObject<RefCounter<UiSliderSmokeScene>>(arena, arena, input),
        ArenaRefDeleter<RefCounter<UiSliderSmokeScene>, Core::Alloc::GlobalArena>(&arena),
        AdoptRef
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

