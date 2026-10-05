// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "search_combo_scene.h"

#include "smoke_geometry.h"
#include "edit_selection_probe.h"
#include "../smoke_environment.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_search_combo_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr Impl::Ui::Rect s_Panel{ 24.0f, 24.0f, 400.0f, 160.0f };
static constexpr Impl::Ui::Rect s_Counter{ 456.0f, 32.0f, 130.0f, 36.0f };
static constexpr f32 s_RowHeight = 24.0f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiSearchComboSmokeScene::UiSearchComboSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input)
    : m_arena(arena)
    , m_input(input)
    , m_source(arena)
    , m_state(arena)
{
    m_input.addHandlerToBack(*this);
}

UiSearchComboSmokeScene::~UiSearchComboSmokeScene(){
    m_input.removeHandler(*this);
}

bool UiSearchComboSmokeScene::paint(Impl::UiPaintContext& context){
    using namespace Impl::Ui;
    observeDisplay(context.display);
    m_source.beginFrame();
    m_focused = false;
    Builder& ui = context.ui;
    ui.style().fontSize = 12.0f;
    ui.style().gap = 8.0f;
    ui.listStyle().padding = { 8.0f, 8.0f, 8.0f, 8.0f };
    const Rect panel = __hidden_ui_search_combo_smoke::s_Panel;
    if(!ui.beginPanel("search_combo_fixture", panel))
        return false;
    const WidgetOptions title{ {}, { LayoutSizePolicy::Fixed, 24.0f } };
    if(!ui.label("title", "Search 100000 rows by numeric prefix", title))
        return false;
    SearchComboOptions options;
    options.combo.height = { LayoutSizePolicy::Fixed, 36.0f };
    options.combo.popupHeight = 280.0f;
    options.combo.rowHeight = __hidden_ui_search_combo_smoke::s_RowHeight;
    options.combo.enabled = m_enabled;
    const SearchComboResult result = ui.searchComboBox("choice", m_source, m_state, options);
    if(!result.combo.valid)
        return false;
    m_focused = result.combo.focused;
    if(result.combo.committed)
        ++m_commits;
    if(!ui.label("hint", "Type 12 or 5 / Arrows preview / Enter commit", title) || !ui.endPanel())
        return false;
    if(!ui.beginPanel("outside_fixture", { 448.0f, 24.0f, 146.0f, 52.0f }))
        return false;
    const WidgetOptions counter{ { LayoutSizePolicy::Fixed, 130.0f }, { LayoutSizePolicy::Fixed, 36.0f } };
    if(ui.button("underlying", "Outside counter", counter))
        ++m_underlying;
    if(!ui.endPanel())
        return false;
    observeState(context.text);
    paintMarkers(context);
    return true;
}

bool UiSearchComboSmokeScene::keyboardUpdate(const i32 key, const i32 scancode, const i32 action, const i32 mods){
    static_cast<void>(scancode);
    static_cast<void>(mods);
    if(key < Core::Key::F4 || key > Core::Key::F9)
        return false;
    if(action != Core::InputAction::Press)
        return true;
    switch(key){
    case Core::Key::F4:
        m_state.combo().select(100000u);
        break;
    case Core::Key::F5:
        m_source.reverse();
        break;
    case Core::Key::F6:
        m_source.remove(m_state.combo().selectedKey());
        break;
    case Core::Key::F7:{
        const bool cleared = m_state.query().setText({});
        GLB_FATAL_ASSERT(cleared);
        break;
    }
    case Core::Key::F9:
        m_enabled = !m_enabled;
        break;
    default:
        break;
    }
    return true;
}

void UiSearchComboSmokeScene::observeDisplay(const Impl::Ui::DisplayMetrics& display){
    if(
        m_lastDisplay.logicalWidth == display.logicalWidth && m_lastDisplay.logicalHeight == display.logicalHeight
        && m_lastDisplay.pixelScaleX == display.pixelScaleX && m_lastDisplay.pixelScaleY == display.pixelScaleY
    )
        return;
    m_lastDisplay = display;
    m_displayChanged = true;
    NWB_LOGGER_ESSENTIAL_INFO(GLB_TEXT("UiSearchComboSmoke: display logical={}x{} scale={}x{}")
        , display.logicalWidth, display.logicalHeight, display.pixelScaleX, display.pixelScaleY
    );
}

Array<u64, 22u> UiSearchComboSmokeScene::values()const{
    const auto& combo = m_state.combo();
    const auto& list = combo.listState();
    const auto& placement = list.placement();
    const auto& query = m_state.query();
    u32 hash = 2166136261u;
    for(const char character : query.text())
        hash = (hash ^ static_cast<u8>(character)) * 16777619u;
    return { combo.selectedKey(), list.cursorKey(), m_source.rowCount(), m_source.filtered().rowCount(),
        placement.firstRow, placement.endRow, m_source.labelReads(), static_cast<u64>(combo.isOpen()),
        static_cast<u64>(query.text().size()), static_cast<u64>(hash & 0xffffffu), static_cast<u64>(query.anchor()),
        static_cast<u64>(query.caret()), static_cast<u64>(m_state.editorState().focused), static_cast<u64>(m_focused),
        m_commits, m_source.filtered().revision(), static_cast<u64>(m_source.reversed()), m_source.removedKey(),
        static_cast<u64>(m_enabled), m_source.revision(), m_underlying, static_cast<u64>(query.composition().active) };
}

