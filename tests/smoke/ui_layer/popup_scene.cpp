// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "popup_scene.h"

#include "smoke_geometry.h"
#include "../smoke_environment.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_popup_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr Impl::Ui::Rect s_Panel{ 24.0f, 24.0f, 320.0f, 216.0f };
static constexpr Impl::Ui::Rect s_Anchor{ 200.0f, 64.0f, 120.0f, 36.0f };
static constexpr Impl::Ui::Rect s_Trigger{ 32.0f, 64.0f, 120.0f, 36.0f };
static constexpr Impl::Ui::Rect s_ModalTrigger{ 160.0f, 64.0f, 120.0f, 36.0f };
static constexpr Impl::Ui::Rect s_Counter{ 32.0f, 112.0f, 130.0f, 36.0f };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiPopupSmokeScene::UiPopupSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input)
    : m_input(input)
    , m_popupText(arena)
    , m_outsideText(arena)
{
    const bool initialized = m_popupText.setText("Popup") && m_outsideText.setText("Outside focus sentinel");
    GLB_FATAL_ASSERT_MSG(initialized, GLB_TEXT("Popup smoke text models must contain valid UTF8"));
    m_input.addHandlerToBack(*this);
}

UiPopupSmokeScene::~UiPopupSmokeScene(){
    m_input.removeHandler(*this);
}

bool UiPopupSmokeScene::paint(Impl::UiPaintContext& context){
    using namespace Impl::Ui;
    observeDisplay(context.display);
    Builder& ui = context.ui;
    ui.style().fontSize = 12.0f;
    ui.style().gap = 8.0f;
    if(!ui.beginPanel("popup_fixture", __hidden_ui_popup_smoke::s_Panel))
        return false;
    const WidgetOptions title{ {}, { LayoutSizePolicy::Fixed, 24.0f } };
    if(!ui.label("title", "Popup focus and overlay fixture", title))
        return false;
    ContainerOptions row;
    row.height = { LayoutSizePolicy::Fixed, 40.0f };
    row.gap = 8.0f;
    if(!ui.beginRow("triggers", row))
        return false;
    const WidgetOptions trigger{ { LayoutSizePolicy::Fixed, 120.0f }, { LayoutSizePolicy::Fixed, 36.0f } };
    if(ui.button("open", "Open popup", trigger)){
        m_modal = false;
        m_popup.open();
    }
    if(ui.button("modal", "Open modal", trigger)){
        m_modal = true;
        m_popup.open();
    }
    if(!ui.endContainer())
        return false;
    const WidgetOptions counter{ { LayoutSizePolicy::Fixed, 130.0f }, { LayoutSizePolicy::Fixed, 36.0f } };
    if(ui.button("underlying", "Underlying", counter))
        ++m_underlying;
    EditBoxOptions edit;
    edit.width = { LayoutSizePolicy::Fixed, 260.0f };
    edit.height = { LayoutSizePolicy::Fixed, 32.0f };
    const EditBoxResult outside = ui.editBox("outside", m_outsideText, m_outsideEdit, edit);
    m_outsideFocused = outside.focused;
    if(!outside.valid || !ui.endPanel())
        return false;

    PopupOptions options;
    options.anchor = __hidden_ui_popup_smoke::s_Anchor;
    options.size = { 220.0f, 236.0f };
    options.modal = m_modal;
    options.dismissOutside = !m_modal;
    if(m_edge)
        options.anchor = { context.display.logicalWidth - 60.0f, context.display.logicalHeight - 48.0f, 48.0f, 24.0f };
    options.side = m_right ? PopupPlacementSide::Right : PopupPlacementSide::Below;
    m_popupFocused = false;
    if(m_visible && ui.beginPopup("popup", m_popup, options)){
        const WidgetOptions caption{ {}, { LayoutSizePolicy::Fixed, 20.0f } };
        const WidgetOptions choice{ { LayoutSizePolicy::Stretch, 1.0f }, { LayoutSizePolicy::Fixed, 32.0f } };
        if(!ui.label("caption", "Choose, edit, or dismiss", caption))
            return false;
        if(ui.button("first", "First choice", choice)){
            m_selected = 1u;
            m_popup.close();
        }
        if(ui.button("second", "Second choice", choice)){
            m_selected = 2u;
            m_popup.close();
        }
        if(ui.checkbox("checked", "Popup checkbox", m_checked, choice))
            NWB_LOGGER_ESSENTIAL_INFO(GLB_TEXT("UiPopupSmoke: checked={}"), static_cast<u32>(m_checked));
        edit.width = {};
        const EditBoxResult popupEdit = ui.editBox("text", m_popupText, m_popupEdit, edit);
        m_popupFocused = popupEdit.focused;
        if(!popupEdit.valid || !ui.endPopup())
            return false;
    }
    observeState();
    paintMarkers(context);
    return true;
}

bool UiPopupSmokeScene::paintLaterRoot(Impl::UiPaintContext& context)const{
    using namespace Impl::Ui;
    Rect bounds = m_popup.placement().bounds;
    if(bounds.width <= 0.0f)
        bounds = { 200.0f, 104.0f, 220.0f, 236.0f };
    if(!context.ui.beginPanel("later_root", { bounds.x - 8.0f, bounds.y - 8.0f, bounds.width + 16.0f, bounds.height + 16.0f }))
        return false;
    if(!context.ui.endPanel())
        return false;
    context.paint.fillRect(bounds, { 0.72f, 0.02f, 0.04f, 1.0f });
    context.paint.fillRect({ context.display.logicalWidth - 40.0f, 24.0f, 16.0f, 16.0f }, { 0.12f, 0.52f, 0.86f, 1.0f });
    return true;
}

