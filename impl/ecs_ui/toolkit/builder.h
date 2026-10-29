// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "builder_scope.h"
#include "context.h"
#include "layout/tree.h"
#include "text/service.h"
#include <impl/ecs_ui/toolkit/widgets/style.adhd.h>
#include "widgets/window.h"
#include "widgets/edit_box_state.h"
#include "widgets/text_area.h"
#include "widgets/numeric_edit.h"
#include "widgets/popup.h"
#include <impl/ecs_ui/toolkit/widgets/popup_style.adhd.h>
#include "widgets/list.h"
#include <impl/ecs_ui/toolkit/widgets/list_style.adhd.h>
#include <impl/ecs_ui/toolkit/widgets/radio_group_style.adhd.h>
#include "widgets/combo.h"
#include <impl/ecs_ui/toolkit/widgets/combo_style.adhd.h>
#include "widgets/search_combo.h"
#include "widgets/tooltip.h"
#include <impl/ecs_ui/toolkit/widgets/tooltip_style.adhd.h>
#include "widgets/context_menu.h"

#include <global/simd/vector.h>


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
    using ListRow = BuilderScopeFrame::ListRow;
    using ComboFrame = BuilderScopeFrame::ComboFrame;
    using TooltipFrame = BuilderScopeFrame::TooltipFrame;
    using ContextMenuFrame = BuilderScopeFrame::ContextMenuFrame;


private:
    [[nodiscard]] static SIMDVectorSingle LoadRectangle(const Rect& rectangle)noexcept{ return VectorSet(rectangle.x, rectangle.y, rectangle.width, rectangle.height); }

    [[nodiscard]] static Rect StoreRectangle(const SIMDVectorSingle rectangle)noexcept{ return { VectorGetX(rectangle), VectorGetY(rectangle), VectorGetZ(rectangle), VectorGetW(rectangle) }; }


public:
    Builder(Core::Alloc::GlobalArena& arena, Context& context, PaintBuilder& paint, TextService& text);


