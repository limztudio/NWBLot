// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "list_scene.h"

#include "smoke_geometry.h"
#include "../smoke_environment.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_list_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr Impl::Ui::Rect s_Panel{ 24.0f, 24.0f, 400.0f, 340.0f };
static constexpr Impl::Ui::Rect s_Counter{ 32.0f, 312.0f, 140.0f, 36.0f };
static constexpr f32 s_RowHeight = 24.0f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiListSmokeScene::UiListSmokeScene(Core::InputDispatcher& input)
    : m_input(input)
{
    m_input.addHandlerToBack(*this);
}

UiListSmokeScene::~UiListSmokeScene(){
    m_input.removeHandler(*this);
}

bool UiListSmokeScene::paint(Impl::UiPaintContext& context){
    using namespace Impl::Ui;
    observeDisplay(context.display);
    m_source.beginFrame();
    m_focused = false;
    Builder& ui = context.ui;
    ui.style().fontSize = 12.0f;
    ui.style().gap = 8.0f;
    ui.listStyle().padding = { 8.0f, 8.0f, 8.0f, 8.0f };
    if(!ui.beginPanel("list_fixture", __hidden_ui_list_smoke::s_Panel))
        return false;
    const WidgetOptions title{ {}, { LayoutSizePolicy::Fixed, 24.0f } };
    if(!ui.label("title", "100000 keyed rows: row5 disabled", title))
        return false;
    if(m_visible){
        ListOptions options;
        options.height = { LayoutSizePolicy::Fixed, 240.0f };
        options.rowHeight = __hidden_ui_list_smoke::s_RowHeight;
        const ListResult result = ui.virtualList("rows", m_source, m_state, options);
        if(!result.valid)
            return false;
        m_focused = result.focused;
        if(result.activated)
            ++m_commits;
    }
    else{
        const WidgetOptions spacer{ {}, { LayoutSizePolicy::Fixed, 240.0f } };
        if(!ui.label("omitted_rows", "Rows omitted", spacer))
            return false;
    }
    const WidgetOptions counter{ { LayoutSizePolicy::Fixed, 140.0f }, { LayoutSizePolicy::Fixed, 36.0f } };
    if(ui.button("underlying", "Outside counter", counter))
        ++m_underlying;
    if(!ui.endPanel())
        return false;
    observeState();
    paintMarkers(context);
    return true;
}

bool UiListSmokeScene::keyboardUpdate(const i32 key, const i32 scancode, const i32 action, const i32 mods){
    static_cast<void>(scancode);
    static_cast<void>(mods);
    if(key != Core::Key::F5 && key != Core::Key::F6 && key != Core::Key::F7)
        return false;
    if(action == Core::InputAction::Press){
        if(key == Core::Key::F5)
            m_source.reverse();
        else if(key == Core::Key::F6)
            m_source.remove(m_state.selectedKey());
        else
            m_visible = !m_visible;
    }
    return true;
}

void UiListSmokeScene::observeDisplay(const Impl::Ui::DisplayMetrics& display){
    if(
        m_lastDisplay.logicalWidth == display.logicalWidth && m_lastDisplay.logicalHeight == display.logicalHeight
        && m_lastDisplay.pixelScaleX == display.pixelScaleX && m_lastDisplay.pixelScaleY == display.pixelScaleY
    )
        return;
    m_lastDisplay = display;
    m_displayChanged = true;
    NWB_LOGGER_ESSENTIAL_INFO(GLOBAL_TEXT("UiListSmoke: display logical={}x{} scale={}x{}")
        , display.logicalWidth, display.logicalHeight, display.pixelScaleX, display.pixelScaleY
    );
}

Array<u64, 12u> UiListSmokeScene::values()const{
    const auto& placement = m_state.placement();
    return { m_state.selectedKey(), m_state.cursorKey(), m_source.rowCount(), placement.firstRow, placement.endRow,
        m_source.labelReads(), static_cast<u64>(m_focused), static_cast<u64>(m_source.reversed()),
        m_source.removedKey(), static_cast<u64>(m_visible), m_commits, m_underlying };
}

Impl::Ui::Rect UiListSmokeScene::selectedRow()const{
    Impl::Ui::Rect rectangle;
    u64 index = 0u;
    const auto& placement = m_state.placement();
    if(
        !m_visible || !m_source.indexOf(m_state.selectedKey(), index)
        || index < placement.firstRow || index >= placement.endRow
        || !Impl::Ui::ScrollLayout::RowBounds(index, placement, __hidden_ui_list_smoke::s_RowHeight, rectangle)
    )
        return {};
    return rectangle;
}

void UiListSmokeScene::observeState(){
    const auto current = values();
    const auto& placement = m_state.placement();
    if(
        m_sequence != 0u && !m_displayChanged && current == m_lastValues
        && placement.offset == m_lastPlacement.offset
        && SameSmokeRect(placement.bounds, m_lastPlacement.bounds)
        && SameSmokeRect(placement.thumb, m_lastPlacement.thumb)
    )
        return;
    ++m_sequence;
    m_lastValues = current;
    m_lastPlacement = placement;
    m_displayChanged = false;
    NWB_LOGGER_ESSENTIAL_INFO(GLOBAL_TEXT("UiListSmoke: state sequence={} values={},{},{},{},{},{},{},{},{},{},{},{}")
        , m_sequence, current[0], current[1], current[2], current[3], current[4], current[5]
        , current[6], current[7], current[8], current[9], current[10], current[11]
    );
    LogSmokeRect(GLOBAL_TEXT("UiListSmoke"), m_sequence, GLOBAL_TEXT("list"), placement.bounds);
    LogSmokeRect(GLOBAL_TEXT("UiListSmoke"), m_sequence, GLOBAL_TEXT("viewport"), placement.viewport);
    LogSmokeRect(GLOBAL_TEXT("UiListSmoke"), m_sequence, GLOBAL_TEXT("track"), placement.track);
    LogSmokeRect(GLOBAL_TEXT("UiListSmoke"), m_sequence, GLOBAL_TEXT("thumb"), placement.thumb);
    LogSmokeRect(GLOBAL_TEXT("UiListSmoke"), m_sequence, GLOBAL_TEXT("selected_row"), selectedRow());
    LogSmokeRect(GLOBAL_TEXT("UiListSmoke"), m_sequence, GLOBAL_TEXT("counter"), __hidden_ui_list_smoke::s_Counter);
}

void UiListSmokeScene::paintMarkers(Impl::UiPaintContext& context)const{
    const auto current = values();
    const f32 y = context.display.logicalHeight - 20.0f;
    for(usize index = 0u; index <= current.size(); ++index){
        const u64 value = index == current.size() ? m_sequence : current[index];
        for(u32 part = 0u; part < 2u; ++part){
            const usize marker = index * 2u + part;
            context.paint.fillRect({ 12.0f + static_cast<f32>(marker) * 20.0f, y, 14.0f, 12.0f },
                EncodeSmokeColor(value >> (part * 12u)));
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool IsUiLayerListSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_LIST");
}

bool IsUiLayerListSkinSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_LIST_SKIN");
}

SharedUiListSmokeScene CreateUiListSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input){
    return SharedUiListSmokeScene(
        NewArenaObject<RefCounter<UiListSmokeScene>>(arena, input),
        ArenaRefDeleter<RefCounter<UiListSmokeScene>, Core::Alloc::GlobalArena>(&arena),
        AdoptRef
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

