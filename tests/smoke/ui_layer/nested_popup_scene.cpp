// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "nested_popup_scene.h"

#include "smoke_geometry.h"
#include "../smoke_environment.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_nested_popup_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr Impl::Ui::Rect s_Open{ 32.0f, 52.0f, 140.0f, 28.0f };
static constexpr Impl::Ui::Rect s_Counter{ 176.0f, 52.0f, 140.0f, 28.0f };
static constexpr Array<TStringView, 26u> s_RectNames{
    NWB_TEXT("open"), NWB_TEXT("counter"), NWB_TEXT("parent"), NWB_TEXT("child"),
    NWB_TEXT("before_button"), NWB_TEXT("before_edit"), NWB_TEXT("before_list"), NWB_TEXT("before_row2"),
    NWB_TEXT("child_open"), NWB_TEXT("after_button"), NWB_TEXT("after_edit"), NWB_TEXT("after_list"),
    NWB_TEXT("after_row2"), NWB_TEXT("child_action"), NWB_TEXT("child_edit"), NWB_TEXT("close_branch"),
    NWB_TEXT("combo"), NWB_TEXT("combo_popup"), NWB_TEXT("combo_row2"), NWB_TEXT("search"),
    NWB_TEXT("search_popup"), NWB_TEXT("search_query"), NWB_TEXT("search_row2"), NWB_TEXT("menu_anchor"),
    NWB_TEXT("menu"), NWB_TEXT("menu_row2")
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiNestedPopupSmokeScene::UiNestedPopupSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input)
    : m_input(input)
    , m_source(arena)
    , m_search(arena)
    , m_beforeText(arena)
    , m_afterText(arena)
    , m_childText(arena)
{
    const bool initialized = m_beforeText.setText("Before") && m_afterText.setText("After") && m_childText.setText("Child");
    NWB_FATAL_ASSERT(initialized);
    m_combo.select(1u);
    m_search.combo().select(1u);
    m_input.addHandlerToBack(*this);
}

UiNestedPopupSmokeScene::~UiNestedPopupSmokeScene(){
    m_input.removeHandler(*this);
}

bool UiNestedPopupSmokeScene::paint(Impl::UiPaintContext& context){
    observeDisplay(context.display);
    m_source.beginFrame();
    m_childDeclared = false;
    auto& ui = context.ui;
    ui.style().fontSize = 12.0f;
    ui.style().gap = 4.0f;
    if(!paintRoot(ui))
        return false;
    Impl::Ui::PopupOptions options;
    options.anchor = { 72.0f, 54.0f, 120.0f, 24.0f };
    options.size = { 300.0f, 420.0f };
    if(ui.beginPopup("parent", m_parent, options)){
        if(!paintParent(ui) || !ui.endPopup())
            return false;
    }
    if(ui.failed())
        return false;
    observeState(context);
    paintMarkers(context);
    return true;
}

bool UiNestedPopupSmokeScene::keyboardUpdate(const i32 key, const i32 scancode, const i32 action, const i32 mods){
    static_cast<void>(scancode);
    static_cast<void>(mods);
    if(key != Core::Key::F8)
        return false;
    if(action == Core::InputAction::Press)
        m_parent.close();
    return true;
}

bool UiNestedPopupSmokeScene::paintRoot(Impl::Ui::Builder& ui){
    using namespace Impl::Ui;
    if(!ui.beginPanel("nested_root", { 24.0f, 24.0f, 300.0f, 80.0f }))
        return false;
    const WidgetOptions caption{ {}, { LayoutSizePolicy::Fixed, 16.0f } };
    if(!ui.label("title", "Nested popup ownership / F8 closes ancestor", caption))
        return false;
    ContainerOptions row;
    row.height = { LayoutSizePolicy::Fixed, 28.0f };
    row.gap = 4.0f;
    if(!ui.beginRow("root_buttons", row))
        return false;
    const WidgetOptions button{ { LayoutSizePolicy::Fixed, 140.0f }, { LayoutSizePolicy::Fixed, 28.0f } };
    if(ui.button("open", "Open parent", button))
        m_parent.open();
    if(ui.button("counter", "Outside counter", button))
        ++m_outside;
    return ui.endContainer() && ui.endPanel();
}