public:
    [[nodiscard]] b8 beginPanel(AStringView stableKey, const Rect& bounds, LayoutDirection::Enum direction = LayoutDirection::Column);
    [[nodiscard]] b8 endPanel();
    // Returns content visibility.
    // A collapsed window still opens a scope and requires endWindow().
    // The borrowed WindowState must remain alive through that matching endWindow().
    [[nodiscard]] b8 beginWindow(AStringView stableKey, StringView title, WindowState& state, const WindowOptions& options = {});
    [[nodiscard]] b8 endWindow();
    // Popups begin after a balanced panel/window or within another popup. Only a visible begin requires endPopup().
    [[nodiscard]] b8 beginPopup(AStringView stableKey, PopupState& state, const PopupOptions& options = {});
    [[nodiscard]] b8 endPopup();
    [[nodiscard]] b8 beginRow(AStringView stableKey, const ContainerOptions& options = {});
    [[nodiscard]] b8 beginColumn(AStringView stableKey, const ContainerOptions& options = {});
    [[nodiscard]] b8 endContainer()noexcept;
    [[nodiscard]] b8 label(AStringView stableKey, StringView text, const WidgetOptions& options = {});
    // Return one action at most per declaration. Disabled controls discard stale activation and remain pointer barriers.
    [[nodiscard]] b8 button(AStringView stableKey, StringView text, const WidgetOptions& options = {});
    [[nodiscard]] b8 checkbox(AStringView stableKey, StringView text, b8& checked, const WidgetOptions& options = {});
    [[nodiscard]] b8 selectable(AStringView stableKey, StringView text, b8 selected, const WidgetOptions& options = {});
    // Borrowed through the matching endPanel/endWindow/endPopup; explicit state/source changes must wait until that scope ends.
    // Only visible rows are borrowed and shaped; stable keys must be unique and lookup/search efficient.
    [[nodiscard]] ListResult virtualList(AStringView stableKey, const IListDataSource& source, ListState& state, const ListOptions& options = {});
    // The complete bounded group borrows source/state through the enclosing scope or outermost popup end.
    [[nodiscard]] RadioGroupResult radioGroup(AStringView stableKey, const IListDataSource& source, RadioGroupState& state, const RadioGroupOptions& options = {});
    // Values and results become available after the successful enclosing scope or outermost popup end.
    [[nodiscard]] b8 slider(AStringView stableKey, SliderState& state, const SliderOptions& options = {});
    // A passive declaration copies its finite fraction and resolved skin style.
    [[nodiscard]] b8 progress(AStringView stableKey, f64 fraction, const ProgressOptions& options = {});
    // Copies an authored skin region and its sizing/tint policy; default dimensions use its natural extent.
    [[nodiscard]] b8 image(AStringView stableKey, const Name& regionName, const ImageOptions& options = {});
    // Retains an immutable engine texture source and copied options; natural size is its texel extent in logical units.
    [[nodiscard]] b8 image(AStringView stableKey, const SharedImageSource& source, const ImageOptions& options = {});
    // Image and texture-image declarations share one frame publisher.
    [[nodiscard]] b8 publishImageFrame(const WidgetState& widget, ImageFrame&& frame);
    // Fields and popups borrow state/source through the enclosing panel/window or outermost popup end.
    // Internal popup/list scopes reserve declaration order and emit after their containing layout.
    [[nodiscard]] ComboResult comboBox(AStringView stableKey, const IListDataSource& source, ComboState& state, const ComboOptions& options = {});
    [[nodiscard]] SearchComboResult searchComboBox(AStringView stableKey, ISearchableListDataSource& source, SearchComboState& state, const SearchComboOptions& options = {});
    // Attach to a preceding item in this same container. Models are borrowed until the enclosing scope ends.
    [[nodiscard]] b8 tooltip(AStringView stableKey, AStringView anchorKey, StringView text, TooltipState& state, const TooltipOptions& options = {});
    [[nodiscard]] ContextMenuResult contextMenu(AStringView stableKey, AStringView anchorKey, const IListDataSource& source, ContextMenuState& state, const ContextMenuOptions& options = {});
    [[nodiscard]] b8 separator(AStringView stableKey, const SeparatorOptions& options = {});
    [[nodiscard]] EditBoxResult editBox(AStringView stableKey, EditModel& model, EditBoxState& state, const EditBoxOptions& options = {});
    // Multiline model and viewport remain lent through the enclosing scope or outermost popup end.
    [[nodiscard]] EditBoxResult textArea(AStringView stableKey, EditModel& model, TextAreaState& state, const TextAreaOptions& options = {});
    // Numeric model and editor state remain lent through the enclosing scope or outermost user popup end.
    [[nodiscard]] NumericEditBoxResult integerEdit(AStringView stableKey, IntegerEditModel& model, EditBoxState& state, const IntegerEditOptions& options = {});
    [[nodiscard]] NumericEditBoxResult floatEdit(AStringView stableKey, FloatEditModel& model, EditBoxState& state, const FloatEditOptions& options = {});
    [[nodiscard]] b8 balanced()const noexcept{ return !declarationBlocked() && !m_scope->m_panelActive && !m_scope->m_windowActive && !m_scope->m_popupState; }
    void reset()noexcept;
    // Observe accepted input without retaining target pointers across frame publication.
    [[nodiscard]] const InputRouter& input()const noexcept{ return m_context.input(); }

    [[nodiscard]] b8 failed()const noexcept{ return m_context.failed(); }
    void setSkin(const UiSkin& skin)noexcept;
    // One explicit left-to-right script/language policy applies to every widget declaration and its deferred paint.
    [[nodiscard]] b8 setTextShaping(u32 scriptTag, StringView language);
    [[nodiscard]] WidgetStyle& style()noexcept;
    [[nodiscard]] ScrollbarStyle& scrollbarStyle()noexcept;
    [[nodiscard]] EditBoxStyle& editStyle()noexcept;
    [[nodiscard]] PopupStyle& popupStyle()noexcept;
    [[nodiscard]] ListStyle& listStyle()noexcept;
    [[nodiscard]] RadioGroupStyle& radioGroupStyle()noexcept;
    [[nodiscard]] SliderStyle& sliderStyle()noexcept;
    [[nodiscard]] ProgressStyle& progressStyle()noexcept;
    [[nodiscard]] ComboStyle& comboStyle()noexcept;
    [[nodiscard]] TooltipStyle& tooltipStyle()noexcept;
    void setEditHost(IEditBoxHost* host)noexcept;
    void setDeltaSeconds(f32 delta)noexcept;
    void setPointerBusy(b8 busy)noexcept;
    // Valid while a window scope is open, including a collapsed window.
    [[nodiscard]] const WindowMetrics& windowMetrics()const noexcept{ return m_scope->m_window.metrics; }