Impl::Ui::Rect UiSearchComboSmokeScene::cursorRow()const{
    Impl::Ui::Rect rectangle;
    u64 index = 0u;
    const auto& list = m_state.combo().listState();
    const auto& placement = list.placement();
    if(
        !m_state.combo().isOpen() || !m_source.filtered().indexOf(list.cursorKey(), index)
        || index < placement.firstRow || index >= placement.endRow
        || !Impl::Ui::ScrollLayout::RowBounds(index, placement, __hidden_ui_search_combo_smoke::s_RowHeight, rectangle)
    )
        return {};
    return rectangle;
}

void UiSearchComboSmokeScene::observeState(Impl::Ui::TextService& text){
    const auto current = values();
    const auto& placement = m_state.combo().listState().placement();
    const auto& popup = m_state.combo().placement();
    const auto& bounds = m_state.combo().bounds();
    if(
        m_sequence != 0u && !m_displayChanged && current == m_lastValues
        && placement.offset == m_lastPlacement.offset
        && SameSmokeRect(placement.bounds, m_lastPlacement.bounds)
        && SameSmokeRect(placement.thumb, m_lastPlacement.thumb)
        && SameSmokeRect(popup.bounds, m_lastPopup.bounds)
        && SameSmokeRect(bounds, m_lastBounds)
        && SameSmokeRect(m_state.editorState().placement.bounds, m_lastQueryBounds)
    )
        return;
    ++m_sequence;
    m_lastValues = current;
    m_lastPlacement = placement;
    m_lastPopup = popup;
    m_lastBounds = bounds;
    m_lastQueryBounds = m_state.editorState().placement.bounds;
    m_displayChanged = false;
    NWB_LOGGER_ESSENTIAL_INFO(GLB_TEXT("UiSearchComboSmoke: state sequence={} values={},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}")
        , m_sequence, current[0], current[1], current[2], current[3], current[4], current[5], current[6], current[7]
        , current[8], current[9], current[10], current[11], current[12], current[13], current[14], current[15]
        , current[16], current[17], current[18], current[19], current[20], current[21]
    );
    LogSmokeRect(GLB_TEXT("UiSearchComboSmoke"), m_sequence, GLB_TEXT("trigger"), bounds);
    LogSmokeRect(GLB_TEXT("UiSearchComboSmoke"), m_sequence, GLB_TEXT("popup"), popup.bounds);
    LogSmokeRect(GLB_TEXT("UiSearchComboSmoke"), m_sequence, GLB_TEXT("list"), placement.bounds);
    LogSmokeRect(GLB_TEXT("UiSearchComboSmoke"), m_sequence, GLB_TEXT("viewport"), placement.viewport);
    LogSmokeRect(GLB_TEXT("UiSearchComboSmoke"), m_sequence, GLB_TEXT("track"), placement.track);
    LogSmokeRect(GLB_TEXT("UiSearchComboSmoke"), m_sequence, GLB_TEXT("thumb"), placement.thumb);
    LogSmokeRect(GLB_TEXT("UiSearchComboSmoke"), m_sequence, GLB_TEXT("cursor_row"), cursorRow());
    LogSmokeRect(GLB_TEXT("UiSearchComboSmoke"), m_sequence, GLB_TEXT("counter"), __hidden_ui_search_combo_smoke::s_Counter);
    const auto& editor = m_state.editorState().placement;
    LogSmokeRect(GLB_TEXT("UiSearchComboSmoke"), m_sequence, GLB_TEXT("query"), editor.bounds);
    LogSmokeRect(GLB_TEXT("UiSearchComboSmoke"), m_sequence, GLB_TEXT("query_content"), editor.content);
    LogSmokeRect(GLB_TEXT("UiSearchComboSmoke"), m_sequence, GLB_TEXT("query_caret"), editor.caret);
    LogSmokeRect(GLB_TEXT("UiSearchComboSmoke"), m_sequence, GLB_TEXT("query_selection"),
        CaptureEditSelection(m_arena, text, m_state.query(), editor, 12.0f));
}

void UiSearchComboSmokeScene::paintMarkers(Impl::UiPaintContext& context)const{
    const auto current = values();
    const f32 y = context.display.logicalHeight - 20.0f;
    for(usize index = 0u; index <= current.size(); ++index){
        const u64 value = index == current.size() ? m_sequence : current[index];
        for(u32 part = 0u; part < 2u; ++part){
            const usize marker = index * 2u + part;
            context.paint.fillRect({ 12.0f + static_cast<f32>(marker) * 12.0f, y, 8.0f, 12.0f },
                EncodeSmokeColor(value >> (part * 12u)));
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool IsUiLayerSearchComboSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_SEARCH_COMBO");
}

bool IsUiLayerSearchComboSkinSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_SEARCH_COMBO_SKIN");
}

SharedUiSearchComboSmokeScene CreateUiSearchComboSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input){
    return SharedUiSearchComboSmokeScene(
        NewArenaObject<RefCounter<UiSearchComboSmokeScene>>(arena, arena, input),
        ArenaRefDeleter<RefCounter<UiSearchComboSmokeScene>, Core::Alloc::GlobalArena>(&arena),
        s_AdoptRef
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

