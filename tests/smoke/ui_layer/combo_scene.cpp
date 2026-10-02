// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "combo_scene.h"

#include "smoke_geometry.h"
#include "../smoke_environment.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_combo_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr Impl::Ui::Rect s_Panel{ 24.0f, 24.0f, 400.0f, 160.0f };
static constexpr Impl::Ui::Rect s_Counter{ 456.0f, 32.0f, 130.0f, 36.0f };
static constexpr f32 s_RowHeight = 24.0f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiComboSmokeScene::UiComboSmokeScene(Core::InputDispatcher& input)
    : m_input(input)
{
    m_input.addHandlerToBack(*this);
}

UiComboSmokeScene::~UiComboSmokeScene(){
    m_input.removeHandler(*this);
}

bool UiComboSmokeScene::paint(Impl::UiPaintContext& context){
    using namespace Impl::Ui;
    observeDisplay(context.display);
    m_source.beginFrame();
    m_focused = false;
    Builder& ui = context.ui;
    ui.style().fontSize = 12.0f;
    ui.style().gap = 8.0f;
    ui.listStyle().padding = { 8.0f, 8.0f, 8.0f, 8.0f };
    Rect panel = __hidden_ui_combo_smoke::s_Panel;
    if(m_bottom)
        panel.y = context.display.logicalHeight - 192.0f;
    if(!ui.beginPanel("combo_fixture", panel))
        return false;
    const WidgetOptions title{ {}, { LayoutSizePolicy::Fixed, 24.0f } };
    if(!ui.label("title", "Combo: 100000 keyed rows, row5 disabled", title))
        return false;
    if(m_visible){
        ComboOptions options;
        options.height = { LayoutSizePolicy::Fixed, 36.0f };
        options.popupHeight = 240.0f;
        options.rowHeight = __hidden_ui_combo_smoke::s_RowHeight;
        options.enabled = m_enabled;
        const ComboResult result = ui.comboBox("choice", m_source, m_state, options);
        if(!result.valid)
            return false;
        m_focused = result.focused;
        if(result.committed)
            ++m_commits;
    }
    else{
        const WidgetOptions spacer{ {}, { LayoutSizePolicy::Fixed, 36.0f } };
        if(!ui.label("omitted_choice", "Combo omitted", spacer))
            return false;
    }
    if(!ui.label("hint", "Preview arrows / Enter commit / Escape cancel", title) || !ui.endPanel())
        return false;
    if(!ui.beginPanel("outside_fixture", { 448.0f, 24.0f, 146.0f, 52.0f }))
        return false;
    const WidgetOptions counter{ { LayoutSizePolicy::Fixed, 130.0f }, { LayoutSizePolicy::Fixed, 36.0f } };
    if(ui.button("underlying", "Outside counter", counter))
        ++m_underlying;
    if(!ui.endPanel())
        return false;
    observeState();
    paintMarkers(context);
    return true;
}

bool UiComboSmokeScene::keyboardUpdate(const i32 key, const i32 scancode, const i32 action, const i32 mods){
    static_cast<void>(scancode);
    static_cast<void>(mods);
    if(key < Core::Key::F4 || key > Core::Key::F12)
        return false;
    if(action != Core::InputAction::Press)
        return true;
    switch(key){
    case Core::Key::F4:
        m_state.select(100000u);
        break;
    case Core::Key::F5:
        m_source.reverse();
        break;
    case Core::Key::F6:
        m_source.remove(m_state.selectedKey());
        break;
    case Core::Key::F7:
        m_visible = !m_visible;
        break;
    case Core::Key::F8:
        m_source.toggleEmpty();
        break;
    case Core::Key::F9:
        m_enabled = !m_enabled;
        break;
    case Core::Key::F10:
        m_bottom = !m_bottom;
        break;
    case Core::Key::F11:
        m_state.select(2u);
        break;
    case Core::Key::F12:
        m_source.replace();
        break;
    default:
        break;
    }
    return true;
}

void UiComboSmokeScene::observeDisplay(const Impl::Ui::DisplayMetrics& display){
    if(
        m_lastDisplay.logicalWidth == display.logicalWidth && m_lastDisplay.logicalHeight == display.logicalHeight
        && m_lastDisplay.pixelScaleX == display.pixelScaleX && m_lastDisplay.pixelScaleY == display.pixelScaleY
    )
        return;
    m_lastDisplay = display;
    m_displayChanged = true;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiComboSmoke: display logical={}x{} scale={}x{}")
        , display.logicalWidth, display.logicalHeight, display.pixelScaleX, display.pixelScaleY
    );
}