bool UiNestedPopupSmokeScene::paintParent(Impl::Ui::Builder& ui){
    using namespace Impl::Ui;
    const WidgetOptions caption{ {}, { LayoutSizePolicy::Fixed, 16.0f } };
    const WidgetOptions button{ { LayoutSizePolicy::Fixed, 100.0f }, { LayoutSizePolicy::Fixed, 32.0f } };
    const WidgetOptions wide{ { LayoutSizePolicy::Stretch, 1.0f }, { LayoutSizePolicy::Fixed, 32.0f } };
    ContainerOptions row;
    row.height = { LayoutSizePolicy::Fixed, 32.0f };
    row.gap = 4.0f;
    EditBoxOptions edit;
    edit.height = { LayoutSizePolicy::Fixed, 32.0f };
    ListOptions list;
    list.height = { LayoutSizePolicy::Fixed, 56.0f };
    list.rowHeight = 20.0f;
    if(!ui.label("title", "Parent scope resumes after nested child", caption) || !ui.beginRow("before", row))
        return false;
    if(ui.button("action", "Before", button))
        ++m_beforeClicks;
    const EditBoxResult before = ui.editBox("edit", m_beforeText, m_beforeEdit, edit);
    if(!before.valid || !ui.endContainer() || !ui.virtualList("before_list", m_source, m_beforeList, list).valid)
        return false;
    if(ui.button("child_open", "Open right child", wide))
        m_child.open();
    if(!paintChild(ui) || !ui.beginRow("after", row))
        return false;
    if(ui.button("action", "After", button))
        ++m_afterClicks;
    const EditBoxResult after = ui.editBox("edit", m_afterText, m_afterEdit, edit);
    if(!after.valid || !ui.endContainer() || !ui.virtualList("after_list", m_source, m_afterList, list).valid)
        return false;
    ComboOptions combo;
    combo.height = { LayoutSizePolicy::Fixed, 32.0f };
    combo.popupHeight = 172.0f;
    combo.rowHeight = 24.0f;
    if(!ui.comboBox("combo", m_source, m_combo, combo).valid)
        return false;
    SearchComboOptions search;
    search.combo = combo;
    search.combo.popupHeight = 196.0f;
    if(!ui.searchComboBox("search", m_source, m_search, search).combo.valid)
        return false;
    if(ui.button("menu_anchor", "Right-click for context commands", wide))
        m_command = 0u;
    ContextMenuOptions menu;
    menu.size = { 200.0f, 172.0f };
    const ContextMenuResult command = ui.contextMenu("commands", "menu_anchor", m_source, m_menu, menu);
    if(command.activated)
        m_command = command.key;
    return command.valid;
}

bool UiNestedPopupSmokeScene::paintChild(Impl::Ui::Builder& ui){
    using namespace Impl::Ui;
    PopupOptions options;
    const Rect& parent = m_parent.placement().bounds;
    options.anchor = { parent.x + parent.width - 8.0f, parent.y + 124.0f, 8.0f, 32.0f };
    options.side = PopupPlacementSide::Right;
    options.size = { 220.0f, 178.0f };
    if(!ui.beginPopup("child", m_child, options))
        return !ui.failed();
    m_childDeclared = true;
    const WidgetOptions caption{ {}, { LayoutSizePolicy::Fixed, 16.0f } };
    const WidgetOptions button{ { LayoutSizePolicy::Stretch, 1.0f }, { LayoutSizePolicy::Fixed, 32.0f } };
    if(!ui.label("title", "Child escapes parent clip", caption))
        return false;
    if(ui.button("action", "Child action", button))
        ++m_childClicks;
    EditBoxOptions edit;
    edit.height = { LayoutSizePolicy::Fixed, 32.0f };
    const EditBoxResult text = ui.editBox("edit", m_childText, m_childEdit, edit);
    if(ui.button("close_branch", "Close whole ancestor branch", button))
        m_parent.close();
    return text.valid && ui.endPopup();
}

