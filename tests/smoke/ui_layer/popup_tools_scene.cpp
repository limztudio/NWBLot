// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "popup_tools_scene.h"

#include "smoke_geometry.h"
#include "../smoke_environment.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_popup_tools_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr Impl::Ui::Rect s_Panel{ 24.0f, 24.0f, 400.0f, 200.0f };
static constexpr Impl::Ui::Rect s_Anchor{ 32.0f, 64.0f, 220.0f, 36.0f };
static constexpr Impl::Ui::Rect s_Counter{ 456.0f, 32.0f, 130.0f, 36.0f };
static constexpr f32 s_RowHeight = 28.0f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiPopupToolsSmokeScene::UiPopupToolsSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input)
    : m_input(input)
    , m_sentinel(arena)
{
    const bool initialized = m_sentinel.setText("Sentinel");
    GLB_FATAL_ASSERT(initialized);
    m_input.addHandlerToBack(*this);
}

UiPopupToolsSmokeScene::~UiPopupToolsSmokeScene(){
    m_input.removeHandler(*this);
}

bool UiPopupToolsSmokeScene::paint(Impl::UiPaintContext& context){
    using namespace Impl::Ui;
    observeDisplay(context.display);
    m_source.beginFrame();
    m_sentinelFocused = false;
    Builder& ui = context.ui;
    ui.style().fontSize = 12.0f;
    ui.style().gap = 8.0f;
    if(!ui.beginPanel("popup_tools_fixture", __hidden_ui_popup_tools_smoke::s_Panel))
        return false;
    const WidgetOptions caption{ {}, { LayoutSizePolicy::Fixed, 24.0f } };
    if(!ui.label("title", "Delayed tooltip and five context commands", caption))
        return false;
    WidgetOptions anchor{ { LayoutSizePolicy::Fixed, 220.0f }, { LayoutSizePolicy::Fixed, 36.0f } };
    anchor.enabled = m_enabled;
    if(ui.button("anchor", "Right-click / Menu / Shift+F10", anchor))
        ++m_anchorClicks;
    TooltipOptions tooltip;
    tooltip.delaySeconds = 1.0f;
    if(!ui.tooltip("hint", "anchor", "Open commands here; Delete is disabled.", m_tooltip, tooltip))
        return false;
    ContextMenuOptions menu;
    menu.size = { 220.0f, 180.0f };
    menu.rowHeight = __hidden_ui_popup_tools_smoke::s_RowHeight;
    const ContextMenuResult result = ui.contextMenu("commands", "anchor", m_source, m_menu, menu);
    if(!result.valid)
        return false;
    if(result.activated){
        m_command = result.key;
        ++m_commits;
    }
    if(!ui.label("keys", "F5 reorder / F6 replace source / F9 disabled", caption))
        return false;
    EditBoxOptions edit;
    edit.width = { LayoutSizePolicy::Fixed, 260.0f };
    edit.height = { LayoutSizePolicy::Fixed, 36.0f };
    const EditBoxResult sentinel = ui.editBox("sentinel", m_sentinel, m_sentinelState, edit);
    m_sentinelFocused = sentinel.focused;
    if(!sentinel.valid || !ui.endPanel())
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

bool UiPopupToolsSmokeScene::keyboardUpdate(const i32 key, const i32 scancode, const i32 action, const i32 mods){
    static_cast<void>(scancode);
    static_cast<void>(mods);
    if(key != Core::Key::F5 && key != Core::Key::F6 && key != Core::Key::F9)
        return false;
    if(action == Core::InputAction::Press){
        if(key == Core::Key::F5)
            m_source.reverse();
        else if(key == Core::Key::F6)
            m_source.replace();
        else
            m_enabled = !m_enabled;
    }
    return true;
}

void UiPopupToolsSmokeScene::observeDisplay(const Impl::Ui::DisplayMetrics& display){
    if(
        m_lastDisplay.logicalWidth == display.logicalWidth && m_lastDisplay.logicalHeight == display.logicalHeight
        && m_lastDisplay.pixelScaleX == display.pixelScaleX && m_lastDisplay.pixelScaleY == display.pixelScaleY
    )
        return;
    m_lastDisplay = display;
    m_displayChanged = true;
    NWB_LOGGER_ESSENTIAL_INFO(GLB_TEXT("UiPopupToolsSmoke: display logical={}x{} scale={}x{}")
        , display.logicalWidth, display.logicalHeight, display.pixelScaleX, display.pixelScaleY
    );
}

Array<u64, 15u> UiPopupToolsSmokeScene::values()const{
    return { static_cast<u64>(m_tooltip.visible()), static_cast<u64>(m_menu.isOpen()), m_menu.cursorKey(), m_command,
        m_commits, m_anchorClicks, m_underlying, static_cast<u64>(m_sentinelFocused), static_cast<u64>(m_enabled),
        static_cast<u64>(m_source.reversed()), m_source.revision(), m_source.instanceGeneration(), m_source.labelReads(),
        static_cast<u64>(m_sentinel.text().size()), m_menu.listState().selectedKey() };
}

Impl::Ui::Rect UiPopupToolsSmokeScene::rowBounds(const u64 key)const{
    Impl::Ui::Rect rectangle;
    u64 index = 0u;
    const auto& placement = m_menu.listState().placement();
    if(
        !m_menu.isOpen() || !m_source.indexOf(key, index) || index < placement.firstRow || index >= placement.endRow
        || !Impl::Ui::ScrollLayout::rowBounds(index, placement, __hidden_ui_popup_tools_smoke::s_RowHeight, rectangle)
    )
        return {};
    return rectangle;
}

void UiPopupToolsSmokeScene::observeState(){
    const auto current = values();
    const auto& menu = m_menu.placement();
    const auto& tooltip = m_tooltip.placement();
    const auto& sentinel = m_sentinelState.placement.bounds;
    if(
        m_sequence != 0u && !m_displayChanged && current == m_lastValues
        && SameSmokeRect(menu.bounds, m_lastMenu.bounds)
        && SameSmokeRect(tooltip.bounds, m_lastTooltip.bounds)
        && SameSmokeRect(sentinel, m_lastSentinel)
    )
        return;
    ++m_sequence;
    m_lastValues = current;
    m_lastMenu = menu;
    m_lastTooltip = tooltip;
    m_lastSentinel = sentinel;
    m_displayChanged = false;
    NWB_LOGGER_ESSENTIAL_INFO(GLB_TEXT("UiPopupToolsSmoke: state sequence={} values={},{},{},{},{},{},{},{},{},{},{},{},{},{},{}")
        , m_sequence, current[0], current[1], current[2], current[3], current[4], current[5], current[6], current[7]
        , current[8], current[9], current[10], current[11], current[12], current[13], current[14]
    );
    LogSmokeRect(GLB_TEXT("UiPopupToolsSmoke"), m_sequence, GLB_TEXT("anchor"), __hidden_ui_popup_tools_smoke::s_Anchor);
    LogSmokeRect(GLB_TEXT("UiPopupToolsSmoke"), m_sequence, GLB_TEXT("menu"), menu.bounds);
    LogSmokeRect(GLB_TEXT("UiPopupToolsSmoke"), m_sequence, GLB_TEXT("tooltip"), tooltip.bounds);
    const auto& list = m_menu.listState().placement();
    LogSmokeRect(GLB_TEXT("UiPopupToolsSmoke"), m_sequence, GLB_TEXT("list"), list.bounds);
    LogSmokeRect(GLB_TEXT("UiPopupToolsSmoke"), m_sequence, GLB_TEXT("viewport"), list.viewport);
    LogSmokeRect(GLB_TEXT("UiPopupToolsSmoke"), m_sequence, GLB_TEXT("first"), rowBounds(1u));
    LogSmokeRect(GLB_TEXT("UiPopupToolsSmoke"), m_sequence, GLB_TEXT("second"), rowBounds(2u));
    LogSmokeRect(GLB_TEXT("UiPopupToolsSmoke"), m_sequence, GLB_TEXT("disabled"), rowBounds(3u));
    LogSmokeRect(GLB_TEXT("UiPopupToolsSmoke"), m_sequence, GLB_TEXT("last"), rowBounds(5u));
    LogSmokeRect(GLB_TEXT("UiPopupToolsSmoke"), m_sequence, GLB_TEXT("cursor_row"), rowBounds(m_menu.cursorKey()));
    LogSmokeRect(GLB_TEXT("UiPopupToolsSmoke"), m_sequence, GLB_TEXT("sentinel"), sentinel);
    LogSmokeRect(GLB_TEXT("UiPopupToolsSmoke"), m_sequence, GLB_TEXT("counter"), __hidden_ui_popup_tools_smoke::s_Counter);
}

void UiPopupToolsSmokeScene::paintMarkers(Impl::UiPaintContext& context)const{
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


bool IsUiLayerPopupToolsSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_POPUP_TOOLS");
}

bool IsUiLayerPopupToolsSkinSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_POPUP_TOOLS_SKIN");
}

SharedUiPopupToolsSmokeScene CreateUiPopupToolsSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input){
    return SharedUiPopupToolsSmokeScene(
        NewArenaObject<RefCounter<UiPopupToolsSmokeScene>>(arena, arena, input),
        ArenaRefDeleter<RefCounter<UiPopupToolsSmokeScene>, Core::Alloc::GlobalArena>(&arena),
        s_AdoptRef
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

