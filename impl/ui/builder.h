// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "context.h"
#include "layout/tree.h"
#include "text/service.h"
#include "widgets/style.h"
#include "widgets/window.h"
#include "widgets/edit_box_state.h"
#include "widgets/popup.h"
#include "widgets/popup_style.h"
#include "widgets/list.h"
#include "widgets/list_style.h"
#include "widgets/combo.h"
#include "widgets/combo_style.h"
#include "widgets/search_combo.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Transient declarations shape text, arrange a panel, then freeze paint and hit targets in the same order.
class Builder final : NoCopy{
private:
    struct Item{
        WidgetState state;
        TextLayout text;
        EditBoxView editView;
        EditBoxState* editState = nullptr;
        EditBoxOptions editOptions;
        EditBoxPaintFlags editFlags;
        Insets padding;
        f32 checkboxExtent = 0.0f;
        u32 node = 0u;
        u32 list = s_LayoutNoParent;
        u32 combo = s_LayoutNoParent;
        bool enabled = true;
        bool checked = false;

        explicit Item(Core::Alloc::GlobalArena& arena)
            : text(arena)
            , editView(arena)
        {}
        Item(Item&&) = default;
        Item& operator=(Item&&) = default;
        Item(const Item&) = delete;
        Item& operator=(const Item&) = delete;
    };

    struct WindowFrame{
        WindowState* state = nullptr;
        WindowOptions options;
        WindowMetrics metrics;
        WidgetState titleState;
        WidgetState collapseState;
        WidgetState resizeState;
        TextLayout title;
        bool firstUse = false;

        explicit WindowFrame(Core::Alloc::GlobalArena& arena)
            : title(arena)
        {}
    };

    struct ListFrame{
        const IListDataSource* source = nullptr;
        ListState* state = nullptr;
        ListOptions options;
        ControlToken token;
        Insets padding;
        u64 rowCount = 0u;
        bool focusOnCommit = false;
        WidgetId keyboardFocus;
    };