void UiNestedPopupSmokeScene::observeDisplay(const Impl::Ui::DisplayMetrics& display){
    if(
        m_lastDisplay.logicalWidth == display.logicalWidth && m_lastDisplay.logicalHeight == display.logicalHeight
        && m_lastDisplay.pixelScaleX == display.pixelScaleX && m_lastDisplay.pixelScaleY == display.pixelScaleY
    )
        return;
    m_lastDisplay = display;
    m_displayChanged = true;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiNestedPopupSmoke: display logical={}x{} scale={}x{}")
        , display.logicalWidth, display.logicalHeight, display.pixelScaleX, display.pixelScaleY
    );
}

void UiNestedPopupSmokeScene::observeState(Impl::UiPaintContext& context){
    using namespace Impl::Ui;
    m_rectangles.fill({});
    m_rectangles[0] = __hidden_ui_nested_popup_smoke::s_Open;
    m_rectangles[1] = __hidden_ui_nested_popup_smoke::s_Counter;
    if(m_parent.isOpen()){
        const Rect parent = m_parent.placement().bounds;
        m_rectangles[2] = parent;
        m_rectangles[4] = { parent.x + 8.0f, parent.y + 28.0f, 100.0f, 32.0f };
        m_rectangles[5] = m_beforeEdit.placement.bounds;
        m_rectangles[6] = m_beforeList.placement().bounds;
        m_rectangles[7] = rowBounds(m_beforeList, m_source, 2u, 20.0f);
        m_rectangles[8] = { parent.x + 8.0f, parent.y + 124.0f, parent.width - 16.0f, 32.0f };
        m_rectangles[9] = { parent.x + 8.0f, parent.y + 160.0f, 100.0f, 32.0f };
        m_rectangles[10] = m_afterEdit.placement.bounds;
        m_rectangles[11] = m_afterList.placement().bounds;
        m_rectangles[12] = rowBounds(m_afterList, m_source, 2u, 20.0f);
        m_rectangles[16] = m_combo.bounds();
        m_rectangles[19] = m_search.combo().bounds();
        m_rectangles[23] = { parent.x + 8.0f, parent.y + 328.0f, parent.width - 16.0f, 32.0f };
        if(m_childDeclared && m_child.isOpen()){
            const Rect child = m_child.placement().bounds;
            m_rectangles[3] = child;
            m_rectangles[13] = { child.x + 8.0f, child.y + 28.0f, child.width - 16.0f, 32.0f };
            m_rectangles[14] = m_childEdit.placement.bounds;
            m_rectangles[15] = { child.x + 8.0f, child.y + 100.0f, child.width - 16.0f, 32.0f };
        }
        if(m_combo.isOpen()){
            m_rectangles[17] = m_combo.placement().bounds;
            m_rectangles[18] = rowBounds(m_combo.listState(), m_source, 2u, 24.0f);
        }
        if(m_search.combo().isOpen()){
            m_rectangles[20] = m_search.combo().placement().bounds;
            m_rectangles[21] = m_search.editorState().placement.bounds;
            m_rectangles[22] = rowBounds(m_search.combo().listState(), m_source.filtered(), 2u, 24.0f);
        }
        if(m_menu.isOpen()){
            m_rectangles[24] = m_menu.placement().bounds;
            m_rectangles[25] = rowBounds(m_menu.listState(), m_source, 2u, 28.0f);
        }
    }
    m_focusScope = 0u;
    m_focusCode = 0u;
    m_popupTargets = 0u;
    const auto& input = context.ui.input();
    m_popupCount = static_cast<u32>(input.popupCount());
    m_childParentScope = false;
    for(const auto& target : input.targets()){
        if(target.popup.valid())
            ++m_popupTargets;
        if(target.popup.instanceGeneration == m_child.instanceGeneration()){
            const PopupScope* scope = input.popupScope(target.popup);
            m_childParentScope |= scope && scope->parent.instanceGeneration == m_parent.instanceGeneration();
        }
        if(target.id != input.focus())
            continue;
        if(target.popup.valid()){
            m_focusScope = target.popup.instanceGeneration == m_child.instanceGeneration() ? 2u
                : target.popup.instanceGeneration == m_parent.instanceGeneration() ? 1u : 3u;
        }
        for(usize index = 0u; index < m_rectangles.size(); ++index){
            if(SameSmokeRect(target.rectangle, m_rectangles[index])){
                m_focusCode = static_cast<u32>(index + 1u);
                break;
            }
        }
    }
    const auto current = values();
    bool geometryChanged = false;
    for(usize index = 0u; index < m_rectangles.size(); ++index)
        geometryChanged |= !SameSmokeRect(m_rectangles[index], m_lastRectangles[index]);
    if(m_sequence != 0u && !m_displayChanged && !geometryChanged && current == m_lastValues)
        return;
    ++m_sequence;
    m_lastValues = current;
    m_lastRectangles = m_rectangles;
    m_displayChanged = false;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiNestedPopupSmoke: state sequence={} values={},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}")
        , m_sequence, current[0], current[1], current[2], current[3], current[4], current[5], current[6]
        , current[7], current[8], current[9], current[10], current[11], current[12], current[13]
        , current[14], current[15], current[16], current[17], current[18], current[19], current[20]
        , current[21], current[22], current[23], current[24], current[25], current[26], current[27], current[28]
    );
    for(usize index = 0u; index < m_rectangles.size(); ++index)
        LogSmokeRect(NWB_TEXT("UiNestedPopupSmoke"), m_sequence, __hidden_ui_nested_popup_smoke::s_RectNames[index], m_rectangles[index]);
}

