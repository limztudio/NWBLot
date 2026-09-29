// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "radio_group_scene.h"

#include "../smoke_environment.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiRadioGroupSmokeScene::UiRadioGroupSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input)
    : m_input(input)
{
    static_cast<void>(arena);
    m_state.select(10u);
    m_disabled.select(30u);
    m_popupRadio.select(10u);
    m_input.addHandlerToBack(*this);
}

UiRadioGroupSmokeScene::~UiRadioGroupSmokeScene(){
    m_input.removeHandler(*this);
}

bool UiRadioGroupSmokeScene::paint(Impl::UiPaintContext& context){
    m_source.beginFrame();
    auto& ui = context.ui;
    ui.style().fontSize = 14.0f;
    ui.style().gap = 4.0f;
    if(!paintMain(ui) || !paintDisabled(ui) || !paintPopup(ui) || ui.failed())
        return false;
    observeState(context);
    paintMarkers(context);
    return true;
}

bool UiRadioGroupSmokeScene::keyboardUpdate(const i32 key, const i32 scancode, const i32 action, const i32 mods){
    static_cast<void>(scancode);
    static_cast<void>(mods);
    if(key < Core::Key::F4 || key > Core::Key::F9)
        return false;
    if(action != Core::InputAction::Press)
        return true;
    switch(key){
    case Core::Key::F4:
        m_source.reverse();
        break;
    case Core::Key::F5:
        m_source.remove(m_state.selectedKey());
        break;
    case Core::Key::F6:
        m_source.replace();
        break;
    case Core::Key::F7:
        m_state.select(30u);
        break;
    case Core::Key::F8:
        m_enabled = !m_enabled;
        break;
    case Core::Key::F9:
        m_parent.close();
        break;
    default:
        break;
    }
    return true;
}

bool UiRadioGroupSmokeScene::paintMain(Impl::Ui::Builder& ui){
    using namespace Impl::Ui;
    if(!ui.beginPanel("radio_main", { 24.0f, 24.0f, 336.0f, 356.0f }))
        return false;
    const WidgetOptions caption{ {}, { LayoutSizePolicy::Fixed, 16.0f } };
    const WidgetOptions button{ { LayoutSizePolicy::Stretch, 1.0f }, { LayoutSizePolicy::Fixed, 28.0f } };
    if(!ui.label("title", "Radio group / stable choice keys", caption))
        return false;
    if(ui.button("before", "Before group", button))
        ++m_beforeClicks;
    RadioGroupOptions options;
    options.enabled = m_enabled;
    const RadioGroupResult result = ui.radioGroup("choices", m_source, m_state, options);
    if(!result.valid)
        return false;
    m_changes += static_cast<u32>(result.selectionChanged);
    m_activations += static_cast<u32>(result.activated);
    if(ui.button("after", "After group", button))
        ++m_afterClicks;
    if(ui.button("open", "Open parent popup", button))
        m_parent.open();
    return ui.endPanel();
}

bool UiRadioGroupSmokeScene::paintDisabled(Impl::Ui::Builder& ui){
    using namespace Impl::Ui;
    if(!ui.beginPanel("radio_disabled", { 392.0f, 24.0f, 300.0f, 240.0f }))
        return false;
    const WidgetOptions caption{ {}, { LayoutSizePolicy::Fixed, 16.0f } };
    if(!ui.label("title", "Disabled group keeps checked choice", caption))
        return false;
    RadioGroupOptions options;
    options.enabled = false;
    const RadioGroupResult result = ui.radioGroup("choices", m_source, m_disabled, options);
    return result.valid && ui.endPanel();
}

bool UiRadioGroupSmokeScene::paintPopup(Impl::Ui::Builder& ui){
    using namespace Impl::Ui;
    PopupOptions options;
    options.anchor = { 360.0f, 52.0f, 8.0f, 28.0f };
    options.side = PopupPlacementSide::Right;
    options.size = { 300.0f, 272.0f };
    if(!ui.beginPopup("radio_parent", m_parent, options))
        return !ui.failed();
    const WidgetOptions caption{ {}, { LayoutSizePolicy::Fixed, 16.0f } };
    if(!ui.label("title", "Radio inside a parent popup", caption))
        return false;
    const RadioGroupResult result = ui.radioGroup("choices", m_source, m_popupRadio);
    if(!result.valid)
        return false;
    m_popupChanges += static_cast<u32>(result.selectionChanged);
    m_popupActivations += static_cast<u32>(result.activated);
    const WidgetOptions button{ { LayoutSizePolicy::Stretch, 1.0f }, { LayoutSizePolicy::Fixed, 28.0f } };
    if(ui.button("close", "Close parent", button))
        m_parent.close();
    return ui.endPopup();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool IsUiLayerRadioGroupSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_RADIO_GROUP");
}

bool IsUiLayerRadioGroupSkinSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_RADIO_GROUP_SKIN");
}

SharedUiRadioGroupSmokeScene CreateUiRadioGroupSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input){
    return SharedUiRadioGroupSmokeScene(
        NewArenaObject<RefCounter<UiRadioGroupSmokeScene>>(arena, arena, input),
        ArenaRefDeleter<RefCounter<UiRadioGroupSmokeScene>, Core::Alloc::GlobalArena>(&arena),
        AdoptRef
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

