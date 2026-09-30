// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "builder_scope.h"
#include "context.h"
#include "layout/tree.h"
#include "text/service.h"
#include "widgets/style.h"
#include "widgets/window.h"
#include "widgets/edit_box_state.h"
#include "widgets/text_area.h"
#include "widgets/numeric_edit.h"
#include "widgets/popup.h"
#include "widgets/popup_style.h"
#include "widgets/list.h"
#include "widgets/list_style.h"
#include "widgets/radio_group_style.h"
#include "widgets/combo.h"
#include "widgets/combo_style.h"
#include "widgets/search_combo.h"
#include "widgets/tooltip.h"
#include "widgets/tooltip_style.h"
#include "widgets/context_menu.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Transient declarations shape text, arrange a panel, then freeze paint and hit targets in the same order.
class Builder final : NoCopy{
private:
    using Item = BuilderScopeFrame::Item;
    using TextAreaFrame = BuilderScopeFrame::TextAreaFrame;
    using NumericEditFrame = BuilderScopeFrame::NumericEditFrame;
    using IntegerEditFrame = BuilderScopeFrame::IntegerEditFrame;
    using FloatEditFrame = BuilderScopeFrame::FloatEditFrame;
    using ListFrame = BuilderScopeFrame::ListFrame;
    using ComboFrame = BuilderScopeFrame::ComboFrame;
    using TooltipFrame = BuilderScopeFrame::TooltipFrame;
    using ContextMenuFrame = BuilderScopeFrame::ContextMenuFrame;


public:
    Builder(Core::Alloc::GlobalArena& arena, Context& context, PaintBuilder& paint, TextService& text);


public:
    [[nodiscard]] bool beginPanel(AStringView stableKey, const Rect& bounds, LayoutDirection::Enum direction = LayoutDirection::Column);
    [[nodiscard]] bool endPanel();
    // Returns content visibility. A collapsed window still opens a scope and requires endWindow().
    // The borrowed WindowState must remain alive through that matching endWindow().
    [[nodiscard]] bool beginWindow(AStringView stableKey, StringView title, WindowState& state, const WindowOptions& options = {});
    [[nodiscard]] bool endWindow();
    // Popups begin after a balanced panel/window or within another popup. Only a visible begin requires endPopup().
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
    // The complete bounded group borrows source/state through the enclosing scope or outermost popup end.
    [[nodiscard]] RadioGroupResult radioGroup(AStringView stableKey, const IListDataSource& source,
        RadioGroupState& state, const RadioGroupOptions& options = {});
    // Values and results become available after the successful enclosing scope or outermost popup end.
    [[nodiscard]] bool slider(AStringView stableKey, SliderState& state, const SliderOptions& options = {});
    // A passive declaration copies its finite fraction and resolved skin style.
    [[nodiscard]] bool progress(AStringView stableKey, f64 fraction, const ProgressOptions& options = {});
    // Copies an authored skin region and its sizing/tint policy; default dimensions use its natural extent.
    [[nodiscard]] bool image(AStringView stableKey, const Name& regionName, const ImageOptions& options = {});
    // Retains an immutable engine texture source and copied options; natural size is its texel extent in logical units.
    [[nodiscard]] bool image(AStringView stableKey, const SharedImageSource& source, const ImageOptions& options = {});
    // Fields and popups borrow state/source through the enclosing panel/window or outermost popup end.
    // Internal popup/list scopes reserve declaration order and emit after their containing layout.
    [[nodiscard]] ComboResult comboBox(AStringView stableKey, const IListDataSource& source, ComboState& state, const ComboOptions& options = {});
    [[nodiscard]] SearchComboResult searchComboBox(AStringView stableKey, ISearchableListDataSource& source,
        SearchComboState& state, const SearchComboOptions& options = {});
    // Attach to a preceding item in this same container. Models are borrowed until the enclosing scope ends.
    [[nodiscard]] bool tooltip(AStringView stableKey, AStringView anchorKey, StringView text,
        TooltipState& state, const TooltipOptions& options = {});
    [[nodiscard]] ContextMenuResult contextMenu(AStringView stableKey, AStringView anchorKey, const IListDataSource& source,
        ContextMenuState& state, const ContextMenuOptions& options = {});
    [[nodiscard]] bool separator(AStringView stableKey, const SeparatorOptions& options = {});
    [[nodiscard]] EditBoxResult editBox(AStringView stableKey, EditModel& model, EditBoxState& state, const EditBoxOptions& options = {});
    // Multiline model and viewport remain lent through the enclosing scope or outermost popup end.
    [[nodiscard]] EditBoxResult textArea(AStringView stableKey, EditModel& model, TextAreaState& state, const TextAreaOptions& options = {});
    // Numeric model and editor state remain lent through the enclosing scope or outermost user popup end.
    [[nodiscard]] NumericEditBoxResult integerEdit(AStringView stableKey, IntegerEditModel& model,
        EditBoxState& state, const IntegerEditOptions& options = {});
    [[nodiscard]] NumericEditBoxResult floatEdit(AStringView stableKey, FloatEditModel& model,
        EditBoxState& state, const FloatEditOptions& options = {});
    [[nodiscard]] bool balanced()const{ return !declarationBlocked() && !m_scope->m_panelActive && !m_scope->m_windowActive && !m_scope->m_popupState; }
    void reset();
    // Observe accepted input without retaining target pointers across frame publication.
    [[nodiscard]] const InputRouter& input()const{ return m_context.input(); }
    [[nodiscard]] bool failed()const{ return m_context.failed(); }
    void setSkin(const UiSkin& skin);
    [[nodiscard]] WidgetStyle& style(){ if(declarationBlocked()) m_context.fail(); return m_style; }
    [[nodiscard]] ScrollbarStyle& scrollbarStyle(){ if(declarationBlocked()) m_context.fail(); return m_scrollbarStyle; }
    [[nodiscard]] EditBoxStyle& editStyle(){ if(declarationBlocked()) m_context.fail(); return m_editStyle; }
    [[nodiscard]] PopupStyle& popupStyle(){ if(declarationBlocked()) m_context.fail(); return m_popupStyle; }
    [[nodiscard]] ListStyle& listStyle(){ if(declarationBlocked()) m_context.fail(); return m_listStyle; }
    [[nodiscard]] RadioGroupStyle& radioGroupStyle(){ if(declarationBlocked()) m_context.fail(); return m_radioGroupStyle; }
    [[nodiscard]] SliderStyle& sliderStyle(){ if(declarationBlocked()) m_context.fail(); return m_sliderStyle; }
    [[nodiscard]] ProgressStyle& progressStyle(){ if(declarationBlocked()) m_context.fail(); return m_progressStyle; }
    [[nodiscard]] ComboStyle& comboStyle(){ if(declarationBlocked()) m_context.fail(); return m_comboStyle; }
    [[nodiscard]] TooltipStyle& tooltipStyle(){ if(declarationBlocked()) m_context.fail(); return m_tooltipStyle; }
    void setEditHost(IEditBoxHost* host){ if(declarationBlocked()) m_context.fail(); else m_editHost = host; }
    void setDeltaSeconds(f32 delta){ if(declarationBlocked()) m_context.fail(); else m_deltaSeconds = delta; }
    void setPointerBusy(bool busy){ if(declarationBlocked()) m_context.fail(); else m_pointerBusy = busy; }
    // Valid while a window scope is open, including a collapsed window.
    [[nodiscard]] const WindowMetrics& windowMetrics()const{ return m_scope->m_window.metrics; }


private:
    [[nodiscard]] bool declarationBlocked()const{ return m_finalizing || m_declaring; }
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
    [[nodiscard]] EditBoxResult declareEditBox(AStringView stableKey, EditModel& model, EditBoxState& state,
        const EditBoxOptions& options, IEditActionSink* actions = nullptr, IntegerEditFrame* integerFrame = nullptr,
        FloatEditFrame* floatFrame = nullptr);
    [[nodiscard]] bool paintTextArea(const Item& item, const LayoutBox& box);
    [[nodiscard]] bool applyTextAreaScrollInput(const Item& item, const TextAreaFrame& frame, const ControlToken& token);
    [[nodiscard]] bool paintTextAreaScrollbars(const Item& item, const ScrollViewportPlacement& placement, const Rect& clip, const ControlToken& token);
    [[nodiscard]] bool textAreaAvailable(const EditModel& model, const TextAreaState& state)const;
    void snapshotTextArea(TextAreaFrame& frame);
    [[nodiscard]] bool textAreaMatches(const TextAreaFrame& frame)const;
    void snapshotNumericEdit(NumericEditFrame& frame, const EditModel& draft);
    [[nodiscard]] bool numericDraftMatches(const NumericEditFrame& frame, const EditModel& draft)const;
    [[nodiscard]] bool integerEditMatches(const IntegerEditFrame& frame)const;
    [[nodiscard]] bool floatEditMatches(const FloatEditFrame& frame)const;
    [[nodiscard]] bool numericEditMatches(const Item& item)const;
    [[nodiscard]] bool numericStateAvailable(const EditBoxState& state)const;
    [[nodiscard]] bool prepareEditBox(Item& item, EditModel& model, EditBoxState& state, const EditBoxResult& result);
    [[nodiscard]] bool paintEditBox(const Item& item, const LayoutBox& box, const HitTarget* navigation = nullptr);
    [[nodiscard]] bool paintSelectable(const Item& item, const LayoutBox& box);
    [[nodiscard]] bool paintList(const Item& item, const LayoutBox& box);
    [[nodiscard]] bool paintListRows(const Item& item, const ListFrame& frame, const ScrollPlacement& placement);
    [[nodiscard]] bool paintRadioGroup(const Item& item, const LayoutBox& box);
    [[nodiscard]] bool radioGroupStateMatches(const RadioGroupFrame& frame)const;
    [[nodiscard]] bool radioGroupMatches(const RadioGroupFrame& frame)const;
    [[nodiscard]] bool prepareRadioGroup(RadioGroupFrame& frame);
    [[nodiscard]] bool prepareSlider(SliderFrame& frame);
    [[nodiscard]] bool paintSlider(const Item& item, const LayoutBox& box);
    [[nodiscard]] bool paintProgress(const Item& item, const LayoutBox& box);
    [[nodiscard]] bool paintImage(const Item& item, const LayoutBox& box);
    [[nodiscard]] bool applySliderInput(SliderFrame& frame, const SliderPlacement& placement, bool interactive);
    [[nodiscard]] bool sliderMatches(const SliderFrame& frame)const;
    void publishSliderResults(bool valid);
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
    [[nodiscard]] Item* annotationAnchor(AStringView stableKey);
    [[nodiscard]] bool paintDeferred();
    [[nodiscard]] bool paintDeferredContents();
    [[nodiscard]] bool validateDeferredSources()const;
    [[nodiscard]] bool validateDeferredStates()const;
    void releaseDeferredLoans();
    [[nodiscard]] bool listStateMatches(const ListFrame& frame)const;
    [[nodiscard]] bool listMatches(const ListFrame& frame)const;
    [[nodiscard]] bool paintTooltips();
    [[nodiscard]] bool prepareContextMenu(ContextMenuFrame& frame, ContextMenuResult& result);
    [[nodiscard]] bool applyContextMenuInput(ContextMenuFrame& frame, ContextMenuResult& result);
    void snapshotContextMenu(ContextMenuFrame& frame);
    [[nodiscard]] bool contextMenuStateMatches(const ContextMenuFrame& frame)const;
    [[nodiscard]] bool contextMenuMatches(const ContextMenuFrame& frame)const;
    [[nodiscard]] bool paintContextMenus();
    [[nodiscard]] bool paintContextMenuPopup(ContextMenuFrame& frame);
    [[nodiscard]] bool paintPopup();
    [[nodiscard]] bool synchronizePopup();
    [[nodiscard]] bool popupFrameVisible(const BuilderScopeFrame& frame)const;
    [[nodiscard]] bool popupAncestorsVisible()const;
    [[nodiscard]] bool finishPopupFamily();
    [[nodiscard]] bool paintPopupFamily(BuilderScopeFrame& frame);
    [[nodiscard]] bool validatePopupFamily();
    void releasePopupFamily();
    [[nodiscard]] bool reserveCompoundPopup(const WidgetState& widget, const PopupToken& token);
    [[nodiscard]] Rect visibleClip(const Rect& clip)const;


private:
    Core::Alloc::GlobalArena& m_arena;
    Context& m_context;
    PaintBuilder& m_paint;
    TextService& m_text;
    BuilderScopeFrame m_scopeFrame;
    NotNull<BuilderScopeFrame*> m_scope;
    PaintVector<Core::GlobalUniquePtr<BuilderScopeFrame>> m_popupFrames;
    usize m_popupFrameCount = 0u;
    const UiSkin* m_skin = nullptr;
    WidgetStyle m_style;
    f32 m_skinDefaultFontSize = 16.0f;
    EditBoxStyle m_editStyle;
    ScrollbarStyle m_scrollbarStyle;
    PopupStyle m_popupStyle;
    ListStyle m_listStyle;
    RadioGroupStyle m_radioGroupStyle;
    SliderStyle m_sliderStyle;
    ProgressStyle m_progressStyle;
    ComboStyle m_comboStyle;
    TooltipStyle m_tooltipStyle;
    IEditBoxHost* m_editHost = nullptr;
    f32 m_deltaSeconds = 0.0f;
    bool m_pointerBusy = false;
    bool m_declaring = false;
    bool m_finalizing = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

