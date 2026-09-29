// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "context.h"
#include "layout/tree.h"
#include "text/service.h"
#include "widgets/style.h"
#include "widgets/window.h"
#include "widgets/edit_box_state.h"
#include "widgets/text_area.h"
#include "widgets/scrollbar_style.h"
#include "widgets/numeric_edit.h"
#include "widgets/popup.h"
#include "widgets/popup_style.h"
#include "widgets/list.h"
#include "widgets/list_style.h"
#include "widgets/radio_group_frame.h"
#include "widgets/slider_frame.h"
#include "widgets/progress_frame.h"
#include "widgets/combo.h"
#include "widgets/combo_style.h"
#include "widgets/search_combo.h"
#include "widgets/tooltip.h"
#include "widgets/tooltip_style.h"
#include "widgets/context_menu.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Each declaration scope owns its transient layout and control loans in the caller's UI arena.
class BuilderScopeFrame final : NoCopy{
    friend class Builder;


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
        u32 radioGroup = s_LayoutNoParent;
        u32 slider = s_LayoutNoParent;
        u32 progress = s_LayoutNoParent;
        u32 combo = s_LayoutNoParent;
        u32 textArea = s_LayoutNoParent;
        u32 integerEdit = s_LayoutNoParent;
        u32 floatEdit = s_LayoutNoParent;
        bool enabled = true;
        bool checked = false;
        bool contextMenu = false;
        bool annotated = false;

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

    struct TextAreaFrame{
        f32 wheelLines = 3.0f;
        const EditModel* model = nullptr;
        TextAreaState* state = nullptr;
        EditNavigationSnapshot navigation;
        u64 stateRevision = 0u;
        u64 modelGeneration = 0u;
        u64 modelRevision = 0u;
        u64 externalRevision = 0u;
        u64 selectionGeneration = 0u;
        u64 compositionGeneration = 0u;
        usize anchor = 0u;
        usize caret = 0u;
        u32 item = s_LayoutNoParent;
        bool focused = false;
    };

    struct NumericEditFrame{
        EditBoxState* state = nullptr;
        u64 revision = 0u;
        u64 draftGeneration = 0u;
        u64 draftRevision = 0u;
        u64 draftExternalRevision = 0u;
        u64 draftCompositionGeneration = 0u;
        u64 draftSelectionGeneration = 0u;
        usize anchor = 0u;
        usize caret = 0u;
        u32 item = s_LayoutNoParent;
        bool focused = false;
    };

    struct IntegerEditFrame : NumericEditFrame{
        const IntegerEditModel* model = nullptr;
    };

    struct FloatEditFrame : NumericEditFrame{
        const FloatEditModel* model = nullptr;
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
        PopupToken parentToken;
        ControlToken token;
        ControlToken listToken;
        Rect visibleField;
        u64 rowCount = 0u;
        u64 resultCount = 0u;
        u64 queryGeneration = 0u;
        u64 queryRevision = 0u;
        u64 queryCompositionGeneration = 0u;
        u64 querySelectionGeneration = 0u;
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


    struct TooltipFrame{
        WidgetState widget;
        WidgetState anchor;
        TooltipState* state = nullptr;
        TooltipOptions options;
        TextLayout text;
        u64 revision = 0u;
        u32 anchorIndex = 0u;
        u32 layer = 1u;

        explicit TooltipFrame(Core::Alloc::GlobalArena& arena)
            : text(arena)
        {}
        TooltipFrame(TooltipFrame&&) = default;
        TooltipFrame& operator=(TooltipFrame&&) = default;
        TooltipFrame(const TooltipFrame&) = delete;
        TooltipFrame& operator=(const TooltipFrame&) = delete;
    };

    struct ContextMenuFrame{
        WidgetState widget;
        WidgetState anchor;
        WidgetState popup;
        WidgetState rows;
        const IListDataSource* source = nullptr;
        ContextMenuState* state = nullptr;
        ContextMenuOptions options;
        PopupToken popupToken;
        PopupToken parentToken;
        ControlToken listToken;
        u64 rowCount = 0u;
        u64 revision = 0u;
        u32 anchorIndex = 0u;
        u32 list = s_LayoutNoParent;
        bool open = false;
    };


public:
    explicit BuilderScopeFrame(Core::Alloc::GlobalArena& arena);
    BuilderScopeFrame(BuilderScopeFrame&&) = delete;
    BuilderScopeFrame& operator=(BuilderScopeFrame&&) = delete;


private:
    void reset();


private:
    BuilderScopeFrame* m_parent = nullptr;
    PopupState* m_popupState = nullptr;
    PopupToken m_popupToken;
    bool m_popupVisible = false;
    PopupOptions m_popupOptions;
    PopupPlacement m_popupPlacement;
    LayoutTree m_layout;
    PaintVector<Item> m_items;
    PaintVector<TextAreaFrame> m_textAreas;
    PaintVector<IntegerEditFrame> m_integerEdits;
    PaintVector<FloatEditFrame> m_floatEdits;
    PaintVector<ListFrame> m_lists;
    PaintVector<Core::GlobalUniquePtr<RadioGroupFrame>> m_radioGroups;
    PaintVector<Core::GlobalUniquePtr<SliderFrame>> m_sliders;
    PaintVector<ProgressFrame> m_progress;
    PaintVector<ComboFrame> m_combos;
    PaintVector<Item> m_comboEditors;
    PaintVector<TooltipFrame> m_tooltips;
    PaintVector<ContextMenuFrame> m_contextMenus;
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