void UiNestedPopupSmokeScene::paintMarkers(Impl::UiPaintContext& context)const{
    const auto current = values();
    const f32 y = context.display.logicalHeight - 18.0f;
    for(usize index = 0u; index <= current.size(); ++index){
        const u64 value = index == current.size() ? m_sequence : current[index];
        for(u32 part = 0u; part < 2u; ++part){
            const usize marker = index * 2u + part;
            context.paint.fillRect({ 12.0f + static_cast<f32>(marker) * 12.0f, y, 8.0f, 8.0f },
                EncodeSmokeColor(value >> (part * 12u)));
        }
    }
}

Array<u64, 29u> UiNestedPopupSmokeScene::values()const{
    const bool parent = m_parent.isOpen();
    return { static_cast<u64>(parent), static_cast<u64>(parent && m_childDeclared && m_child.isOpen()), m_focusScope, m_focusCode,
        m_beforeClicks, m_afterClicks, m_childClicks, m_outside, static_cast<u64>(m_beforeText.text().size()),
        static_cast<u64>(m_afterText.text().size()), static_cast<u64>(m_childText.text().size()),
        m_beforeList.selectedKey(), m_afterList.selectedKey(), static_cast<u64>(parent && m_combo.isOpen()), m_combo.selectedKey(),
        static_cast<u64>(parent && m_search.combo().isOpen()), m_search.combo().selectedKey(),
        static_cast<u64>(m_search.query().text().size()), static_cast<u64>(parent && m_menu.isOpen()), m_menu.cursorKey(), m_command,
        m_source.labelReads(), static_cast<u64>(m_child.isOpen()), static_cast<u64>(m_combo.isOpen()),
        static_cast<u64>(m_search.combo().isOpen()), static_cast<u64>(m_menu.isOpen()), m_popupTargets, m_popupCount,
        static_cast<u64>(m_childParentScope) };
}

Impl::Ui::Rect UiNestedPopupSmokeScene::rowBounds(const Impl::Ui::ListState& state,
    const Impl::Ui::IListDataSource& source, const u64 key, const f32 rowHeight)const{
    Impl::Ui::Rect result;
    u64 index = 0u;
    const auto& placement = state.placement();
    if(
        !source.indexOf(key, index) || index < placement.firstRow || index >= placement.endRow
        || !Impl::Ui::ScrollLayout::RowBounds(index, placement, rowHeight, result)
    )
        return {};
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool IsUiLayerNestedPopupSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_NESTED_POPUP");
}

bool IsUiLayerNestedPopupSkinSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_NESTED_POPUP_SKIN");
}

SharedUiNestedPopupSmokeScene CreateUiNestedPopupSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input){
    return SharedUiNestedPopupSmokeScene(
        NewArenaObject<RefCounter<UiNestedPopupSmokeScene>>(arena, arena, input),
        ArenaRefDeleter<RefCounter<UiNestedPopupSmokeScene>, Core::Alloc::GlobalArena>(&arena),
        s_AdoptRef
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