private:
    [[nodiscard]] b8 declarationBlocked()const noexcept{ return m_finalizing || m_declaring; }
    [[nodiscard]] ShapeRequest textShapeRequest(StringView text, f32 fontSize)const noexcept;
    [[nodiscard]] b8 beginContainer(AStringView stableKey, LayoutDirection::Enum direction, const ContainerOptions& options);
    [[nodiscard]] Expected<NotNull<Item*>> addItem(AStringView stableKey, StringView text, WidgetKind::Enum kind, const WidgetOptions& options);
    [[nodiscard]] const UiSkinRegion* region(const Name& preferred, const Name& fallback)const noexcept;
    void loadButtonMetrics(SIMDVectorSingle& size, SIMDVectorSingle& padding)const noexcept;
    [[nodiscard]] b8 paintPanel();
    [[nodiscard]] b8 paintWindow();
    [[nodiscard]] b8 paintWindowTitle();
    [[nodiscard]] b8 paintWindowResize();
    [[nodiscard]] b8 paintItems();
    [[nodiscard]] b8 paintItem(const Item& item, const LayoutBox& box);
    [[nodiscard]] EditBoxResult declareEditBox(AStringView stableKey, const EditBoxOptions& options, EditModel& model, EditBoxState& state, IEditActionSink* actions = nullptr, IntegerEditFrame* integerFrame = nullptr, FloatEditFrame* floatFrame = nullptr);
    [[nodiscard]] b8 paintTextArea(const Item& item, const LayoutBox& box);
    [[nodiscard]] b8 applyTextAreaScrollInput(const Item& item, const TextAreaFrame& frame, const ControlToken& token);
    [[nodiscard]] b8 paintTextAreaScrollbars(const Item& item, const ScrollViewportPlacement& placement, const Rect& clip, const ControlToken& token);
    [[nodiscard]] b8 textAreaAvailable(const EditModel& model, const TextAreaState& state)const noexcept;
    void snapshotTextArea(TextAreaFrame& frame)noexcept;
    [[nodiscard]] b8 textAreaMatches(const TextAreaFrame& frame)const noexcept;
    void snapshotNumericEdit(NumericEditFrame& frame, const EditModel& draft)noexcept;
    [[nodiscard]] b8 numericDraftMatches(const NumericEditFrame& frame, const EditModel& draft)const noexcept;
    template<typename EditFrameT>
    [[nodiscard]] b8 numericEditFrameMatches(const EditFrameT& frame)const noexcept{
        return frame.model && frame.model->revision() == frame.revision && numericDraftMatches(frame, frame.model->draft());
    }
    [[nodiscard]] b8 numericEditMatches(const Item& item)const noexcept;
    [[nodiscard]] b8 numericStateAvailable(const EditBoxState& state)const noexcept;
    [[nodiscard]] SIMDVectorSingle loadEditMinimumSize()const noexcept;
    [[nodiscard]] b8 prepareEditBox(Item& item, EditModel& model, EditBoxState& state, const EditBoxResult& result);
    [[nodiscard]] b8 paintEditBox(const Item& item, const LayoutBox& box, const HitTarget* navigation = nullptr);
    [[nodiscard]] b8 paintSelectable(const Item& item, const LayoutBox& box);
    [[nodiscard]] b8 prepareListMetrics(ListFrame& frame);
    [[nodiscard]] Expected<ListRow> prepareListRow(const ListFrame& frame, u64 index);
    [[nodiscard]] Expected<ScrollPlacement> measureListPlacement(ListFrame& frame, const LayoutBox& box, const Rect& clip);
    [[nodiscard]] b8 paintList(const Item& item, const LayoutBox& box);
    [[nodiscard]] b8 paintListRows(const Item& item, const ListFrame& frame, const ScrollPlacement& placement);
    [[nodiscard]] b8 paintRadioGroup(const Item& item, const LayoutBox& box);
    [[nodiscard]] b8 radioGroupStateMatches(const RadioGroupFrame& frame)const noexcept;
    [[nodiscard]] b8 radioGroupMatches(const RadioGroupFrame& frame)const;
    [[nodiscard]] b8 prepareRadioGroup(RadioGroupFrame& frame);
    [[nodiscard]] b8 prepareSlider(SliderFrame& frame)noexcept;
    [[nodiscard]] b8 paintSlider(const Item& item, const LayoutBox& box);
    [[nodiscard]] b8 paintProgress(const Item& item, const LayoutBox& box);
    [[nodiscard]] b8 paintImage(const Item& item, const LayoutBox& box);
    // Image and progress paints share one clip-pop plus hit-target tail.
    [[nodiscard]] b8 finishClipPaint(const Item& item, const Rect& bounds, const Rect& clip, b8 painted);
    [[nodiscard]] b8 applySliderInput(SliderFrame& frame, const SliderPlacement& placement, b8 interactive);
    [[nodiscard]] b8 sliderMatches(const SliderFrame& frame)const noexcept;
    void publishSliderResults(b8 valid)noexcept;
    [[nodiscard]] b8 applyListGesture(ListState& state, const PointerGesture& gesture)noexcept;
    [[nodiscard]] ComboResult declareCombo(
        AStringView stableKey, const IListDataSource& source,
        const ComboOptions& options, const SearchComboOptions* searchOptions, ComboState& state,
        SearchComboState* search = nullptr, ISearchableListDataSource* searchSource = nullptr
    );
    [[nodiscard]] b8 paintCombo(const Item& item, const LayoutBox& box);
    [[nodiscard]] b8 paintCombos();
    [[nodiscard]] b8 paintComboPopup(ComboFrame& frame);
    [[nodiscard]] b8 comboStateMatches(const ComboFrame& frame)const noexcept;
    [[nodiscard]] b8 comboMatches(const ComboFrame& frame)const;
    [[nodiscard]] b8 applyComboInput(const WidgetState& field, ComboFrame& frame, ComboResult& result);
    [[nodiscard]] b8 applyComboListInput(ComboFrame& frame, ComboResult& result);
    [[nodiscard]] b8 prepareComboSearch(const WidgetState& field, ComboFrame& frame);
    void snapshotComboQuery(ComboFrame& frame)noexcept;
    [[nodiscard]] b8 paintComboQuery(ComboFrame& frame, LayoutBox& content);
    [[nodiscard]] Item* annotationAnchor(AStringView stableKey)noexcept;
    [[nodiscard]] b8 paintDeferred();
    [[nodiscard]] b8 paintDeferredContents();
    [[nodiscard]] b8 validateDeferredSources()const;
    [[nodiscard]] b8 validateDeferredStates()const noexcept;
    void releaseDeferredLoans()noexcept;
    [[nodiscard]] b8 listStateMatches(const ListFrame& frame)const noexcept;
    [[nodiscard]] b8 listMatches(const ListFrame& frame)const;
    [[nodiscard]] b8 paintTooltips();
    [[nodiscard]] b8 prepareContextMenu(ContextMenuFrame& frame, ContextMenuResult& result);
    [[nodiscard]] b8 applyContextMenuInput(ContextMenuFrame& frame, ContextMenuResult& result);
    void snapshotContextMenu(ContextMenuFrame& frame)noexcept;
    [[nodiscard]] b8 contextMenuStateMatches(const ContextMenuFrame& frame)const noexcept;
    [[nodiscard]] b8 contextMenuMatches(const ContextMenuFrame& frame)const;
    [[nodiscard]] b8 paintContextMenus();
    [[nodiscard]] b8 paintContextMenuPopup(ContextMenuFrame& frame);
    [[nodiscard]] b8 paintPopup();
    [[nodiscard]] b8 synchronizePopup()noexcept;
    [[nodiscard]] b8 popupFrameVisible(const BuilderScopeFrame& frame)const noexcept;
    [[nodiscard]] b8 popupAncestorsVisible()const noexcept;
    [[nodiscard]] b8 finishPopupFamily();
    [[nodiscard]] b8 paintPopupFamily(BuilderScopeFrame& frame);
    [[nodiscard]] b8 validatePopupFamily();
    void releasePopupFamily()noexcept;
    [[nodiscard]] b8 reserveCompoundPopup(const WidgetState& widget, const PopupToken& token);
    [[nodiscard]] SIMDVectorSingle loadVisibleClip(SIMDVectorSingle clip)const noexcept;


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
    AString<Core::Alloc::GlobalArena> m_textLanguage;
    u32 m_textScriptTag = s_TextDefaultScriptTag;
    f32 m_skinDefaultFontSize = s_TextDefaultFontSize;
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
    b8 m_pointerBusy = false;
    b8 m_declaring = false;
    b8 m_finalizing = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