    struct ComboFrame{
        const IListDataSource* source = nullptr;
        const IListDataSource* results = nullptr;
        ISearchableListDataSource* searchSource = nullptr;
        SearchComboState* search = nullptr;
        ComboState* state = nullptr;
        ComboOptions options;
        WidgetState popup;
        WidgetState rows;
        PopupToken popupToken;
        ControlToken token;
        ControlToken listToken;
        Rect visibleField;
        u64 rowCount = 0u;
        u64 resultCount = 0u;
        u64 queryGeneration = 0u;
        u64 queryRevision = 0u;
        u64 queryCompositionGeneration = 0u;
        u64 queryExternalRevision = 0u;
        usize queryAnchor = 0u;
        usize queryCaret = 0u;
        u32 editor = s_LayoutNoParent;
        u32 list = s_LayoutNoParent;
        f32 arrowExtent = 0.0f;
        f32 queryHeight = 0.0f;
        f32 queryGap = 0.0f;
        bool open = false;
        bool focusOnCommit = false;
        bool listFocusOnCommit = false;
        bool editorSubmitted = false;
        bool queryChanged = false;
    };


public:
    Builder(Core::Alloc::GlobalArena& arena, Context& context, PaintBuilder& paint, TextService& text);


public:
    [[nodiscard]] bool beginPanel(AStringView stableKey, const Rect& bounds, LayoutDirection::Enum direction = LayoutDirection::Column);
    [[nodiscard]] bool endPanel();
    // Returns content visibility. A collapsed window still opens a scope and requires endWindow().
    // The borrowed WindowState must remain alive through that matching endWindow().
    [[nodiscard]] bool beginWindow(AStringView stableKey, StringView title, WindowState& state, const WindowOptions& options = {});
    [[nodiscard]] bool endWindow();
    // Popups begin after the preceding panel/window is balanced. Only a visible begin requires endPopup().
    [[nodiscard]] bool beginPopup(AStringView stableKey, PopupState& state, const PopupOptions& options = {});
    [[nodiscard]] bool endPopup();
    [[nodiscard]] bool beginRow(AStringView stableKey, const ContainerOptions& options = {});
    [[nodiscard]] bool beginColumn(AStringView stableKey, const ContainerOptions& options = {});
    [[nodiscard]] bool endContainer();
    [[nodiscard]] bool label(AStringView stableKey, StringView text, const WidgetOptions& options = {});
    // Return one action at most per declaration. Disabled controls discard stale activation and remain pointer barriers.
    [[nodiscard]] bool button(AStringView stableKey, StringView text, const WidgetOptions& options = {});
    [[nodiscard]] bool checkbox(AStringView stableKey, StringView text, bool& checked, const WidgetOptions& options = {});
    [[nodiscard]] bool selectable(AStringView stableKey, StringView text, bool selected, const WidgetOptions& options = {});
    // Borrowed through the matching endPanel/endWindow/endPopup; explicit state/source changes must wait until that scope ends.
    // Only visible rows are borrowed and shaped; stable keys must be unique and lookup/search efficient.
    [[nodiscard]] ListResult virtualList(AStringView stableKey, const IListDataSource& source, ListState& state, const ListOptions& options = {});
    // The field and its popup borrow state/source through endPanel/endWindow; popup input is consumed in this declaration.
    // Internal popup/list scopes are emitted automatically after the containing scope's layout. User-popup nesting is not supported.
    [[nodiscard]] ComboResult comboBox(AStringView stableKey, const IListDataSource& source, ComboState& state, const ComboOptions& options = {});
    [[nodiscard]] SearchComboResult searchComboBox(AStringView stableKey, ISearchableListDataSource& source,
        SearchComboState& state, const SearchComboOptions& options = {});
    [[nodiscard]] bool separator(AStringView stableKey, const SeparatorOptions& options = {});
    [[nodiscard]] EditBoxResult editBox(AStringView stableKey, EditModel& model, EditBoxState& state, const EditBoxOptions& options = {});
    [[nodiscard]] bool balanced()const{ return !m_panelActive && !m_windowActive && !m_popupState; }
    void reset();
    void setSkin(const UiSkin& skin){ m_skin = &skin; }
    [[nodiscard]] WidgetStyle& style(){ return m_style; }
    [[nodiscard]] EditBoxStyle& editStyle(){ return m_editStyle; }
    [[nodiscard]] PopupStyle& popupStyle(){ return m_popupStyle; }
    [[nodiscard]] ListStyle& listStyle(){ return m_listStyle; }
    [[nodiscard]] ComboStyle& comboStyle(){ return m_comboStyle; }
    void setEditHost(IEditBoxHost* host){ m_editHost = host; }
    void setDeltaSeconds(f32 delta){ m_deltaSeconds = delta; }
    // Valid while a window scope is open, including a collapsed window.
    [[nodiscard]] const WindowMetrics& windowMetrics()const{ return m_window.metrics; }


private:
    [[nodiscard]] bool beginContainer(AStringView stableKey, LayoutDirection::Enum direction, const ContainerOptions& options);
    [[nodiscard]] Item* addItem(AStringView stableKey, StringView text, WidgetKind::Enum kind, const WidgetOptions& options);
    [[nodiscard]] const UiSkinRegion* region(const Name& preferred, const Name& fallback)const;
    void buttonMetrics(Point& size, Insets& padding)const;
    [[nodiscard]] bool paintPanel();
    [[nodiscard]] bool paintWindow();
    [[nodiscard]] bool paintWindowTitle();
    [[nodiscard]] bool paintWindowResize();
    [[nodiscard]] bool paintItems();
    [[nodiscard]] bool paintItem(const Item& item, const LayoutBox& box);
    [[nodiscard]] bool prepareEditBox(Item& item, EditModel& model, EditBoxState& state, const EditBoxResult& result);
    [[nodiscard]] bool paintEditBox(const Item& item, const LayoutBox& box, const HitTarget* navigation = nullptr);
    [[nodiscard]] bool paintSelectable(const Item& item, const LayoutBox& box);
    [[nodiscard]] bool paintList(const Item& item, const LayoutBox& box);
    [[nodiscard]] bool paintListRows(const Item& item, const ListFrame& frame, const ScrollPlacement& placement);
    [[nodiscard]] bool applyListGesture(ListState& state, const PointerGesture& gesture);
    [[nodiscard]] ComboResult declareCombo(AStringView stableKey, const IListDataSource& source,
        ComboState& state, const ComboOptions& options, SearchComboState* search = nullptr,
        ISearchableListDataSource* searchSource = nullptr, const SearchComboOptions* searchOptions = nullptr);
    [[nodiscard]] bool paintCombo(const Item& item, const LayoutBox& box);
    [[nodiscard]] bool paintCombos();
    [[nodiscard]] bool paintComboPopup(ComboFrame& frame);
    [[nodiscard]] bool comboStateMatches(const ComboFrame& frame)const;
    [[nodiscard]] bool comboMatches(const ComboFrame& frame)const;
    [[nodiscard]] bool applyComboInput(const WidgetState& field, ComboFrame& frame, ComboResult& result);
    [[nodiscard]] bool applyComboListInput(ComboFrame& frame, ComboResult& result);
    [[nodiscard]] bool prepareComboSearch(const WidgetState& field, ComboFrame& frame);
    void snapshotComboQuery(ComboFrame& frame);
    [[nodiscard]] bool paintComboQuery(ComboFrame& frame, LayoutBox& content);
    [[nodiscard]] bool paintPopup();
    [[nodiscard]] bool synchronizePopup();
    [[nodiscard]] Rect visibleClip(const Rect& clip)const;


private:
    Core::Alloc::GlobalArena& m_arena;
    Context& m_context;
    PaintBuilder& m_paint;
    TextService& m_text;
    const UiSkin* m_skin = nullptr;
    WidgetStyle m_style;
    EditBoxStyle m_editStyle;
    PopupStyle m_popupStyle;
    ListStyle m_listStyle;
    ComboStyle m_comboStyle;
    PopupState* m_popupState = nullptr;
    PopupOptions m_popupOptions;
    PopupPlacement m_popupPlacement;
    IEditBoxHost* m_editHost = nullptr;
    f32 m_deltaSeconds = 0.0f;
    LayoutTree m_layout;
    PaintVector<Item> m_items;
    PaintVector<ListFrame> m_lists;
    PaintVector<ComboFrame> m_combos;
    PaintVector<Item> m_comboEditors;
    PaintVector<u32> m_stack;
    WindowFrame m_window;
    WidgetState m_panelState;
    Rect m_bounds;
    bool m_panelActive = false;
    bool m_windowActive = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