bool UiPopupSmokeScene::keyboardUpdate(const i32 key, const i32 scancode, const i32 action, const i32 mods){
    static_cast<void>(scancode);
    static_cast<void>(mods);
    if(key != Core::Key::F5 && key != Core::Key::F6 && key != Core::Key::F7)
        return false;
    if(action == Core::InputAction::Press){
        if(key == Core::Key::F5)
            m_edge = !m_edge;
        else if(key == Core::Key::F6)
            m_right = !m_right;
        else{
            m_visible = !m_visible;
            if(m_visible)
                m_popup.close();
        }
    }
    return true;
}

void UiPopupSmokeScene::observeDisplay(const Impl::Ui::DisplayMetrics& display){
    if(
        m_lastDisplay.logicalWidth == display.logicalWidth && m_lastDisplay.logicalHeight == display.logicalHeight
        && m_lastDisplay.pixelScaleX == display.pixelScaleX && m_lastDisplay.pixelScaleY == display.pixelScaleY
    )
        return;
    m_lastDisplay = display;
    m_displayChanged = true;
    NWB_LOGGER_ESSENTIAL_INFO(GLB_TEXT("UiPopupSmoke: display logical={}x{} scale={}x{}")
        , display.logicalWidth, display.logicalHeight, display.pixelScaleX, display.pixelScaleY
    );
}

Array<u32, 12u> UiPopupSmokeScene::values()const{
    return { static_cast<u32>(m_visible && m_popup.isOpen()), static_cast<u32>(m_modal), m_underlying, m_selected,
        static_cast<u32>(m_checked), static_cast<u32>(m_popupFocused), static_cast<u32>(m_outsideFocused),
        static_cast<u32>(m_edge), static_cast<u32>(m_right), static_cast<u32>(m_visible),
        static_cast<u32>(m_popupText.text().size()), static_cast<u32>(m_popup.placement().side) };
}

void UiPopupSmokeScene::observeState(){
    const auto current = values();
    const auto& placement = m_popup.placement();
    if(
        m_sequence != 0u && !m_displayChanged && current == m_lastValues
        && SameSmokeRect(placement.bounds, m_lastPlacement.bounds)
        && SameSmokeRect(placement.viewport, m_lastPlacement.viewport)
    )
        return;
    ++m_sequence;
    m_lastValues = current;
    m_lastPlacement = placement;
    m_displayChanged = false;
    NWB_LOGGER_ESSENTIAL_INFO(GLB_TEXT("UiPopupSmoke: state sequence={} values={},{},{},{},{},{},{},{},{},{},{},{}")
        , m_sequence, current[0], current[1], current[2], current[3], current[4], current[5]
        , current[6], current[7], current[8], current[9], current[10], current[11]
    );
    LogSmokeRect(GLB_TEXT("UiPopupSmoke"), m_sequence, GLB_TEXT("popup"), placement.bounds);
    LogSmokeRect(GLB_TEXT("UiPopupSmoke"), m_sequence, GLB_TEXT("viewport"), placement.viewport);
    LogSmokeRect(GLB_TEXT("UiPopupSmoke"), m_sequence, GLB_TEXT("trigger"), __hidden_ui_popup_smoke::s_Trigger);
    LogSmokeRect(GLB_TEXT("UiPopupSmoke"), m_sequence, GLB_TEXT("modal"), __hidden_ui_popup_smoke::s_ModalTrigger);
    LogSmokeRect(GLB_TEXT("UiPopupSmoke"), m_sequence, GLB_TEXT("counter"), __hidden_ui_popup_smoke::s_Counter);
    LogSmokeRect(GLB_TEXT("UiPopupSmoke"), m_sequence, GLB_TEXT("outside"), m_outsideEdit.placement.bounds);
    const Impl::Ui::Rect first{ placement.bounds.x + 8.0f, placement.bounds.y + 36.0f,
        placement.bounds.width - 16.0f, 32.0f };
    LogSmokeRect(GLB_TEXT("UiPopupSmoke"), m_sequence, GLB_TEXT("first"), first);
    LogSmokeRect(GLB_TEXT("UiPopupSmoke"), m_sequence, GLB_TEXT("second"), { first.x, first.y + 40.0f, first.width, first.height });
    LogSmokeRect(GLB_TEXT("UiPopupSmoke"), m_sequence, GLB_TEXT("check"), { first.x, first.y + 80.0f, first.width, first.height });
    LogSmokeRect(GLB_TEXT("UiPopupSmoke"), m_sequence, GLB_TEXT("edit"), m_popupEdit.placement.bounds);
}

void UiPopupSmokeScene::paintMarkers(Impl::UiPaintContext& context)const{
    const auto current = values();
    const f32 y = context.display.logicalHeight - 20.0f;
    for(usize index = 0u; index <= current.size(); ++index){
        const u32 value = index == current.size() ? m_sequence : current[index];
        context.paint.fillRect({ 12.0f + static_cast<f32>(index) * 20.0f, y, 14.0f, 12.0f },
            EncodeSmokeColor(value));
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool IsUiLayerPopupSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_POPUP");
}

bool IsUiLayerPopupSkinSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_POPUP_SKIN");
}

SharedUiPopupSmokeScene CreateUiPopupSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input){
    return SharedUiPopupSmokeScene(
        NewArenaObject<RefCounter<UiPopupSmokeScene>>(arena, arena, input),
        ArenaRefDeleter<RefCounter<UiPopupSmokeScene>, Core::Alloc::GlobalArena>(&arena),
        s_AdoptRef
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