Array<u64, 16u> UiComboSmokeScene::values()const{
    const auto& list = m_state.listState();
    const auto& placement = list.placement();
    return { m_state.selectedKey(), list.cursorKey(), m_source.rowCount(), placement.firstRow, placement.endRow,
        m_source.labelReads(), static_cast<u64>(m_visible && m_state.isOpen()), static_cast<u64>(m_focused),
        static_cast<u64>(m_source.reversed()), m_source.removedKey(), static_cast<u64>(m_visible), m_commits,
        m_underlying, static_cast<u64>(m_enabled), static_cast<u64>(m_bottom), m_source.instanceGeneration() };
}

Impl::Ui::Rect UiComboSmokeScene::cursorRow()const{
    Impl::Ui::Rect rectangle;
    u64 index = 0u;
    const auto& list = m_state.listState();
    const auto& placement = list.placement();
    if(
        !m_visible || !m_state.isOpen() || !m_source.indexOf(list.cursorKey(), index)
        || index < placement.firstRow || index >= placement.endRow
        || !Impl::Ui::ScrollLayout::RowBounds(index, placement, __hidden_ui_combo_smoke::s_RowHeight, rectangle)
    )
        return {};
    return rectangle;
}

void UiComboSmokeScene::observeState(){
    const auto current = values();
    const auto& placement = m_state.listState().placement();
    const auto& popup = m_state.placement();
    const auto& bounds = m_state.bounds();
    if(
        m_sequence != 0u && !m_displayChanged && current == m_lastValues
        && placement.offset == m_lastPlacement.offset
        && SameSmokeRect(placement.bounds, m_lastPlacement.bounds)
        && SameSmokeRect(placement.thumb, m_lastPlacement.thumb)
        && SameSmokeRect(popup.bounds, m_lastPopup.bounds)
        && SameSmokeRect(bounds, m_lastBounds)
    )
        return;
    ++m_sequence;
    m_lastValues = current;
    m_lastPlacement = placement;
    m_lastPopup = popup;
    m_lastBounds = bounds;
    m_displayChanged = false;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiComboSmoke: state sequence={} values={},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}")
        , m_sequence, current[0], current[1], current[2], current[3], current[4], current[5], current[6], current[7]
        , current[8], current[9], current[10], current[11], current[12], current[13], current[14], current[15]
    );
    LogSmokeRect(NWB_TEXT("UiComboSmoke"), m_sequence, NWB_TEXT("trigger"), bounds);
    LogSmokeRect(NWB_TEXT("UiComboSmoke"), m_sequence, NWB_TEXT("popup"), popup.bounds);
    LogSmokeRect(NWB_TEXT("UiComboSmoke"), m_sequence, NWB_TEXT("list"), placement.bounds);
    LogSmokeRect(NWB_TEXT("UiComboSmoke"), m_sequence, NWB_TEXT("viewport"), placement.viewport);
    LogSmokeRect(NWB_TEXT("UiComboSmoke"), m_sequence, NWB_TEXT("track"), placement.track);
    LogSmokeRect(NWB_TEXT("UiComboSmoke"), m_sequence, NWB_TEXT("thumb"), placement.thumb);
    LogSmokeRect(NWB_TEXT("UiComboSmoke"), m_sequence, NWB_TEXT("cursor_row"), cursorRow());
    LogSmokeRect(NWB_TEXT("UiComboSmoke"), m_sequence, NWB_TEXT("counter"), __hidden_ui_combo_smoke::s_Counter);
}

void UiComboSmokeScene::paintMarkers(Impl::UiPaintContext& context)const{
    const auto current = values();
    const f32 y = context.display.logicalHeight - 20.0f;
    for(usize index = 0u; index <= current.size(); ++index){
        const u64 value = index == current.size() ? m_sequence : current[index];
        for(u32 part = 0u; part < 2u; ++part){
            const usize marker = index * 2u + part;
            context.paint.fillRect({ 12.0f + static_cast<f32>(marker) * 16.0f, y, 12.0f, 12.0f },
                EncodeSmokeColor(value >> (part * 12u)));
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool IsUiLayerComboSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_COMBO");
}

bool IsUiLayerComboSkinSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_COMBO_SKIN");
}

SharedUiComboSmokeScene CreateUiComboSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input){
    return SharedUiComboSmokeScene(
        NewArenaObject<RefCounter<UiComboSmokeScene>>(arena, input),
        ArenaRefDeleter<RefCounter<UiComboSmokeScene>, Core::Alloc::GlobalArena>(&arena),
        AdoptRef
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

