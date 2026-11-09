// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "builder.h"
#include <impl/ecs_ui/toolkit/widgets/ui_scope_descriptor_factory.adhd.h>
#include <impl/ecs_ui/toolkit/widgets/control_style_metrics.adhd.h>
#include <impl/ecs_ui/toolkit/widgets/hit_target_factory.adhd.h>
#include <impl/ecs_ui/toolkit/widgets/control_visual_painter.adhd.h>
#include <global/simd/vector_arithmetic.h>
#include <global/simplemath.h>
#include <global/scope_exit.h>
#include <global/bit.h>
#include <impl/ecs_ui/toolkit/widgets/list_input.h>
#include <impl/ecs_ui/toolkit/edit/text_normalization.adhd.h>
#include <impl/ecs_ui/toolkit/edit/types.adhd.h>
#include <impl/ecs_ui/toolkit/widgets/list_layout_preparer.h>
#include <impl/ecs_ui/toolkit/edit/numeric_types.adhd.h>
#include <impl/ecs_ui/toolkit/widgets/text_area_navigation.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Builder;


class ComboCoordinator final{
private:
    using Item = BuilderScopeFrame::Item;
    using ListFrame = BuilderScopeFrame::ListFrame;
    using ComboFrame = BuilderScopeFrame::ComboFrame;


public:
    explicit ComboCoordinator(Builder& builder)noexcept
        : m_builder(builder)
    {}


public:
    [[nodiscard]] ComboResult declareCombo(AStringView stableKey, const IListDataSource& source, const ComboOptions& options, const SearchComboOptions* searchOptions, ComboState& state, SearchComboState* search, ISearchableListDataSource* searchSource);
    [[nodiscard]] SearchComboResult searchComboBox(AStringView stableKey, ISearchableListDataSource& source, SearchComboState& state, const SearchComboOptions& options);
    [[nodiscard]] b8 paintCombo(const Item& item, const LayoutBox& box);
    [[nodiscard]] b8 paintCombos();


private:
    [[nodiscard]] b8 paintComboPopup(ComboFrame& frame);
    [[nodiscard]] b8 applyComboInput(const WidgetState& field, ComboFrame& frame, ComboResult& result);
    [[nodiscard]] b8 applyComboListInput(ComboFrame& frame, ComboResult& result);
    [[nodiscard]] b8 prepareComboSearch(const WidgetState& field, ComboFrame& frame);
    void snapshotComboQuery(ComboFrame& frame)noexcept;
    [[nodiscard]] b8 paintComboQuery(ComboFrame& frame, LayoutBox& content);


private:
    Builder& m_builder;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Builder;


class ContextMenuCoordinator final{
private:
    using Item = BuilderScopeFrame::Item;
    using ListFrame = BuilderScopeFrame::ListFrame;
    using ContextMenuFrame = BuilderScopeFrame::ContextMenuFrame;


public:
    explicit ContextMenuCoordinator(Builder& builder)noexcept
        : m_builder(builder)
    {}


public:
    [[nodiscard]] ContextMenuResult declareMenu(AStringView stableKey, AStringView anchorKey, const IListDataSource& source, ContextMenuState& state, const ContextMenuOptions& options);
    [[nodiscard]] b8 paintMenus();


private:
    [[nodiscard]] b8 prepareContextMenu(ContextMenuFrame& frame, ContextMenuResult& result);
    [[nodiscard]] b8 applyContextMenuInput(ContextMenuFrame& frame, ContextMenuResult& result);
    void snapshotContextMenu(ContextMenuFrame& frame)noexcept;
    [[nodiscard]] b8 paintContextMenuPopup(ContextMenuFrame& frame);


private:
    Builder& m_builder;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Builder;


class EditBoxCoordinator final{
private:
    using Item = BuilderScopeFrame::Item;
    using IntegerEditFrame = BuilderScopeFrame::IntegerEditFrame;
    using FloatEditFrame = BuilderScopeFrame::FloatEditFrame;


public:
    explicit EditBoxCoordinator(Builder& builder)noexcept
        : m_builder(builder)
    {}


public:
    [[nodiscard]] EditBoxResult declareEditBox(AStringView stableKey, const EditBoxOptions& options, EditModel& model, EditBoxState& state, IEditActionSink* actions, IntegerEditFrame* integerFrame, FloatEditFrame* floatFrame);
    [[nodiscard]] b8 prepareEditBox(const EditBoxResult& result, EditModel& model, EditBoxState& state, Item& item);
    [[nodiscard]] b8 paintEditBox(const Item& item, const LayoutBox& box, const HitTarget* navigation);


private:
    Builder& m_builder;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Builder;


class ListCoordinator final{
private:
    using Item = BuilderScopeFrame::Item;
    using ListFrame = BuilderScopeFrame::ListFrame;
    using ListRow = BuilderScopeFrame::ListRow;


public:
    explicit ListCoordinator(Builder& builder)noexcept
        : m_builder(builder)
    {}


public:
    [[nodiscard]] ListResult virtualList(AStringView stableKey, const IListDataSource& source, ListState& state, const ListOptions& options);
    [[nodiscard]] const UiSkinRegion* prepareListFrame(const IListDataSource& source, ListState& state, const ListOptions& options, const ControlToken& token, u64 rowCount, b8 focusOnCommit, WidgetId commandFocus, ListFrame& outFrame);
    [[nodiscard]] b8 paintList(const Item& item, const LayoutBox& box);


private:
    [[nodiscard]] b8 prepareListMetrics(ListFrame& frame);
    [[nodiscard]] b8 paintListRows(const Item& item, const ListFrame& frame, const ScrollPlacement& placement);


private:
    Builder& m_builder;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Builder;


class NumericEditCoordinator final{
private:
    using IntegerEditFrame = BuilderScopeFrame::IntegerEditFrame;
    using FloatEditFrame = BuilderScopeFrame::FloatEditFrame;


public:
    explicit NumericEditCoordinator(Builder& builder)noexcept
        : m_builder(builder)
    {}


public:
    [[nodiscard]] NumericEditBoxResult integerEdit(AStringView stableKey, IntegerEditModel& model, EditBoxState& state, const IntegerEditOptions& options);
    [[nodiscard]] NumericEditBoxResult floatEdit(AStringView stableKey, FloatEditModel& model, EditBoxState& state, const FloatEditOptions& options);


private:
    template<typename FrameT, typename ModelT, typename OptionsT, typename ValidateBoundsF, typename FrameListT>
    [[nodiscard]] NumericEditBoxResult declareNumericEdit(AStringView stableKey, ModelT& model, EditBoxState& state, const OptionsT& options, ValidateBoundsF validateBounds, FrameListT& frames);


private:
    Builder& m_builder;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Builder;


class TextAreaCoordinator final{
private:
    using Item = BuilderScopeFrame::Item;
    using TextAreaFrame = BuilderScopeFrame::TextAreaFrame;


public:
    explicit TextAreaCoordinator(Builder& builder)noexcept
        : m_builder(builder)
    {}


public:
    [[nodiscard]] EditBoxResult declareTextArea(AStringView stableKey, EditModel& model, TextAreaState& state, const TextAreaOptions& options);
    [[nodiscard]] b8 paintTextArea(const Item& item, const LayoutBox& box);


private:
    [[nodiscard]] b8 textAreaAvailable(const EditModel& model, const TextAreaState& state)const noexcept;
    void snapshotTextArea(TextAreaFrame& frame)noexcept;
    [[nodiscard]] b8 applyTextAreaScrollInput(const Item& item, const TextAreaFrame& frame, const ControlToken& token);
    [[nodiscard]] b8 paintTextAreaScrollbars(const Item& item, const ScrollViewportPlacement& placement, const Rect& clip, const ControlToken& token);


private:
    Builder& m_builder;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Builder;


class TooltipCoordinator final{
private:
    using Item = BuilderScopeFrame::Item;
    using TooltipFrame = BuilderScopeFrame::TooltipFrame;


public:
    explicit TooltipCoordinator(Builder& builder)noexcept
        : m_builder(builder)
    {}


public:
    [[nodiscard]] b8 declareTooltip(AStringView stableKey, AStringView anchorKey, StringView text, TooltipState& state, const TooltipOptions& options);
    [[nodiscard]] b8 paintTooltips();


private:
    Builder& m_builder;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Builder::Builder(Core::Alloc::GlobalArena& arena, Context& context, PaintBuilder& paint, TextService& text)
    : m_arena(arena)
    , m_context(context)
    , m_paint(paint)
    , m_text(text)
    , m_scopeFrame(arena)
    , m_scope(MakeNotNull(&m_scopeFrame))
    , m_popupFrames(arena)
    , m_textLanguage(s_TextDefaultLanguage, arena)
{
    m_popupFrames.reserve(s_MaxNestedPopupFrames);
    for(usize frameIndex = 0u; frameIndex < s_MaxNestedPopupFrames; ++frameIndex)
        m_popupFrames.push_back(Core::MakeGlobalUnique<BuilderScopeFrame>(m_arena, m_arena));
}

b8 Builder::beginPanel(const AStringView stableKey, const Rect& bounds, const LayoutDirection::Enum direction){
    if(declarationBlocked() || m_scope->m_panelActive || m_scope->m_windowActive || !m_skin || (direction != LayoutDirection::Row && direction != LayoutDirection::Column)){
        m_context.fail();
        return false;
    }
    const UiSkinRegion* panel = region(m_style.panel, m_style.panel);
    const auto state = m_context.declare(stableKey, WidgetKind::Panel);
    if(!panel || !state){
        m_context.fail();
        return false;
    }
    reset();
    m_scope->m_panelStyle = m_style;
    m_scope->m_panelState = **state;
    m_scope->m_bounds = bounds;
    const LayoutNodeDesc description = UiScopeDescriptorFactory().panelRoot(bounds, direction, panel->padding, m_style.gap);
    u32 node = 0u;
    const auto admittedNode = m_scope->m_layout.addNode(s_LayoutNoParent, description);
    if(!admittedNode || !m_context.pushScope(stableKey)){
        m_context.fail();
        return false;
    }
    node = *admittedNode;
    m_scope->m_stack.push_back(node);
    m_scope->m_panelActive = true;
    return true;
}

b8 Builder::endPanel(){
    if(declarationBlocked() || !m_scope->m_panelActive || m_scope->m_windowActive || m_scope->m_popupState || m_scope->m_stack.size() != 1u || m_context.failed() || !m_scope->m_layout.arrange(m_scope->m_bounds)){
        m_context.fail();
        return false;
    }
    m_finalizing = true;
    ScopeExit finish([this]()noexcept{ m_finalizing = false; });
    const b8 painted = paintPanel();
    const b8 popped = m_context.popScope();
    m_scope->m_panelActive = false;
    m_scope->m_stack.clear();
    const b8 combosPainted = painted && popped && paintDeferred();
    publishSliderResults(combosPainted);
    releaseDeferredLoans();
    if(!combosPainted)
        m_context.fail();
    return combosPainted;
}

b8 Builder::endContainer()noexcept{
    if(declarationBlocked() || !m_scope->m_panelActive || m_scope->m_stack.size() <= 1u){
        m_context.fail();
        return false;
    }
    m_scope->m_stack.pop_back();
    return m_context.popScope();
}

b8 Builder::button(const AStringView stableKey, const StringView text, const WidgetOptions& options){
    const auto item = addItem(stableKey, text, WidgetKind::Button, options);
    return item && m_context.takeActivation((*item)->state, (*item)->enabled);
}

b8 Builder::checkbox(const AStringView stableKey, const StringView text, b8& checked, const WidgetOptions& options){
    const auto item = addItem(stableKey, text, WidgetKind::Checkbox, options);
    if(!item)
        return false;
    const b8 changed = m_context.takeActivation((*item)->state, (*item)->enabled);
    if(changed)
        checked = !checked;
    (*item)->checked = checked;
    return changed;
}

b8 Builder::selectable(const AStringView stableKey, const StringView text, const b8 selected, const WidgetOptions& options){
    const auto item = addItem(stableKey, text, WidgetKind::Selectable, options);
    if(!item)
        return false;
    (*item)->selectableStyle = m_listStyle.row;
    (*item)->checked = selected;
    return m_context.takeActivation((*item)->state, (*item)->enabled);
}

void Builder::reset()noexcept{
    if(declarationBlocked()){
        m_context.fail();
        return;
    }
    if(m_scope->m_panelActive || m_scope->m_windowActive)
        m_context.fail();
    m_scope = MakeNotNull(&m_scopeFrame);
    m_scopeFrame.reset();
    for(auto& frame : m_popupFrames)
        frame->reset();
    m_popupFrameCount = 0u;
}

b8 Builder::separator(const AStringView stableKey, const SeparatorOptions& options){
    if(itemDeclarationBlocked() || options.direction > SeparatorDirection::Vertical || !IsFinite(options.thickness) || options.thickness < 0.0f){
        m_context.fail();
        return false;
    }
    const UiSkinRegion* skinRegion = region(m_style.separator, m_style.separator);
    const auto state = m_context.declare(stableKey, WidgetKind::Separator);
    if(!skinRegion || !state){
        m_context.fail();
        return false;
    }
    Item item(m_arena);
    item.style = m_style;
    item.state = **state;
    const LayoutNodeDesc description = ResolveSeparatorNodeDesc(*skinRegion, options);
    if(!addItemNode(description, item))
        return false;
    m_scope->m_items.push_back(Move(item));
    return true;
}

void Builder::setSkin(const UiSkin& skin)noexcept{
    if(!balanced() || m_context.failed()){
        m_context.fail();
        return;
    }
    m_skin = &skin;
    ApplyBuilderSkinStyles(
        skin.palette(),
        skin.typography().defaultFontSize,
        m_skinDefaultFontSize,
        m_style,
        m_tooltipStyle,
        m_editStyle,
        m_scrollbarStyle,
        m_popupStyle,
        m_radioGroupStyle,
        m_sliderStyle,
        m_progressStyle
    );
}

b8 Builder::setTextShaping(const u32 scriptTag, const StringView language){
    if(!balanced() || m_context.failed()){
        m_context.fail();
        return false;
    }
    const ShapeRequest request{ {}, language, s_TextDefaultFontSize, scriptTag, TextDirection::LeftToRight };
    if(ValidateTextRequest(request, false) != TextLayoutStatus::Success)
        return false;
    m_textLanguage.assign(language.data(), language.size());
    m_textScriptTag = scriptTag;
    return true;
}

b8 Builder::beginContainer(const AStringView stableKey, const LayoutDirection::Enum direction, const ContainerOptions& options){
    if(panelDeclarationBlocked() || m_scope->m_stack.size() >= s_ContextMaxScopeDepth || !m_context.declare(stableKey, WidgetKind::Container)){
        m_context.fail();
        return false;
    }
    const LayoutNodeDesc description = UiScopeDescriptorFactory().container(direction, options);
    u32 node = 0u;
    const auto admittedNode = m_scope->m_layout.addNode(m_scope->m_stack.back(), description);
    if(!admittedNode || !m_context.pushScope(stableKey)){
        m_context.fail();
        return false;
    }
    node = *admittedNode;
    m_scope->m_stack.push_back(node);
    return true;
}

Expected<NotNull<Builder::Item*>> Builder::addItem(const AStringView stableKey, const StringView text, const WidgetKind::Enum kind, const WidgetOptions& options){
    if(itemDeclarationBlocked()){
        m_context.fail();
        return MakeUnexpected(Failure{});
    }
    const auto state = m_context.declare(stableKey, kind);
    if(!state)
        return MakeUnexpected(Failure{});
    Item item(m_arena);
    item.style = m_style;
    item.state = **state;
    item.enabled = options.enabled && synchronizePopup();
    const ShapeRequest request = textShapeRequest(text, m_style.fontSize);
    auto layout = m_text.layout(request);
    if(!layout){
        m_context.fail();
        return MakeUnexpected(Failure{});
    }
    item.text = Move(*layout);
    const auto textBounds = m_text.measure(item.text, m_paint.displayMetrics());
    if(!textBounds){
        m_context.fail();
        return MakeUnexpected(Failure{});
    }
    item.textBounds = *textBounds;
    const Point textSize{ item.textBounds.width, item.textBounds.height };
    Point intrinsicSize = textSize;
    if(kind == WidgetKind::Selectable){
        const auto metrics = MeasureSelectable(*m_skin, m_listStyle.row);
        if(!metrics){
            m_context.fail();
            return MakeUnexpected(Failure{});
        }
        item.padding = metrics->padding;
        intrinsicSize = ResolvePaddedIntrinsicSize(textSize, item.padding, metrics->minimum);
    }
    else if(kind == WidgetKind::Button){
        if(!region(m_style.button, m_style.button)){
            m_context.fail();
            return MakeUnexpected(Failure{});
        }
        Point minimum{};
        loadButtonMetrics(minimum, item.padding);
        intrinsicSize = ResolvePaddedIntrinsicSize(textSize, item.padding, minimum);
    }
    else if(kind == WidgetKind::Checkbox){
        const auto checkboxExtent = ControlStyleMetrics().checkboxExtent(*m_skin, m_style);
        if(!checkboxExtent){
            m_context.fail();
            return MakeUnexpected(Failure{});
        }
        intrinsicSize = ResolveCheckboxIntrinsicSize(textSize, *checkboxExtent, m_style.gap);
        item.checkboxExtent = *checkboxExtent;
    }
    LayoutNodeDesc description;
    description.width = options.width;
    description.height = options.height;
    description.intrinsicSize = intrinsicSize;
    if(!addItemNode(description, item))
        return MakeUnexpected(Failure{});
    m_scope->m_items.push_back(Move(item));
    return MakeNotNull(&m_scope->m_items.back());
}

b8 Builder::addItemNode(const LayoutNodeDesc& description, Item& item){
    const auto admittedNode = m_scope->m_layout.addNode(m_scope->m_stack.back(), description);
    if(!admittedNode){
        m_context.fail();
        return false;
    }
    item.node = *admittedNode;
    return true;
}

const UiSkinRegion* Builder::region(const Name& preferred, const Name& fallback)const noexcept{
    if(!m_skin)
        return nullptr;
    const UiSkinRegion* found = m_skin->findRegion(preferred);
    return found ? found : m_skin->findRegion(fallback);
}

Insets Builder::resolveRegionsPadding(const Insets& initial, const Span<const Name> names, const Name& fallback)const noexcept{
    if(!m_skin)
        return initial;
    return ControlStyleMetrics().skinRegionsMaxPadding(*m_skin, initial, names, fallback);
}

void Builder::loadButtonMetrics(Point& size, Insets& padding)const noexcept{
    if(!m_skin)
        return;
    ControlStyleMetrics().buttonMetrics(*m_skin, m_style, size, padding);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Builder::Item* Builder::annotationAnchor(const AStringView stableKey)noexcept{
    const auto id = TryMakeWidgetId(m_context.scopeId(), stableKey);
    if(!id)
        return nullptr;
    for(auto& item : m_scope->m_items){
        if(item.state.id == *id && item.state.kind != WidgetKind::Separator)
            return &item;
    }
    return nullptr;
}

b8 Builder::validateDeferredSources()const{
    // Every loan remains live through all later source callbacks, including those in an ended child scope.
    for(const auto& frame : m_scope->m_radioGroups){
        if(!radioGroupMatches(frame))
            return false;
    }
    for(const auto& frame : m_scope->m_lists){
        if(!listMatches(frame))
            return false;
    }
    for(const auto& frame : m_scope->m_combos){
        if(!comboMatches(frame))
            return false;
    }
    for(const auto& frame : m_scope->m_contextMenus){
        if(!contextMenuMatches(frame))
            return false;
    }
    return true;
}

b8 Builder::validateDeferredStates()const noexcept{
    // No source callbacks run after these model checks until every scope's loans have been released.
    for(const auto& frame : m_scope->m_sliders){
        if(!sliderMatches(frame))
            return false;
    }
    for(const auto& frame : m_scope->m_textAreas){
        if(!textAreaMatches(frame))
            return false;
    }
    for(const auto& frame : m_scope->m_integerEdits){
        if(!numericEditFrameMatches(frame))
            return false;
    }
    for(const auto& frame : m_scope->m_floatEdits){
        if(!numericEditFrameMatches(frame))
            return false;
    }
    for(const auto& frame : m_scope->m_radioGroups){
        if(!radioGroupStateMatches(frame))
            return false;
    }
    for(const auto& frame : m_scope->m_lists){
        if(!listStateMatches(frame))
            return false;
    }
    for(const auto& frame : m_scope->m_combos){
        if(!comboStateMatches(frame))
            return false;
    }
    for(const auto& frame : m_scope->m_contextMenus){
        if(!contextMenuStateMatches(frame))
            return false;
    }
    for(const auto& frame : m_scope->m_tooltips){
        if(!frame.state || frame.state->revision() != frame.revision)
            return false;
    }
    return true;
}

void Builder::releaseDeferredLoans()noexcept{
    // Frozen geometry and input targets retain copied tokens and keys, never source or model loans.
    m_scope->m_radioGroups.clear();
    m_scope->m_sliders.clear();
    const auto clearEditLoanFrames = [this](auto& frames)noexcept{
        for(auto& frame : frames){
            frame.model = nullptr;
            frame.state = nullptr;
            if(frame.item < m_scope->m_items.size())
                m_scope->m_items[frame.item].editState = nullptr;
        }
    };
    clearEditLoanFrames(m_scope->m_textAreas);
    clearEditLoanFrames(m_scope->m_integerEdits);
    clearEditLoanFrames(m_scope->m_floatEdits);
    for(auto& frame : m_scope->m_lists){
        frame.source = nullptr;
        frame.state = nullptr;
    }
    for(auto& frame : m_scope->m_combos){
        frame.source = nullptr;
        frame.results = nullptr;
        frame.searchSource = nullptr;
        frame.search = nullptr;
        frame.state = nullptr;
        if(frame.editor != s_LayoutNoParent)
            m_scope->m_comboEditors[frame.editor].editState = nullptr;
    }
    for(auto& frame : m_scope->m_contextMenus){
        frame.source = nullptr;
        frame.state = nullptr;
    }
    for(auto& frame : m_scope->m_tooltips)
        frame.state = nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 Builder::image(const AStringView stableKey, const Name& regionName, const ImageOptions& options){
    if(itemDeclarationBlocked() || !m_skin){
        m_context.fail();
        return false;
    }
    if(!synchronizePopup())
        return true;
    m_declaring = true;
    ScopeExit finish([this]()noexcept{ m_declaring = false; });
    const auto widget = m_context.declare(stableKey, WidgetKind::Image);
    ImageFrame frame;
    frame.options = options;
    if(!widget || !ControlStyleMetrics().imageMetrics(*m_skin, regionName, options, frame.metrics, frame.region)){
        m_context.fail();
        return false;
    }
    return publishImageFrame(**widget, Move(frame));
}

b8 Builder::image(const AStringView stableKey, const SharedImageSource& source, const ImageOptions& options){
    if(itemDeclarationBlocked() || !m_skin){
        m_context.fail();
        return false;
    }
    if(!synchronizePopup())
        return true;
    m_declaring = true;
    ScopeExit finish([this]()noexcept{ m_declaring = false; });
    const auto widget = m_context.declare(stableKey, WidgetKind::Image);
    ImageFrame frame;
    frame.source = source;
    frame.options = options;
    if(!widget || !frame.source){
        m_context.fail();
        return false;
    }
    const auto metrics = MeasureImage(frame.options, *frame.source);
    if(!metrics){
        m_context.fail();
        return false;
    }
    frame.metrics = *metrics;
    return publishImageFrame(**widget, Move(frame));
}

b8 Builder::publishImageFrame(const WidgetState& widget, ImageFrame&& frame){
    Item item(m_arena);
    item.state = widget;
    item.image = static_cast<u32>(m_scope->m_images.size());
    LayoutNodeDesc description;
    description.width = frame.options.width;
    description.height = frame.options.height;
    description.intrinsicSize = frame.metrics.contentSize;
    if(!addItemNode(description, item))
        return false;
    m_scope->m_images.push_back(Move(frame));
    m_scope->m_items.push_back(Move(item));
    return true;
}

b8 Builder::paintImage(const Item& item, const LayoutBox& box){
    if(item.image >= m_scope->m_images.size())
        return false;
    const ImageFrame& frame = m_scope->m_images[item.image];
    const auto placement = PlaceImage(box.rectangle, visibleClip(box.clip));
    if(!placement)
        return false;
    m_paint.pushClip(placement->clip);
    const b8 painted = frame.source ? m_paint.drawImage(frame.source, placement->bounds, { 0.0f, 0.0f, 1.0f, 1.0f }, frame.options.tint) : m_paint.drawRegion(frame.region, placement->bounds, frame.options.tint);
    return finishClipPaint(item, placement->bounds, placement->clip, painted);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 Builder::progress(const AStringView stableKey, const f64 fraction, const ProgressOptions& options){
    if(itemDeclarationBlocked() || !m_skin){
        m_context.fail();
        return false;
    }
    if(!synchronizePopup())
        return true;
    m_declaring = true;
    ScopeExit finish([this]()noexcept{ m_declaring = false; });
    const auto widget = m_context.declare(stableKey, WidgetKind::Progress);
    if(!widget)
        return false;
    ProgressFrame frame;
    frame.fraction = fraction;
    frame.options = options;
    frame.style = m_progressStyle;
    if(!ControlStyleMetrics().progressMetrics(*m_skin, fraction, options, frame.style, frame.metrics)){
        m_context.fail();
        return false;
    }
    Item item(m_arena);
    item.state = **widget;
    item.progress = static_cast<u32>(m_scope->m_progress.size());
    LayoutNodeDesc description;
    description.width = frame.options.width;
    description.intrinsicSize = frame.metrics.contentSize;
    if(!addItemNode(description, item))
        return false;
    m_scope->m_progress.push_back(Move(frame));
    m_scope->m_items.push_back(Move(item));
    return true;
}

b8 Builder::paintProgress(const Item& item, const LayoutBox& box){
    if(item.progress >= m_scope->m_progress.size())
        return false;
    const ProgressFrame& frame = m_scope->m_progress[item.progress];
    const auto placement = PlaceProgress(box.rectangle, visibleClip(box.clip), frame.metrics, frame.fraction);
    if(!placement)
        return false;
    const b8 painted = ControlVisualPainter().paintProgressBarRegions(m_paint, frame.style, *placement);
    return finishClipPaint(item, placement->bounds, placement->clip, painted);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 Builder::paintPanel(){
    const LayoutBox* panel = m_scope->m_layout.box(0u);
    if(!panel)
        return false;
    m_paint.pushClip(visibleClip(panel->clip));
    const b8 painted = m_paint.drawRegion(m_scope->m_panelStyle.panel, panel->rectangle);
    const b8 popped = m_paint.popClip();
    const HitTarget barrier = HitTargetFactory().makeBarrierHitTarget(panel->rectangle, visibleClip(panel->clip));
    if(!painted || !popped || !m_context.addTarget(m_scope->m_panelState, barrier))
        return false;
    return paintItems();
}

b8 Builder::paintItems(){
    for(const auto& item : m_scope->m_items){
        const LayoutBox* box = m_scope->m_layout.box(item.node);
        if(!popupAncestorsVisible() || !box || !paintItem(item, *box))
            return false;
    }
    return true;
}

b8 Builder::paintItem(const Item& item, const LayoutBox& box){
    if(item.state.kind == WidgetKind::ComboBox || item.state.kind == WidgetKind::SearchComboBox)
        return paintCombo(item, box);
    if(item.state.kind == WidgetKind::TextArea)
        return paintTextArea(item, box);
    if(item.state.kind == WidgetKind::EditBox)
        return paintEditBox(item, box);
    if(item.state.kind == WidgetKind::Selectable)
        return paintSelectable(item, box);
    if(item.state.kind == WidgetKind::VirtualList)
        return paintList(item, box);
    if(item.state.kind == WidgetKind::RadioGroup)
        return paintRadioGroup(item, box);
    if(item.state.kind == WidgetKind::Slider)
        return paintSlider(item, box);
    if(item.state.kind == WidgetKind::Progress)
        return paintProgress(item, box);
    if(item.state.kind == WidgetKind::Image)
        return paintImage(item, box);
    const InputRouter& input = m_context.input();
    const b8 hover = input.hover() == item.state.id;
    const b8 pressed = input.pressed(item.state.id);
    const b8 focused = input.focus() == item.state.id;
    const b8 interactive = item.state.kind == WidgetKind::Button || item.state.kind == WidgetKind::Checkbox;
    const Rect clip = visibleClip(box.clip);
    if(!ControlVisualPainter().paintBasicControlItemVisual(
        m_paint,
        m_text,
        *m_skin,
        item.state.kind,
        item.style,
        item.text,
        item.textBounds,
        item.padding,
        item.checkboxExtent,
        box.rectangle,
        clip,
        item.enabled,
        item.checked,
        hover,
        pressed,
        focused,
        interactive
    ))
        return false;
    if(item.state.kind == WidgetKind::Separator)
        return true;
    if(interactive || item.annotated){
        // Disabled controls still cover the panel; the router consumes the panel barrier beneath them.
        const HitTarget target = HitTargetFactory().makeBasicControlHitTarget(box.rectangle, clip, item.enabled, interactive, item.contextMenu);
        return m_context.addTarget(item.state, target);
    }
    return true;
}

b8 Builder::paintSelectable(const Item& item, const LayoutBox& box){
    const InputRouter& input = m_context.input();
    const SelectablePaintFlags flags{ item.enabled, item.checked, input.hover() == item.state.id, input.focus() == item.state.id };
    const Rect clip = visibleClip(box.clip);
    if(!PaintSelectable(m_paint, m_text, *m_skin, item.text, item.textBounds, item.padding, box.rectangle, clip, item.selectableStyle, flags, item.style.text, item.style.disabledText))
        return false;
    const HitTarget target = HitTargetFactory().makeBasicControlHitTarget(box.rectangle, clip, item.enabled, true, item.contextMenu);
    return m_context.addTarget(item.state, target);
}

b8 Builder::finishClipPaint(const Item& item, const Rect& bounds, const Rect& clip, b8 painted){
    const b8 popped = m_paint.popClip();
    if(!painted || !popped)
        return false;
    if(!item.annotated)
        return true;
    const HitTarget target = HitTargetFactory().makeAnnotatedClipHitTarget(bounds, clip, item.contextMenu);
    return m_context.addTarget(item.state, target);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 Builder::beginPopup(const AStringView stableKey, PopupState& state, const PopupOptions& options){
    const b8 nested = m_scope->m_popupState && m_scope->m_panelActive;
    if(declarationBlocked() || (!balanced() && !nested) || !m_skin || m_context.failed()){
        m_context.fail();
        return false;
    }
    if(nested && !synchronizePopup())
        return false;
    const auto widget = m_context.declare(stableKey, WidgetKind::Popup);
    if(!widget || !m_context.claimState(**widget, state.instanceGeneration()))
        return false;
    const WidgetState declaration = **widget;
    state.bindParent(m_context.popupToken());
    const PopupToken token{ (*widget)->id, (*widget)->declarationGeneration, state.instanceGeneration(), state.openGeneration() };
    m_context.input().fencePopup(token);
    if(m_context.input().consumePopupDismissal(token))
        state.close();
    if(!state.isOpen()){
        m_context.input().closePopup(token);
        return false;
    }
    const UiSkinRegion* background = region(m_popupStyle.background, m_popupStyle.fallback);
    const auto placement = PlacePopup(options, m_paint.displayMetrics());
    if(!background || !placement){
        m_context.fail();
        return false;
    }
    const PopupScope scope = UiScopeDescriptorFactory().popupFrame(token, m_context.popupToken(), *placement, options);
    NotNull<BuilderScopeFrame*> next = m_scope;
    if(!nested)
        reset();
    else{
        if(m_popupFrameCount >= m_popupFrames.size() || !m_popupFrames[m_popupFrameCount]){
            m_context.fail();
            return false;
        }
        BuilderScopeFrame& child = *m_popupFrames[m_popupFrameCount++];
        child.reset();
        child.m_parent = m_scope.get();
        next = MakeNotNull(&child);
    }
    if(!m_context.beginPopupScope(declaration, scope) || !m_context.pushScope(stableKey) || !m_paint.beginOverlay(m_context.popupLayer())){
        m_context.fail();
        return false;
    }
    m_scope = next;
    m_scope->m_panelState = declaration;
    m_scope->m_popupPaintStyle = m_popupStyle;
    m_scope->m_bounds = placement->bounds;
    const LayoutNodeDesc description = UiScopeDescriptorFactory().popupRoot(
        m_scope->m_bounds,
        m_scope->m_popupPaintStyle.padding,
        background->padding,
        m_style.gap
    );
    u32 node = 0u;
    const auto admittedNode = m_scope->m_layout.addNode(s_LayoutNoParent, description);
    if(!admittedNode){
        m_context.fail();
        return false;
    }
    node = *admittedNode;
    m_scope->m_stack.push_back(node);
    m_scope->m_popupState = &state;
    m_scope->m_popupToken = token;
    m_scope->m_popupOptions = options;
    m_scope->m_popupPlacement = *placement;
    state.m_placement = *placement;
    m_scope->m_panelActive = true;
    return true;
}

b8 Builder::endPopup(){
    if(declarationBlocked() || !m_scope->m_popupState || !m_scope->m_panelActive || m_scope->m_windowActive || m_scope->m_stack.size() != 1u){
        m_context.fail();
        return false;
    }
    m_finalizing = true;
    ScopeExit finish([this]()noexcept{ m_finalizing = false; });
    const b8 visible = synchronizePopup();
    const b8 arranged = !visible || (!m_context.failed() && m_scope->m_layout.arrange(m_scope->m_bounds));
    const b8 overlayEnded = m_paint.endOverlay();
    const b8 scopePopped = m_context.popScope();
    const b8 popupEnded = m_context.endPopupScope(visible && arranged);
    m_scope->m_panelActive = false;
    m_scope->m_stack.clear();
    if(!arranged || !overlayEnded || !scopePopped || !popupEnded){
        m_context.fail();
        if(m_scope->m_parent)
            m_scope = MakeNotNull(m_scope->m_parent);
        else
            releasePopupFamily();
        return false;
    }
    if(m_scope->m_parent){
        m_scope = MakeNotNull(m_scope->m_parent);
        return true;
    }
    const b8 finished = finishPopupFamily();
    if(!finished)
        m_context.fail();
    return finished;
}

b8 Builder::synchronizePopup()noexcept{
    for(const BuilderScopeFrame* frame = m_scope.get(); frame; frame = frame->m_parent){
        if(frame->m_popupState && !popupFrameVisible(*frame)){
            m_context.input().closePopup(frame->m_popupToken);
            return false;
        }
    }
    return true;
}

b8 Builder::popupAncestorsVisible()const noexcept{
    if(m_context.failed())
        return false;
    for(const BuilderScopeFrame* frame = m_scope.get(); frame; frame = frame->m_parent){
        if(frame->m_popupState && !popupFrameVisible(*frame))
            return false;
    }
    return true;
}

b8 Builder::finishPopupFamily(){
    // Visibility is resolved without calling application sources before any deferred subtree paint.
    m_scopeFrame.m_popupVisible = popupFrameVisible(m_scopeFrame);
    if(!m_scopeFrame.m_popupVisible)
        m_context.discardPopupScope(m_scopeFrame.m_popupToken);
    for(usize index = 0u; index < m_popupFrameCount; ++index){
        BuilderScopeFrame& frame = *m_popupFrames[index];
        frame.m_popupVisible = frame.m_parent->m_popupVisible && popupFrameVisible(frame);
        if(!frame.m_popupVisible)
            m_context.discardPopupScope(frame.m_popupToken);
    }
    const b8 painted = paintPopupFamily(m_scopeFrame);
    const b8 valid = painted && validatePopupFamily();
    for(usize index = 0u; index <= m_popupFrameCount; ++index){
        BuilderScopeFrame& frame = index == 0u ? m_scopeFrame : *m_popupFrames[index - 1u];
        m_scope = MakeNotNull(&frame);
        publishSliderResults(valid && frame.m_popupVisible);
    }
    releasePopupFamily();
    return valid;
}

b8 Builder::paintPopupFamily(BuilderScopeFrame& frame){
    m_scope = MakeNotNull(&frame);
    if(!frame.m_popupVisible)
        return true;
    if(!popupAncestorsVisible() || !m_context.activatePopupScope(frame.m_popupToken))
        return false;
    if(!m_paint.beginOverlay(m_context.popupLayer())){
        const b8 ended = m_context.endPopupScope(false);
        if(!ended)
            m_context.fail();
        return false;
    }
    b8 painted = paintPopup() && paintDeferredContents();
    for(usize index = 0u; painted && index < m_popupFrameCount; ++index){
        BuilderScopeFrame& child = *m_popupFrames[index];
        if(child.m_parent == &frame)
            painted = paintPopupFamily(child);
    }
    m_scope = MakeNotNull(&frame);
    const b8 overlayEnded = m_paint.endOverlay();
    const b8 scopeEnded = m_context.endPopupScope(painted);
    return painted && overlayEnded && scopeEnded;
}

b8 Builder::validatePopupFamily(){
    // A child callback can change an already painted parent, or a later sibling can change an ended child.
    for(usize index = 0u; index <= m_popupFrameCount; ++index){
        BuilderScopeFrame& frame = index == 0u ? m_scopeFrame : *m_popupFrames[index - 1u];
        m_scope = MakeNotNull(&frame);
        if(frame.m_popupVisible && (!popupFrameVisible(frame) || !validateDeferredSources()))
            return false;
    }
    // This pass calls no source code. Every model and ancestor lifetime is still borrowed until it completes.
    for(usize index = 0u; index <= m_popupFrameCount; ++index){
        BuilderScopeFrame& frame = index == 0u ? m_scopeFrame : *m_popupFrames[index - 1u];
        m_scope = MakeNotNull(&frame);
        if(frame.m_popupVisible && (!popupFrameVisible(frame) || !validateDeferredStates()))
            return false;
    }
    return !m_context.failed();
}

void Builder::releasePopupFamily()noexcept{
    for(usize index = 0u; index <= m_popupFrameCount; ++index){
        BuilderScopeFrame& frame = index == 0u ? m_scopeFrame : *m_popupFrames[index - 1u];
        m_scope = MakeNotNull(&frame);
        if(!frame.m_popupVisible){
            for(auto& tooltip : frame.m_tooltips){
                if(tooltip.state && tooltip.state->revision() == tooltip.revision)
                    tooltip.state->reset();
            }
        }
        releaseDeferredLoans();
        frame.m_popupState = nullptr;
    }
    m_scope = MakeNotNull(&m_scopeFrame);
}

b8 Builder::reserveCompoundPopup(const WidgetState& widget, const PopupToken& token){
    PopupOptions options;
    options.size = { 1.0f, 1.0f };
    const auto placement = PlacePopup(options, m_paint.displayMetrics());
    if(!placement)
        return false;
    const PopupScope scope = UiScopeDescriptorFactory().basicPopup(token, m_context.popupToken(), *placement);
    return m_context.registerPopupScope(widget, scope);
}

b8 Builder::paintPopup(){
    const LayoutBox* panel = m_scope->m_layout.box(0u);
    const UiSkinRegion* background = region(m_scope->m_popupPaintStyle.background, m_scope->m_popupPaintStyle.fallback);
    if(!panel || !background)
        return false;
    if(m_scope->m_popupOptions.modal){
        const Color& color = m_scope->m_popupPaintStyle.backdrop;
        if(!IsValidPaintColor(color))
            return false;
        m_paint.fillRect(m_scope->m_popupPlacement.viewport, color);
    }
    m_paint.pushClip(m_scope->m_popupPlacement.viewport);
    const b8 painted = m_paint.drawRegion(background->name, panel->rectangle);
    const b8 popped = m_paint.popClip();
    const HitTarget barrier = HitTargetFactory().makeBarrierHitTarget(panel->rectangle, m_scope->m_popupPlacement.viewport);
    if(!painted || !popped || !m_context.addTarget(m_scope->m_panelState, barrier))
        return false;
    return paintItems();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_builder_radio_group{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ReconcileGuard final : public IRadioGroupReconcileGuard{
private:
    struct PopupLoan{
        const PopupState& state;
        PopupToken token;
    };


public:
    explicit ReconcileGuard(const Context& context)noexcept
        : m_context(context)
    {}
    virtual ~ReconcileGuard()noexcept override = default;


public:
    [[nodiscard]] b8 add(const PopupState& state, const PopupToken& token)noexcept{
        if(m_count == s_InputMaxPopups)
            return false;
        m_popups[m_count].emplace(PopupLoan{ state, token });
        ++m_count;
        return true;
    }

    [[nodiscard]] virtual b8 current()const noexcept override{
        if(m_context.failed())
            return false;
        for(usize index = 0u; index < m_count; ++index){
            const PopupLoan& loan = *m_popups[index];
            if(!loan.state.isOpen() || loan.state.instanceGeneration() != loan.token.instanceGeneration || loan.state.openGeneration() != loan.token.openGeneration)
                return false;
        }
        return true;
    }


private:
    const Context& m_context;
    Array<Optional<PopupLoan>, s_InputMaxPopups> m_popups{};
    usize m_count = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RadioGroupResult Builder::radioGroup(const AStringView stableKey, const IListDataSource& source, RadioGroupState& state, const RadioGroupOptions& options){
    RadioGroupResult result;
    if(itemDeclarationBlocked() || !m_skin){
        m_context.fail();
        return result;
    }
    if(!synchronizePopup()){
        result.valid = true;
        return result;
    }
    m_declaring = true;
    ScopeExit finish([this]()noexcept{ m_declaring = false; });
    const auto widget = m_context.declare(stableKey, WidgetKind::RadioGroup);
    if(!widget || !m_context.claimState(**widget, state.instanceGeneration()))
        return result;
    RadioGroupFrame frame(m_arena, source, state);
    frame.m_options = options;
    frame.m_style = m_radioGroupStyle;
    frame.m_widgetStyle = m_style;
    // Reject options and style before invoking application callbacks or changing reconciliation state.
    const auto metrics = MeasureRadioGroup(0u, {}, options, frame.m_style);
    if(!metrics){
        m_context.fail();
        return result;
    }
    frame.m_metrics = *metrics;
    const b8 previouslyFocused = m_context.input().focus() == (*widget)->id;
    __hidden_builder_radio_group::ReconcileGuard guard(m_context);
    for(const BuilderScopeFrame* scope = m_scope.get(); scope; scope = scope->m_parent){
        if(scope->m_popupState && !guard.add(*scope->m_popupState, scope->m_popupToken)){
            m_context.fail();
            return result;
        }
    }
    const auto choices = RadioGroupBehavior::Reconcile(source, &guard, state, result);
    const b8 reconciled = choices.has_value();
    if(choices)
        frame.m_choices = *choices;
    if(m_context.failed()){
        result.valid = false;
        return result;
    }
    // Source callbacks can close or reopen an ancestor before reconciliation or copied input commits.
    if(!synchronizePopup()){
        result.valid = true;
        return result;
    }
    if(!reconciled){
        result.valid = false;
        m_context.fail();
        return result;
    }
    frame.m_snapshot = state.snapshot();
    frame.m_token = { state.inputGeneration(), frame.m_choices.sourceGeneration, frame.m_choices.sourceRevision };
    m_context.input().fenceControl((*widget)->id, (*widget)->declarationGeneration, frame.m_token);
    const b8 interactive = IsRadioGroupInteractive(frame.m_choices, options);
    if(!interactive)
        m_context.input().invalidateTarget((*widget)->id);
    while(auto action = m_context.takeControlAction(**widget, interactive, frame.m_token)){
        if(!RadioGroupBehavior::Apply(frame.m_choices, options, *action, state, result)){
            result.valid = false;
            m_context.fail();
            return result;
        }
    }
    frame.m_snapshot = state.snapshot();
    result.focused = interactive && m_context.input().focus() == (*widget)->id;
    frame.m_focusOnCommit = interactive && previouslyFocused && !result.focused;
    if(!prepareRadioGroup(frame)){
        result.valid = false;
        m_context.fail();
        return result;
    }
    Item item(m_arena);
    item.state = **widget;
    item.enabled = interactive;
    item.radioGroup = static_cast<u32>(m_scope->m_radioGroups.size());
    LayoutNodeDesc description;
    description.width = options.width;
    description.intrinsicSize = frame.m_metrics.contentSize;
    if(!addItemNode(description, item)){
        result.valid = false;
        return result;
    }
    m_scope->m_radioGroups.push_back(Move(frame));
    m_scope->m_items.push_back(Move(item));
    result.valid = true;
    return result;
}

b8 Builder::radioGroupMatches(const RadioGroupFrame& frame)const{
    // Repeat the stamp fence to catch a one-shot metadata mutation from the final count callback.
    for(u32 pass = 0u; pass < s_RadioGroupStampFencePassCount; ++pass){
        if(
            !radioGroupStateMatches(frame)
            || frame.m_source.instanceGeneration() != frame.m_choices.sourceGeneration || !radioGroupStateMatches(frame)
            || frame.m_source.revision() != frame.m_choices.sourceRevision || !radioGroupStateMatches(frame)
            || frame.m_source.rowCount() != frame.m_choices.count || !radioGroupStateMatches(frame)
        )
            return false;
    }
    return true;
}

b8 Builder::prepareRadioGroup(RadioGroupFrame& frame){
    if(!radioGroupMatches(frame))
        return false;
    Point maximumLabel{};
    frame.m_labels.reserve(frame.m_choices.count);
    for(u32 index = 0u; index < frame.m_choices.count; ++index){
        if(!radioGroupStateMatches(frame))
            return false;
        const StringView text = frame.m_source.text(index);
        // Do not call any further source method until the temporary label has been shaped and copied.
        if(!radioGroupStateMatches(frame))
            return false;
        auto label = m_text.layout(textShapeRequest(text, frame.m_widgetStyle.fontSize));
        if(!label || !radioGroupMatches(frame))
            return false;
        const auto textBounds = m_text.measure(*label, m_paint.displayMetrics());
        if(!textBounds)
            return false;
        maximumLabel.x = Max(maximumLabel.x, textBounds->width);
        maximumLabel.y = Max(maximumLabel.y, textBounds->height);
        frame.m_labels.push_back({ Move(*label), *textBounds });
    }
    if(!m_skin || !ControlStyleMetrics().radioGroupMetrics(*m_skin, frame.m_choices.count, maximumLabel, frame.m_options, frame.m_style, frame.m_metrics))
        return false;
    return radioGroupMatches(frame);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 Builder::paintRadioGroup(const Item& item, const LayoutBox& box){
    if(item.radioGroup >= m_scope->m_radioGroups.size())
        return false;
    RadioGroupFrame& frame = m_scope->m_radioGroups[item.radioGroup];
    if(!radioGroupMatches(frame) || frame.m_labels.size() != frame.m_choices.count)
        return false;
    const auto placement = PlaceRadioGroup(box.rectangle, visibleClip(box.clip), frame.m_choices, frame.m_metrics);
    if(!placement)
        return false;
    const HitTarget host = HitTargetFactory().makeRadioGroupHostHitTarget(*placement, item.enabled, item.contextMenu, frame.m_token, frame.m_focusOnCommit);
    if(!m_context.addTarget(item.state, host))
        return false;
    const auto choices = TryMakeWidgetId(item.state.id, s_WidgetPartChoices);
    if(!choices)
        return false;
    const UiSkinRegion* mark = placement->count != 0u ? region(frame.m_style.mark, frame.m_style.markFallback) : nullptr;
    for(u32 index = 0u; index < placement->count; ++index){
        const RadioGroupChoicePlacement& row = placement->rows[index];
        const auto part = TryMakeWidgetPartId(*choices, row.key);
        if(!part)
            return false;
        const InputRouter& input = m_context.input();
        const b8 enabled = frame.m_options.enabled && row.enabled;
        const b8 checked = frame.m_snapshot.selectedKey == row.key;
        const b8 hover = input.hover() == *part;
        const b8 pressed = input.pressed(*part) || (frame.m_snapshot.cursorKey == row.key && input.pressed(item.state.id));
        const b8 focused = enabled && input.focus() == item.state.id && frame.m_snapshot.cursorKey == row.key;
        if(!mark || !ControlVisualPainter().paintRadioGroupChoiceRow(
            m_paint,
            m_text,
            *m_skin,
            frame.m_style,
            frame.m_widgetStyle,
            frame.m_metrics,
            row,
            frame.m_labels[index].text,
            frame.m_labels[index].bounds,
            mark->name,
            enabled,
            checked,
            pressed,
            hover,
            focused
        ))
            return false;
        if(item.enabled){
            const HitTarget target = HitTargetFactory().makeRadioGroupChoiceHitTarget(row, enabled, frame.m_token);
            if(!m_context.addPartTarget(item.state, *part, target))
                return false;
        }
    }
    if(!radioGroupMatches(frame))
        return false;
    // Diagnostics describe prepared placement; input still uses the last published owned hit targets.
    frame.m_state.m_placement = *placement;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 Builder::slider(const AStringView stableKey, SliderState& state, const SliderOptions& options){
    if(itemDeclarationBlocked() || !m_skin){
        m_context.fail();
        return false;
    }
    if(!synchronizePopup())
        return true;
    m_declaring = true;
    ScopeExit finish([this]()noexcept{ m_declaring = false; });
    const auto widget = m_context.declare(stableKey, WidgetKind::Slider);
    if(!widget || !m_context.claimState(**widget, state.instanceGeneration()))
        return false;
    // Attempt diagnostics retire immediately; invalid policy leaves semantic state and accepted geometry untouched.
    state.m_result = {};
    SliderFrame frame(state);
    frame.m_options = options;
    frame.m_style = m_sliderStyle;
    frame.m_snapshot = state.snapshot();
    frame.m_widget = **widget;
    if(!IsValidSliderOptions(options)){
        m_context.fail();
        return false;
    }
    const auto metrics = MeasureSlider(options, frame.m_style);
    if(!metrics){
        m_context.fail();
        return false;
    }
    frame.m_metrics = *metrics;
    if(!prepareSlider(frame)){
        m_context.fail();
        return false;
    }
    Item item(m_arena);
    item.state = **widget;
    item.enabled = options.enabled;
    item.slider = static_cast<u32>(m_scope->m_sliders.size());
    LayoutNodeDesc description;
    description.width = options.width;
    description.intrinsicSize = frame.m_metrics.contentSize;
    if(!addItemNode(description, item))
        return false;
    m_scope->m_sliders.push_back(Move(frame));
    m_scope->m_items.push_back(Move(item));
    return true;
}

b8 Builder::prepareSlider(SliderFrame& frame)noexcept{
    if(!sliderMatches(frame) || !m_skin)
        return false;
    if(!ControlStyleMetrics().sliderMetrics(*m_skin, frame.m_options, frame.m_style, frame.m_metrics))
        return false;
    return sliderMatches(frame);
}

void Builder::publishSliderResults(const b8 valid)noexcept{
    const InputRouter& input = m_context.input();
    for(const auto& frame : m_scope->m_sliders){
        SliderResult result;
        if(valid && frame.m_result.valid && sliderMatches(frame)){
            const auto finalized = TryFinalizeSliderFrameResult(
                frame.m_result,
                frame.m_widget.id,
                frame.m_interactive,
                input.focus(),
                input.primaryDown(),
                input.capture()
            );
            if(!finalized){
                m_context.fail();
                frame.m_state.m_result = {};
                continue;
            }
            result = *finalized;
        }
        frame.m_state.m_result = result;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 Builder::paintSlider(const Item& item, const LayoutBox& box){
    if(item.slider >= m_scope->m_sliders.size())
        return false;
    SliderFrame& frame = m_scope->m_sliders[item.slider];
    if(!sliderMatches(frame))
        return false;
    auto normalized = SliderBehavior::Normalize(frame.m_options.minimum, frame.m_options.maximum, frame.m_state.value());
    if(!normalized)
        return false;
    const Rect clip = visibleClip(box.clip);
    auto placement = PlaceSlider(box.rectangle, clip, frame.m_metrics, *normalized);
    if(!placement || !SliderBehavior::Admit(frame.m_state, frame.m_options, *placement))
        return false;
    frame.m_snapshot = frame.m_state.snapshot();
    frame.m_token = frame.m_state.controlToken();
    const b8 interactive = IsSliderPlacementInteractive(frame.m_options, *placement);
    InputRouter& input = m_context.input();
    const b8 previouslyFocused = input.focus() == item.state.id;
    // Prospective geometry and policy fence the displayed lifetime before any copied intention changes the value.
    input.fenceControl(item.state.id, item.state.declarationGeneration, frame.m_token);
    if(!interactive)
        input.invalidateTarget(item.state.id);
    frame.m_focusOnCommit = interactive && previouslyFocused && input.focus() != item.state.id;
    frame.m_interactive = interactive;
    if(!applySliderInput(frame, *placement, interactive))
        return false;
    normalized = SliderBehavior::Normalize(frame.m_options.minimum, frame.m_options.maximum, frame.m_state.value());
    if(!normalized)
        return false;
    placement = PlaceSlider(box.rectangle, clip, frame.m_metrics, *normalized);
    if(!placement)
        return false;
    const auto trackId = TryMakeWidgetId(item.state.id, s_WidgetPartTrack);
    if(!trackId)
        return false;
    const auto thumbId = TryMakeWidgetId(item.state.id, s_WidgetPartThumb);
    if(!thumbId)
        return false;
    const b8 hover = interactive && input.hover() == *thumbId;
    const b8 pressed = interactive && input.primaryDown() && (input.capture() == *trackId || input.capture() == *thumbId);
    const b8 focused = input.focus() == item.state.id;
    if(!ControlVisualPainter().paintSliderVisual(m_paint, *m_skin, frame.m_style, *placement, interactive, pressed, hover, focused) || !sliderMatches(frame))
        return false;
    const HitTarget host = HitTargetFactory().makeSliderHostHitTarget(*placement, interactive, item.contextMenu, frame.m_token, frame.m_focusOnCommit);
    if(!m_context.addTarget(item.state, host))
        return false;
    if(interactive){
        const HitTarget trackTarget = HitTargetFactory().makeSliderTrackHitTarget(*placement, frame.m_token);
        if(!m_context.addPartTarget(item.state, *trackId, trackTarget))
            return false;
        const HitTarget thumbTarget = HitTargetFactory().makeSliderThumbHitTarget(*placement, frame.m_token, frame.m_state.value());
        if(!m_context.addPartTarget(item.state, *thumbId, thumbTarget))
            return false;
    }
    if(!sliderMatches(frame))
        return false;
    frame.m_state.m_placement = *placement;
    frame.m_result.valid = true;
    return true;
}

b8 Builder::applySliderInput(SliderFrame& frame, const SliderPlacement& placement, const b8 interactive){
    if(!sliderMatches(frame))
        return false;
    if(!interactive || placement.centerTravel.width <= 0.0f)
        return true;
    const auto track = TryMakeWidgetId(frame.m_widget.id, s_WidgetPartTrack);
    if(!track)
        return false;
    const auto thumb = TryMakeWidgetId(frame.m_widget.id, s_WidgetPartThumb);
    if(!thumb)
        return false;
    auto control = m_context.takeControlAction(frame.m_widget, true, frame.m_token);
    b8 hasControl = control.has_value();
    auto trackGesture = m_context.takePartPointerGesture(frame.m_widget, *track, true);
    b8 hasTrack = trackGesture.has_value();
    auto thumbGesture = m_context.takePartPointerGesture(frame.m_widget, *thumb, true);
    b8 hasThumb = thumbGesture.has_value();
    while(hasControl || hasTrack || hasThumb){
        if(!sliderMatches(frame))
            return false;
        const u8 stream = SelectNextSliderInputStream(
            hasControl,
            hasControl ? control->id.sequence : 0u,
            hasTrack,
            hasTrack ? trackGesture->updateSequence : 0u,
            hasThumb,
            hasThumb ? thumbGesture->updateSequence : 0u
        );
        if(stream == 0u){
            if(control->control != frame.m_token || !SliderBehavior::Apply(frame.m_options, *control, frame.m_state, frame.m_result))
                return false;
            frame.m_snapshot = frame.m_state.snapshot();
            control = m_context.takeControlAction(frame.m_widget, true, frame.m_token);
            hasControl = control.has_value();
        }
        else if(stream == 1u){
            if(trackGesture->control != frame.m_token || !SliderBehavior::Seek(frame.m_options, *trackGesture, frame.m_state, frame.m_result))
                return false;
            frame.m_snapshot = frame.m_state.snapshot();
            trackGesture = m_context.takePartPointerGesture(frame.m_widget, *track, true);
            hasTrack = trackGesture.has_value();
        }
        else{
            if(thumbGesture->control != frame.m_token || !SliderBehavior::Drag(frame.m_options, *thumbGesture, frame.m_state, frame.m_result))
                return false;
            frame.m_snapshot = frame.m_state.snapshot();
            thumbGesture = m_context.takePartPointerGesture(frame.m_widget, *thumb, true);
            hasThumb = thumbGesture.has_value();
        }
    }
    return sliderMatches(frame);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 Builder::beginWindow(const AStringView stableKey, const StringView title, WindowState& state, const WindowOptions& options){
    if(declarationBlocked() || m_scope->m_panelActive || m_scope->m_windowActive || !m_skin || m_context.failed()){
        m_context.fail();
        return false;
    }
    reset();
    m_scope->m_window.style = m_style;
    const auto window = m_context.declare(stableKey, WidgetKind::Window);
    const ShapeRequest request = textShapeRequest(title, m_style.fontSize);
    if(!window){
        m_context.fail();
        return false;
    }
    auto titleLayout = m_text.layout(request);
    if(!titleLayout){
        m_context.fail();
        return false;
    }
    m_scope->m_window.title = Move(*titleLayout);
    const auto titleBounds = m_text.measure(m_scope->m_window.title, m_paint.displayMetrics());
    if(!titleBounds || !ControlStyleMetrics().windowMetrics(*m_skin, m_style, options, { titleBounds->width, titleBounds->height }, m_scope->m_window.metrics)){
        m_context.fail();
        return false;
    }
    m_scope->m_window.titleBounds = *titleBounds;
    m_scope->m_panelState = **window;
    m_scope->m_window.state = &state;
    m_scope->m_window.options = options;
    m_scope->m_window.firstUse = !state.initialized;
    WindowState candidate = state;
    if(!TryInitializeWindow(options, m_scope->m_window.metrics, candidate) || !m_context.pushScope(stableKey)){
        m_context.fail();
        return false;
    }
    // Keys beginning with @window. belong to the window's internal chrome.
    const auto titleState = m_context.declare(s_WindowChromeTitleKey, WidgetKind::Window);
    const auto collapseState = m_context.declare(s_WindowChromeCollapseKey, WidgetKind::Button);
    const auto resizeState = m_context.declare(s_WindowChromeResizeKey, WidgetKind::Window);
    if(!titleState || !collapseState || !resizeState){
        m_context.fail();
        return false;
    }
    m_scope->m_window.titleState = **titleState;
    m_scope->m_window.collapseState = **collapseState;
    m_scope->m_window.resizeState = **resizeState;
    if(!options.collapsible)
        candidate.collapsed = false;
    if(m_context.takeActivation(m_scope->m_window.collapseState, options.collapsible))
        candidate.collapsed = !candidate.collapsed;
    while(auto gesture = m_context.takePointerGesture(m_scope->m_window.titleState, options.movable)){
        if(!TryApplyWindowMove(*gesture, candidate)){
            m_context.fail();
            return false;
        }
    }
    while(auto gesture = m_context.takePointerGesture(m_scope->m_window.resizeState, options.resizable && !candidate.collapsed)){
        if(!TryApplyWindowResize(*gesture, m_scope->m_window.metrics.minimumSize, candidate)){
            m_context.fail();
            return false;
        }
    }
    if(!TryConstrainWindow(m_paint.displayMetrics(), m_scope->m_window.metrics.titleHeight, candidate)){
        m_context.fail();
        return false;
    }
    m_scope->m_bounds = ResolveWindowContentBounds(candidate, m_scope->m_window.metrics);
    const LayoutNodeDesc description = BuildWindowRootLayoutNodeDesc(
        options,
        m_scope->m_window.metrics,
        m_scope->m_bounds,
        m_scope->m_window.firstUse,
        m_style.gap
    );
    u32 node = 0u;
    const auto admittedNode = m_scope->m_layout.addNode(s_LayoutNoParent, description);
    if(!admittedNode){
        m_context.fail();
        return false;
    }
    node = *admittedNode;
    m_scope->m_stack.push_back(node);
    m_scope->m_panelActive = true;
    m_scope->m_windowActive = true;
    state = candidate;
    return !state.collapsed;
}

b8 Builder::endWindow(){
    if(declarationBlocked() || !m_scope->m_windowActive || !m_scope->m_panelActive || !m_scope->m_window.state || m_scope->m_stack.size() != 1u || m_context.failed()){
        m_context.fail();
        return false;
    }
    m_finalizing = true;
    ScopeExit finish([this]()noexcept{ m_finalizing = false; });
    WindowState& state = *m_scope->m_window.state;
    b8 arranged = true;
    if(!state.collapsed){
        const DisplayMetrics& display = m_paint.displayMetrics();
        const Rect viewport{ m_scope->m_bounds.x, m_scope->m_bounds.y, Max(display.logicalWidth, m_scope->m_bounds.width), Max(display.logicalHeight, m_scope->m_bounds.height) };
        arranged = m_scope->m_layout.arrange(viewport);
        const LayoutBox* root = m_scope->m_layout.box(0u);
        if(arranged && root){
            arranged = TryFinalizeWindowFirstUseArrangement(
                m_scope->m_window.options,
                m_scope->m_window.metrics,
                root->rectangle,
                m_scope->m_window.firstUse,
                display,
                state,
                m_scope->m_bounds
            );
            if(arranged)
                arranged = m_scope->m_layout.arrange(m_scope->m_bounds);
        }
        else
            arranged = false;
    }
    const b8 painted = arranged && paintWindow();
    const b8 popped = m_context.popScope();
    m_scope->m_panelActive = false;
    m_scope->m_windowActive = false;
    m_scope->m_window.state = nullptr;
    m_scope->m_stack.clear();
    const b8 combosPainted = painted && popped && paintDeferred();
    publishSliderResults(combosPainted);
    releaseDeferredLoans();
    if(!combosPainted)
        m_context.fail();
    return combosPainted;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 Builder::paintWindow(){
    const WindowState& state = *m_scope->m_window.state;
    const WidgetStyle& style = m_scope->m_window.style;
    const Rect bounds = ResolveVisibleWindowBounds(state, m_scope->m_window.metrics);
    const Rect clip = visibleClip(bounds);
    m_paint.pushClip(clip);
    const b8 painted = m_paint.drawRegion(style.window, bounds);
    const b8 popped = m_paint.popClip();
    const HitTarget barrier = HitTargetFactory().makeBarrierHitTarget(bounds, clip);
    if(!painted || !popped || !m_context.addTarget(m_scope->m_panelState, barrier) || !paintWindowTitle())
        return false;
    if(state.collapsed)
        return true;
    return paintItems() && paintWindowResize();
}

b8 Builder::paintWindowTitle(){
    const WindowState& state = *m_scope->m_window.state;
    const WidgetStyle& style = m_scope->m_window.style;
    const WindowMetrics& metrics = m_scope->m_window.metrics;
    const Rect header{ state.bounds.x, state.bounds.y, state.bounds.width, metrics.titleHeight };
    const Rect clip = visibleClip(header);
    const InputRouter& input = m_context.input();
    const b8 collapseHovered = input.hover() == m_scope->m_window.collapseState.id;
    const b8 collapsePressed = input.pressed(m_scope->m_window.collapseState.id);
    const b8 collapseFocused = input.focus() == m_scope->m_window.collapseState.id;
    const b8 painted = ControlVisualPainter().paintWindowTitleHeader(
        m_paint,
        m_text,
        *m_skin,
        state,
        style,
        metrics,
        m_scope->m_window.options.collapsible,
        collapseHovered,
        collapsePressed,
        collapseFocused,
        m_scope->m_window.title,
        m_scope->m_window.titleBounds,
        header,
        clip
    );
    const HitTarget title = HitTargetFactory().makeWindowTitleHitTarget(header, clip, state.bounds, m_scope->m_window.options.movable);
    if(!painted || !m_context.addTarget(m_scope->m_window.titleState, title))
        return false;
    if(m_scope->m_window.options.collapsible){
        const HitTarget collapse = HitTargetFactory().makeWindowCollapseHitTarget(state, metrics, clip);
        return m_context.addTarget(m_scope->m_window.collapseState, collapse);
    }
    return true;
}

b8 Builder::paintWindowResize(){
    if(!m_scope->m_window.options.resizable)
        return true;
    const WindowState& state = *m_scope->m_window.state;
    const WidgetStyle& style = m_scope->m_window.style;
    const Rect corner = ResolveWindowResizeBounds(state, m_scope->m_window.metrics);
    const Rect clip = visibleClip(ResolveVisibleWindowBounds(state, m_scope->m_window.metrics));
    const b8 painted = ControlVisualPainter().paintWindowResizeCorner(m_paint, *m_skin, style, corner, clip);
    const HitTarget target = HitTargetFactory().makeWindowResizeHitTarget(corner, clip, state.bounds);
    return painted && m_context.addTarget(m_scope->m_window.resizeState, target);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ComboResult ComboCoordinator::declareCombo(AStringView stableKey, const IListDataSource& source, const ComboOptions& options, const SearchComboOptions* searchOptions, ComboState& state, SearchComboState* search, ISearchableListDataSource* searchSource){
    ComboResult result;
    if(m_builder.itemDeclarationBlocked() || !IsValidComboDeclarationOptions(options, m_builder.m_comboStyle)){
        m_builder.m_context.fail();
        return result;
    }
    if(!m_builder.synchronizePopup()){
        result.valid = true;
        return result;
    }
    m_builder.m_declaring = true;
    ScopeExit finish([this]()noexcept{ m_builder.m_declaring = false; });
    const auto widget = m_builder.m_context.declare(stableKey, search ? WidgetKind::SearchComboBox : WidgetKind::ComboBox);
    if(!widget)
        return result;
    const WidgetState field = **widget;
    const u64 previousSelection = state.selectedKey();
    const b8 previouslyOpen = state.isOpen();
    const b8 previouslyFocused = m_builder.m_context.input().focus() == field.id;
    if(!m_builder.m_context.claimState(field, state.m_popup.instanceGeneration()) || !ComboBehavior::Bind(state, field.id, field.declarationGeneration))
        return result;
    state.m_popup.bindParent(m_builder.m_context.popupToken());
    if(previouslyOpen && !state.isOpen())
        ComboBehavior::Close(state);
    const TextAreaModelStamp queryStamp = search ? search->query().stamp() : TextAreaModelStamp{};
    if(!ComboBehavior::Reconcile(state, source) || (search && !search->query().matchesStamp(queryStamp))){
        m_builder.m_context.fail();
        return result;
    }
    if(search && (!SearchComboBehavior::Filter(*search, *searchSource) || !search->query().matchesStamp(queryStamp))){
        m_builder.m_context.fail();
        return result;
    }
    const IListDataSource& results = search ? searchSource->filtered() : source;
    if(!ListBehavior::Reconcile(state.m_list, results) || (search && !search->query().matchesStamp(queryStamp))){
        m_builder.m_context.fail();
        return result;
    }
    ComboFrame frame;
    frame.parentToken = m_builder.m_context.popupToken();
    frame.source = &source;
    frame.results = &results;
    frame.searchSource = searchSource;
    frame.search = search;
    frame.style = m_builder.m_comboStyle;
    frame.popupStyle = m_builder.m_popupStyle;
    frame.queryHeight = searchOptions ? searchOptions->queryHeight : 0.0f;
    frame.queryGap = searchOptions ? searchOptions->queryGap : 0.0f;
    snapshotComboQuery(frame);
    frame.state = &state;
    frame.options = options;
    frame.token = { state.inputGeneration(), source.instanceGeneration(), source.revision() };
    frame.rowCount = source.rowCount();
    frame.resultCount = results.rowCount();
    const auto popup = m_builder.m_context.declarePart(field, s_WidgetPartPopup, WidgetKind::Popup);
    if(!popup)
        return result;
    frame.popup = **popup;
    const auto rows = m_builder.m_context.declarePart(field, s_WidgetPartRows, WidgetKind::VirtualList);
    if(!rows)
        return result;
    frame.rows = **rows;
    frame.popupToken = { frame.popup.id, frame.popup.declarationGeneration, state.m_popup.instanceGeneration(), state.m_popup.openGeneration() };
    frame.listToken = { state.m_list.inputGeneration(), results.instanceGeneration(), results.revision() };
    frame.open = state.isOpen();
    const b8 previouslyListFocused = m_builder.m_context.input().focus() == frame.rows.id;
    if(!m_builder.comboMatches(frame) || !applyComboInput(field, frame, result)){
        m_builder.m_context.fail();
        return {};
    }
    frame.open = state.isOpen();
    state.m_popup.bindParent(frame.parentToken);
    if(search && !frame.open)
        search->m_editor.focused = false;
    frame.popupToken.openGeneration = state.m_popup.openGeneration();
    frame.listToken.instanceGeneration = state.m_list.inputGeneration();
    frame.focusOnCommit = options.enabled && previouslyFocused && m_builder.m_context.input().focus() != field.id && !frame.open;
    frame.listFocusOnCommit = options.enabled && previouslyListFocused && m_builder.m_context.input().focus() != frame.rows.id;
    Item item(m_builder.m_arena);
    item.state = field;
    item.style = m_builder.m_style;
    item.enabled = options.enabled;
    item.combo = static_cast<u32>(m_builder.m_scope->m_combos.size());
    Point minimumSize{};
    if(!m_builder.m_skin || !ControlStyleMetrics().comboMetrics(*m_builder.m_skin, frame.style, minimumSize, item.padding, frame.arrowExtent)){
        m_builder.m_context.fail();
        return {};
    }
    const b8 selected = state.selectedKey() != 0u;
    const auto index = selected ? source.indexOf(state.selectedKey()) : Expected<u64>{ 0u };
    if(selected && (!index || *index >= frame.rowCount || source.key(*index) != state.selectedKey() || !source.enabled(*index))){
        m_builder.m_context.fail();
        return {};
    }
    // Text is shaped before another source callback can invalidate a temporary row label.
    const ShapeRequest request = m_builder.textShapeRequest(selected ? source.text(*index) : options.placeholder, item.style.fontSize);
    auto layout = m_builder.m_text.layout(request);
    if(!layout){
        m_builder.m_context.fail();
        return {};
    }
    item.text = Move(*layout);
    if(!m_builder.comboMatches(frame)){
        m_builder.m_context.fail();
        return {};
    }
    frame.options.placeholder = {};
    ListOptions listOptions;
    listOptions.wheelRows = options.wheelRows;
    listOptions.enabled = options.enabled;
    listOptions.selectOnNavigate = false;
    const WidgetId commandFocus = frame.editor != s_LayoutNoParent ? m_builder.m_scope->m_comboEditors[frame.editor].state.id : WidgetId{};
    ListFrame list(m_builder.m_arena);
    if(!m_builder.prepareListFrame(*frame.results, state.m_list, listOptions, frame.listToken, frame.resultCount, frame.listFocusOnCommit, commandFocus, list) || !m_builder.comboMatches(frame)){
        m_builder.m_context.fail();
        return {};
    }
    const auto textBounds = m_builder.m_text.measure(item.text, m_builder.m_paint.displayMetrics());
    if(!textBounds){
        m_builder.m_context.fail();
        return {};
    }
    item.textBounds = *textBounds;
    const Point measured{ textBounds->width, textBounds->height };
    LayoutNodeDesc description;
    description.width = options.width;
    description.height = options.height;
    description.intrinsicSize = ComputeComboFieldIntrinsicSize(minimumSize, measured, frame.arrowExtent, frame.style.arrowGap, item.padding);
    if(!m_builder.addItemNode(description, item))
        return {};
    frame.list = static_cast<u32>(m_builder.m_scope->m_lists.size());
    if(frame.open && !m_builder.reserveCompoundPopup(frame.popup, frame.popupToken)){
        m_builder.m_context.fail();
        return {};
    }
    m_builder.m_scope->m_lists.push_back(Move(list));
    m_builder.m_scope->m_combos.push_back(frame);
    m_builder.m_scope->m_items.push_back(Move(item));
    result.valid = true;
    result.selectionChanged = previousSelection != state.selectedKey();
    result.opened = !previouslyOpen && state.isOpen();
    result.closed = previouslyOpen && !state.isOpen();
    result.focused = options.enabled && (m_builder.m_context.input().focus() == field.id || m_builder.m_context.input().focus() == frame.rows.id || (frame.editor != s_LayoutNoParent && m_builder.m_context.input().focus() == m_builder.m_scope->m_comboEditors[frame.editor].state.id));
    return result;
}

SearchComboResult ComboCoordinator::searchComboBox(AStringView stableKey, ISearchableListDataSource& source, SearchComboState& state, const SearchComboOptions& options){
    if(m_builder.declarationBlocked() || !IsValidSearchComboDeclarationOptions(options)){
        m_builder.m_context.fail();
        return {};
    }
    const usize previousCount = m_builder.m_scope->m_combos.size();
    SearchComboResult result;
    result.combo = declareCombo(stableKey, source, options.combo, &options, state.combo(), &state, &source);
    if(result.combo.valid && m_builder.m_scope->m_combos.size() > previousCount)
        result.queryChanged = m_builder.m_scope->m_combos.back().queryChanged;
    return result;
}

b8 ComboCoordinator::prepareComboSearch(const WidgetState& field, ComboFrame& frame){
    SearchComboState& state = *frame.search;
    EditModel& query = state.query();
    if(!frame.open){
        query.cancelComposition();
        snapshotComboQuery(frame);
        return m_builder.comboMatches(frame);
    }
    const auto declared = m_builder.m_context.declarePart(field, s_WidgetPartQuery, WidgetKind::EditBox);
    if(!declared)
        return false;
    const WidgetState editor = **declared;
    if(!m_builder.m_context.claimState(editor, query.instanceGeneration()))
        return false;
    Item item(m_builder.m_arena);
    item.state = editor;
    item.style = m_builder.m_style;
    item.editStyle = m_builder.m_editStyle;
    item.editOptions.enabled = frame.options.enabled;
    EditBoxResult result;
    result.valid = true;
    result.focused = m_builder.m_context.input().focus() == editor.id;
    if(m_builder.m_editHost)
        result = m_builder.m_editHost->editInPopup(editor, item.editOptions, frame.popupToken, query);
    if(!result.valid)
        return false;
    frame.queryChanged = result.textChanged;
    frame.editorSubmitted = result.submitted;
    if(result.cancelled){
        ComboBehavior::Close(*frame.state);
        m_builder.m_context.input().closePopup(frame.popupToken);
        frame.open = false;
    }
    snapshotComboQuery(frame);
    if(!SearchComboBehavior::Filter(state, *frame.searchSource))
        return false;
    frame.results = &frame.searchSource->filtered();
    if(!ListBehavior::Reconcile(frame.state->m_list, *frame.results))
        return false;
    frame.listToken = { frame.state->m_list.inputGeneration(), frame.results->instanceGeneration(), frame.results->revision() };
    frame.resultCount = frame.results->rowCount();
    m_builder.m_context.input().fenceControl(frame.rows.id, frame.rows.declarationGeneration, frame.listToken);
    if(!m_builder.comboMatches(frame))
        return false;
    if(!frame.open)
        return true;
    if(!m_builder.prepareEditBox(result, query, state.m_editor, item))
        return false;
    frame.editor = static_cast<u32>(m_builder.m_scope->m_comboEditors.size());
    m_builder.m_scope->m_comboEditors.push_back(Move(item));
    return m_builder.comboMatches(frame);
}

void ComboCoordinator::snapshotComboQuery(ComboFrame& frame)noexcept{
    if(!frame.search)
        return;
    frame.queryStamp = frame.search->query().stamp();
}

b8 ComboCoordinator::paintComboQuery(ComboFrame& frame, LayoutBox& content){
    if(frame.editor >= m_builder.m_scope->m_comboEditors.size() || !m_builder.comboMatches(frame))
        return false;
    const Item& item = m_builder.m_scope->m_comboEditors[frame.editor];
    const f32 measuredHeight = (item.editView.visualBounds().height + item.padding.top) + item.padding.bottom;
    Rect nextContentRect = content.rectangle;
    LayoutBox box = content;
    box.rectangle = ResolveComboQueryBoxAndAdvanceContent(measuredHeight, frame.queryHeight, frame.queryGap, nextContentRect);
    const HitTarget navigation = HitTargetFactory().makeComboQueryNavigationTarget(
        frame.options.enabled,
        item.editView.composing(),
        frame.rows.id,
        frame.rows.declarationGeneration,
        frame.listToken
    );
    if(!m_builder.paintEditBox(item, box, &navigation))
        return false;
    content.rectangle = nextContentRect;
    return m_builder.comboMatches(frame);
}

ComboResult Builder::declareCombo(AStringView stableKey, const IListDataSource& source, const ComboOptions& options, const SearchComboOptions* searchOptions, ComboState& state, SearchComboState* search, ISearchableListDataSource* searchSource){
    return ComboCoordinator(*this).declareCombo(stableKey, source, options, searchOptions, state, search, searchSource);
}

SearchComboResult Builder::searchComboBox(AStringView stableKey, ISearchableListDataSource& source, SearchComboState& state, const SearchComboOptions& options){
    return ComboCoordinator(*this).searchComboBox(stableKey, source, state, options);
}

b8 Builder::comboStateMatches(const ComboFrame& frame)const noexcept{
    if(!frame.state)
        return false;
    return MatchesComboFrameStateValues(
        popupAncestorsVisible(),
        frame.state->inputGeneration(),
        frame.token.instanceGeneration,
        frame.state->m_list.inputGeneration(),
        frame.listToken.instanceGeneration,
        frame.state->isOpen(),
        frame.open,
        frame.state->m_popup.instanceGeneration(),
        frame.popupToken.instanceGeneration,
        frame.state->m_popup.openGeneration(),
        frame.popupToken.openGeneration,
        frame.state->m_popup.m_parent,
        frame.parentToken,
        !frame.search || frame.search->query().matchesStamp(frame.queryStamp)
    );
}

b8 Builder::comboMatches(const ComboFrame& frame)const{
    return
        frame.source && comboStateMatches(frame) && (!frame.searchSource || &frame.searchSource->filtered() == frame.results) && comboStateMatches(frame)
        && frame.source->instanceGeneration() == frame.token.contentGeneration && comboStateMatches(frame)
        && frame.source->revision() == frame.token.contentRevision && comboStateMatches(frame)
        && frame.source->rowCount() == frame.rowCount && comboStateMatches(frame)
        && frame.results && frame.results->instanceGeneration() == frame.listToken.contentGeneration && comboStateMatches(frame)
        && frame.results->revision() == frame.listToken.contentRevision && comboStateMatches(frame)
        && frame.results->rowCount() == frame.resultCount && comboStateMatches(frame)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ComboCoordinator::applyComboInput(const WidgetState& field, ComboFrame& frame, ComboResult& result){
    InputRouter& input = m_builder.m_context.input();
    ComboState& state = *frame.state;
    input.fenceControl(field.id, field.declarationGeneration, frame.token);
    input.fencePopup(frame.popupToken);
    if(!frame.options.enabled){
        input.invalidateTarget(field.id);
        ComboBehavior::Close(state);
        input.closePopup(frame.popupToken);
        if(frame.search){
            frame.search->query().cancelComposition();
            snapshotComboQuery(frame);
        }
        return true;
    }
    if(input.consumePopupDismissal(frame.popupToken))
        ComboBehavior::Close(state);
    if(!state.isOpen())
        input.closePopup(frame.popupToken);
    if(m_builder.m_context.takeActivation(field, true))
        ComboBehavior::Open(state);
    while(auto action = m_builder.m_context.takeControlAction(field, true, frame.token)){
        if(state.isOpen())
            continue;
        if(!IsComboActionEligibleToOpen(action->kind))
            continue;
        ComboBehavior::Open(state);
        if(!ListBehavior::Reconcile(state.m_list, *frame.results))
            return false;
        if(action->kind != ControlActionKind::Submit){
            action->control = { state.m_list.inputGeneration(), frame.results->instanceGeneration(), frame.results->revision() };
            ListOptions options;
            options.wheelRows = frame.options.wheelRows;
            options.selectOnNavigate = false;
            ListResult preview;
            if(!ListBehavior::Apply(*frame.results, options, *action, state.m_list, preview))
                return false;
        }
    }
    if(!ListBehavior::Reconcile(state.m_list, *frame.results))
        return false;
    frame.popupToken.openGeneration = state.m_popup.openGeneration();
    frame.listToken.instanceGeneration = state.m_list.inputGeneration();
    frame.open = state.isOpen();
    input.fencePopup(frame.popupToken);
    input.fenceControl(frame.rows.id, frame.rows.declarationGeneration, frame.listToken);
    if(!m_builder.comboMatches(frame))
        return false;
    if(frame.search && !prepareComboSearch(field, frame))
        return false;
    return !state.isOpen() || applyComboListInput(frame, result);
}

b8 ComboCoordinator::applyComboListInput(ComboFrame& frame, ComboResult& result){
    ComboState& state = *frame.state;
    ListOptions options;
    options.wheelRows = frame.options.wheelRows;
    options.selectOnNavigate = false;
    return ListInput(m_builder.m_context).consume(
        frame.rows,
        frame.listToken,
        true,
        &frame.popupToken,
        state.m_list,
        [&](){ return m_builder.comboMatches(frame); },
        [&](const ControlAction& action){
            const b8 fromEditor = frame.editor != s_LayoutNoParent && action.source == m_builder.m_scope->m_comboEditors[frame.editor].state.id;
            const b8 composing = frame.search != nullptr && frame.search->query().composition().active;
            if(ShouldIgnoreComboEditorAction(fromEditor, composing, action.kind, frame.editorSubmitted))
                return ListActionOutcome::Continue;
            ListResult preview;
            if(!ListBehavior::Apply(*frame.results, options, action, state.m_list, preview) || !m_builder.comboMatches(frame))
                return ListActionOutcome::Failed;
            if(!preview.activated)
                return ListActionOutcome::Continue;
            if(!ComboBehavior::Commit(state, *frame.source, state.m_list.selectedKey()))
                return ListActionOutcome::Failed;
            result.committed = true;
            m_builder.m_context.input().closePopup(frame.popupToken);
            return ListActionOutcome::Stop;
        });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ComboCoordinator::paintCombo(const Item& item, const LayoutBox& box){
    if(item.combo >= m_builder.m_scope->m_combos.size())
        return false;
    ComboFrame& frame = m_builder.m_scope->m_combos[item.combo];
    if(!m_builder.comboMatches(frame))
        return false;
    const InputRouter& input = m_builder.m_context.input();
    const b8 focused = input.focus() == item.state.id;
    const b8 hovered = input.hover() == item.state.id;
    const Rect clip = m_builder.visibleClip(box.clip);
    if(!ControlVisualPainter().paintComboFieldVisual(
        m_builder.m_paint,
        m_builder.m_text,
        *m_builder.m_skin,
        frame.style,
        item.style,
        item.text,
        item.textBounds,
        item.padding,
        frame.arrowExtent,
        box.rectangle,
        clip,
        item.enabled,
        frame.open,
        focused,
        hovered
    ) || !m_builder.comboMatches(frame))
        return false;
    const HitTarget target = HitTargetFactory().makeComboFieldHitTarget(box.rectangle, clip, item.enabled, item.contextMenu, frame.token, frame.focusOnCommit);
    if(!m_builder.m_context.addTarget(item.state, target))
        return false;
    frame.state->m_bounds = box.rectangle;
    frame.visibleField = IntersectUiRectsBounds(box.rectangle, clip);
    return true;
}

b8 ComboCoordinator::paintCombos(){
    for(auto& frame : m_builder.m_scope->m_combos){
        if(!m_builder.comboMatches(frame) || frame.list >= m_builder.m_scope->m_lists.size())
            return false;
        if(frame.open && frame.options.enabled && !paintComboPopup(frame))
            return false;
    }
    return true;
}

b8 ComboCoordinator::paintComboPopup(ComboFrame& frame){
    const UiSkinRegion* background = m_builder.region(frame.popupStyle.background, frame.popupStyle.fallback);
    if(!background || !m_builder.comboMatches(frame))
        return false;
    const Rect& anchor = frame.state->bounds();
    if(frame.visibleField.width <= 0.0f || frame.visibleField.height <= 0.0f){
        m_builder.m_context.discardPopupScope(frame.popupToken);
        ComboBehavior::Close(*frame.state);
        frame.open = false;
        if(frame.search){
            frame.search->m_editor.focused = false;
            frame.search->query().cancelComposition();
            snapshotComboQuery(frame);
        }
        frame.listToken.instanceGeneration = frame.state->m_list.inputGeneration();
        m_builder.m_scope->m_lists[frame.list].token = frame.listToken;
        return true;
    }
    PopupOptions options;
    options.anchor = anchor;
    options.size = { anchor.width, frame.options.popupHeight };
    const auto placement = PlacePopup(options, m_builder.m_paint.displayMetrics());
    if(!placement)
        return false;
    const Insets padding = MaxUiPadding(frame.popupStyle.padding, background->padding);
    const PopupScope scope = UiScopeDescriptorFactory().comboPopup(
        frame.popupToken,
        frame.parentToken,
        *placement,
        frame.state->m_owner,
        frame.state->m_ownerDeclaration
    );
    if(!m_builder.m_context.updatePopupScope(frame.popupToken, scope) || !m_builder.m_context.activatePopupScope(frame.popupToken))
        return false;
    if(!m_builder.m_paint.beginOverlay(m_builder.m_context.popupLayer())){
        const b8 ended = m_builder.m_context.endPopupScope(false);
        if(!ended)
            m_builder.m_context.fail();
        return false;
    }
    m_builder.m_paint.pushClip(placement->viewport);
    const b8 backgroundPainted = m_builder.m_paint.drawRegion(background->name, placement->bounds);
    const b8 clipPopped = m_builder.m_paint.popClip();
    const HitTarget barrier = HitTargetFactory().makeBarrierHitTarget(placement->bounds, placement->viewport);
    b8 painted = backgroundPainted && clipPopped && m_builder.m_context.addTarget(frame.popup, barrier);
    if(painted){
        Item listItem(m_builder.m_arena);
        listItem.state = frame.rows;
        listItem.list = frame.list;
        LayoutBox box;
        box.rectangle = InsetUiRectBounds(placement->bounds, padding);
        box.clip = placement->bounds;
        painted = (!frame.search || paintComboQuery(frame, box)) && m_builder.paintList(listItem, box) && m_builder.comboMatches(frame);
    }
    const b8 overlayEnded = m_builder.m_paint.endOverlay();
    const b8 scopeEnded = m_builder.m_context.endPopupScope(painted);
    if(painted)
        frame.state->m_popup.m_placement = *placement;
    return painted && overlayEnded && scopeEnded;
}

b8 Builder::paintCombo(const Item& item, const LayoutBox& box){
    return ComboCoordinator(*this).paintCombo(item, box);
}

b8 Builder::paintCombos(){
    return ComboCoordinator(*this).paintCombos();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ContextMenuResult ContextMenuCoordinator::declareMenu(const AStringView stableKey, const AStringView anchorKey, const IListDataSource& source, ContextMenuState& state, const ContextMenuOptions& options){
    ContextMenuResult result;
    Item* anchor = m_builder.annotationAnchor(anchorKey);
    if(m_builder.panelDeclarationBlocked() || !anchor || !IsValidContextMenuDeclarationOptions(options, m_builder.m_scope->m_contextMenus.size(), s_InputMaxPopups)){
        m_builder.m_context.fail();
        return result;
    }
    if(!m_builder.synchronizePopup()){
        result.valid = true;
        return result;
    }
    for(const auto& previous : m_builder.m_scope->m_contextMenus){
        if(previous.anchor.id == anchor->state.id){
            m_builder.m_context.fail();
            return result;
        }
    }
    m_builder.m_declaring = true;
    ScopeExit finish([this]()noexcept{ m_builder.m_declaring = false; });
    const auto widget = m_builder.m_context.declare(stableKey, WidgetKind::ContextMenu);
    if(!widget || !m_builder.m_context.claimState(**widget, state.instanceGeneration()))
        return result;
    ContextMenuFrame frame;
    frame.parentToken = m_builder.m_context.popupToken();
    frame.widget = **widget;
    frame.anchor = anchor->state;
    frame.anchorIndex = static_cast<u32>(anchor - m_builder.m_scope->m_items.data());
    frame.source = &source;
    frame.state = &state;
    frame.popupStyle = m_builder.m_popupStyle;
    frame.options = options;
    frame.options.enabled = options.enabled && anchor->enabled;
    const b8 previouslyOpen = state.isOpen();
    if(DidContextMenuBindingChange(
        (*widget)->id,
        (*widget)->declarationGeneration,
        anchor->state.id,
        anchor->state.declarationGeneration,
        state.m_owner,
        state.m_ownerDeclaration,
        state.m_anchorWidget,
        state.m_anchorDeclaration
    )){
        if(state.m_owner.valid())
            state.close();
        state.m_owner = (*widget)->id;
        state.m_ownerDeclaration = (*widget)->declarationGeneration;
        state.m_anchorWidget = anchor->state.id;
        state.m_anchorDeclaration = anchor->state.declarationGeneration;
        AdvanceNonWrappingRevision(state.m_revision);
    }
    const b8 boundOpen = state.isOpen();
    state.m_popup.bindParent(frame.parentToken);
    if(boundOpen && !state.isOpen())
        state.close();
    const auto popup = m_builder.m_context.declarePart(frame.widget, s_WidgetPartPopup, WidgetKind::Popup);
    if(!popup)
        return result;
    frame.popup = **popup;
    const auto rows = m_builder.m_context.declarePart(frame.widget, s_WidgetPartRows, WidgetKind::VirtualList);
    if(!rows)
        return result;
    frame.rows = **rows;
    const b8 previouslyListFocused = m_builder.m_context.input().focus() == frame.rows.id;
    frame.popupToken = { frame.popup.id, frame.popup.declarationGeneration, state.instanceGeneration(), state.m_popup.openGeneration() };
    frame.revision = state.revision();
    frame.open = state.isOpen();
    frame.listToken = { state.m_list.inputGeneration(), source.instanceGeneration(), source.revision() };
    frame.rowCount = source.rowCount();
    if(!m_builder.contextMenuMatches(frame)){
        m_builder.m_context.fail();
        return result;
    }
    const u64 previousGeneration = state.m_list.m_sourceGeneration;
    const u64 previousRevision = state.m_list.m_sourceRevision;
    if(!ListBehavior::Reconcile(state.m_list, source) || !m_builder.contextMenuMatches(frame)){
        m_builder.m_context.fail();
        return result;
    }
    if(previousGeneration != 0u && previousGeneration != frame.listToken.contentGeneration)
        state.close();
    else if(previousRevision != 0u && previousRevision != frame.listToken.contentRevision){
        state.m_list.select(0u);
        if(!state.m_list.scrollTo(0.0)){
            m_builder.m_context.fail();
            return result;
        }
    }
    snapshotContextMenu(frame);
    if(!prepareContextMenu(frame, result)){
        m_builder.m_context.fail();
        return result;
    }
    ListOptions listOptions;
    listOptions.wheelRows = options.wheelRows;
    listOptions.enabled = frame.options.enabled;
    listOptions.selectOnNavigate = false;
    const b8 focusOnCommit = frame.open && frame.options.enabled && previouslyListFocused && m_builder.m_context.input().focus() != frame.rows.id;
    ListFrame list(m_builder.m_arena);
    if(!m_builder.prepareListFrame(source, state.m_list, listOptions, frame.listToken, frame.rowCount, focusOnCommit, {}, list) || !m_builder.contextMenuMatches(frame)){
        m_builder.m_context.fail();
        return result;
    }
    frame.list = static_cast<u32>(m_builder.m_scope->m_lists.size());
    state.m_popup.bindParent(frame.parentToken);
    if(frame.open && !m_builder.reserveCompoundPopup(frame.popup, frame.popupToken)){
        m_builder.m_context.fail();
        return {};
    }
    m_builder.m_scope->m_lists.push_back(Move(list));
    anchor->contextMenu = frame.options.enabled;
    anchor->annotated = true;
    result.valid = true;
    result.opened = !previouslyOpen && state.isOpen();
    result.closed = previouslyOpen && !state.isOpen();
    m_builder.m_scope->m_contextMenus.push_back(frame);
    return result;
}

b8 ContextMenuCoordinator::prepareContextMenu(ContextMenuFrame& frame, ContextMenuResult& result){
    InputRouter& input = m_builder.m_context.input();
    ContextMenuState& state = *frame.state;
    input.fencePopup(frame.popupToken);
    if(input.consumePopupDismissal(frame.popupToken))
        state.close();
    while(auto trigger = input.consumeContextMenu(frame.anchor.id, frame.anchor.declarationGeneration)){
        if(frame.options.enabled && trigger->popup == m_builder.m_context.popupToken() && !state.open({ trigger->position.x, trigger->position.y, 0.0f, 0.0f }))
            return false;
    }
    if(!frame.options.enabled)
        state.close();
    snapshotContextMenu(frame);
    input.fencePopup(frame.popupToken);
    input.fenceControl(frame.rows.id, frame.rows.declarationGeneration, frame.listToken);
    if(!frame.open){
        input.closePopup(frame.popupToken);
        return m_builder.contextMenuMatches(frame);
    }
    if(!ListBehavior::Reconcile(state.m_list, *frame.source) || !m_builder.contextMenuMatches(frame))
        return false;
    if(state.m_list.cursorKey() == 0u){
        ControlAction first;
        first.control = frame.listToken;
        first.kind = ControlActionKind::Home;
        ListOptions options;
        options.wheelRows = frame.options.wheelRows;
        options.selectOnNavigate = false;
        ListResult preview;
        if(!ListBehavior::Apply(*frame.source, options, first, state.m_list, preview) || !m_builder.contextMenuMatches(frame))
            return false;
    }
    return applyContextMenuInput(frame, result);
}

b8 ContextMenuCoordinator::applyContextMenuInput(ContextMenuFrame& frame, ContextMenuResult& result){
    ListOptions options;
    options.wheelRows = frame.options.wheelRows;
    options.selectOnNavigate = false;
    return ListInput(m_builder.m_context).consume(
        frame.rows,
        frame.listToken,
        true,
        &frame.popupToken,
        frame.state->m_list,
        [&](){ return m_builder.contextMenuMatches(frame); },
        [&](const ControlAction& action){
            ListResult preview;
            if(!ListBehavior::Apply(*frame.source, options, action, frame.state->m_list, preview) || !m_builder.contextMenuMatches(frame))
                return ListActionOutcome::Failed;
            if(!preview.activated)
                return ListActionOutcome::Continue;
            result.activated = true;
            result.key = frame.state->m_list.selectedKey();
            frame.state->close();
            m_builder.m_context.input().closePopup(frame.popupToken);
            snapshotContextMenu(frame);
            return ListActionOutcome::Stop;
        });
}

void ContextMenuCoordinator::snapshotContextMenu(ContextMenuFrame& frame)noexcept{
    frame.revision = frame.state->revision();
    frame.open = frame.state->isOpen();
    frame.popupToken.openGeneration = frame.state->m_popup.openGeneration();
    frame.listToken.instanceGeneration = frame.state->m_list.inputGeneration();
    if(frame.list < m_builder.m_scope->m_lists.size())
        m_builder.m_scope->m_lists[frame.list].token = frame.listToken;
}

b8 ContextMenuCoordinator::paintMenus(){
    for(auto& frame : m_builder.m_scope->m_contextMenus){
        if(!m_builder.contextMenuMatches(frame) || frame.list >= m_builder.m_scope->m_lists.size() || frame.anchorIndex >= m_builder.m_scope->m_items.size())
            return false;
        if(frame.open && frame.options.enabled && !paintContextMenuPopup(frame))
            return false;
    }
    return true;
}

b8 ContextMenuCoordinator::paintContextMenuPopup(ContextMenuFrame& frame){
    const Item& anchor = m_builder.m_scope->m_items[frame.anchorIndex];
    const LayoutBox* anchorBox = m_builder.m_scope->m_layout.box(anchor.node);
    const UiSkinRegion* background = m_builder.region(frame.popupStyle.background, frame.popupStyle.fallback);
    if(!anchorBox || !background || !m_builder.contextMenuMatches(frame))
        return false;
    const Rect clip = m_builder.visibleClip(anchorBox->clip);
    if(!IsContextMenuAnchorVisible(anchorBox->rectangle, clip, anchor.enabled)){
        frame.state->close();
        m_builder.m_context.discardPopupScope(frame.popupToken);
        snapshotContextMenu(frame);
        return true;
    }
    PopupOptions options;
    options.anchor = frame.state->m_anchor;
    options.size = frame.options.size;
    options.gap = 0.0f;
    const auto placement = PlacePopup(options, m_builder.m_paint.displayMetrics());
    if(!placement)
        return false;
    const PopupScope scope = UiScopeDescriptorFactory().basicPopup(frame.popupToken, frame.parentToken, *placement);
    if(!m_builder.m_context.updatePopupScope(frame.popupToken, scope) || !m_builder.m_context.activatePopupScope(frame.popupToken))
        return false;
    if(!m_builder.m_paint.beginOverlay(m_builder.m_context.popupLayer())){
        const b8 ended = m_builder.m_context.endPopupScope(false);
        if(!ended)
            m_builder.m_context.fail();
        return false;
    }
    const Insets padding = MaxUiPadding(frame.popupStyle.padding, background->padding);
    m_builder.m_paint.pushClip(placement->bounds);
    const b8 backgroundPainted = m_builder.m_paint.drawRegion(background->name, placement->bounds);
    const b8 popped = m_builder.m_paint.popClip();
    const HitTarget barrier = HitTargetFactory().makeBarrierHitTarget(placement->bounds, placement->viewport);
    b8 painted = backgroundPainted && popped && m_builder.m_context.addTarget(frame.popup, barrier);
    if(painted){
        Item item(m_builder.m_arena);
        item.state = frame.rows;
        item.list = frame.list;
        LayoutBox box;
        box.rectangle = InsetUiRectBounds(placement->bounds, padding);
        box.clip = placement->bounds;
        painted = m_builder.paintList(item, box) && m_builder.contextMenuMatches(frame);
    }
    const b8 overlayEnded = m_builder.m_paint.endOverlay();
    const b8 scopeEnded = m_builder.m_context.endPopupScope(painted);
    if(painted)
        frame.state->m_popup.m_placement = *placement;
    return painted && overlayEnded && scopeEnded;
}

ContextMenuResult Builder::contextMenu(const AStringView stableKey, const AStringView anchorKey, const IListDataSource& source, ContextMenuState& state, const ContextMenuOptions& options){
    return ContextMenuCoordinator(*this).declareMenu(stableKey, anchorKey, source, state, options);
}

b8 Builder::contextMenuStateMatches(const ContextMenuFrame& frame)const noexcept{
    if(!frame.state)
        return false;
    return MatchesContextMenuFrameStateValues(
        popupAncestorsVisible(),
        frame.state->revision(),
        frame.revision,
        frame.state->isOpen(),
        frame.open,
        frame.state->m_popup.instanceGeneration(),
        frame.popupToken.instanceGeneration,
        frame.state->m_popup.openGeneration(),
        frame.popupToken.openGeneration,
        frame.state->m_popup.m_parent,
        frame.parentToken,
        frame.state->m_list.inputGeneration(),
        frame.listToken.instanceGeneration,
        frame.state->m_owner,
        frame.widget.id,
        frame.state->m_ownerDeclaration,
        frame.widget.declarationGeneration,
        frame.state->m_anchorWidget,
        frame.anchor.id,
        frame.state->m_anchorDeclaration,
        frame.anchor.declarationGeneration
    );
}

b8 Builder::contextMenuMatches(const ContextMenuFrame& frame)const{
    return
        frame.source && contextMenuStateMatches(frame)
        && frame.listToken.contentGeneration != 0u && frame.listToken.contentRevision != 0u && frame.source->instanceGeneration() == frame.listToken.contentGeneration && contextMenuStateMatches(frame)
        && frame.source->revision() == frame.listToken.contentRevision && contextMenuStateMatches(frame)
        && frame.source->rowCount() == frame.rowCount && contextMenuStateMatches(frame)
    ;
}

b8 Builder::paintContextMenus(){
    return ContextMenuCoordinator(*this).paintMenus();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


EditBoxResult EditBoxCoordinator::declareEditBox(const AStringView stableKey, const EditBoxOptions& options, EditModel& model, EditBoxState& state, IEditActionSink* actions, IntegerEditFrame* integerFrame, FloatEditFrame* floatFrame){
    if(m_builder.itemDeclarationBlocked() || model.textMode() != EditTextMode::SingleLine){
        m_builder.m_context.fail();
        return {};
    }
    if(!m_builder.synchronizePopup()){
        EditBoxResult result;
        result.valid = true;
        return result;
    }
    m_builder.m_declaring = true;
    ScopeExit finish([this]()noexcept{ m_builder.m_declaring = false; });
    const auto widget = m_builder.m_context.declare(stableKey, WidgetKind::EditBox);
    if(!widget)
        return {};
    if((integerFrame || floatFrame) && (!m_builder.numericStateAvailable(state) || !m_builder.m_context.claimState(**widget, model.instanceGeneration()))){
        m_builder.m_context.fail();
        return {};
    }
    if(!options.enabled)
        m_builder.m_context.input().invalidateTarget((*widget)->id);
    EditBoxResult result;
    result.valid = true;
    result.focused = options.enabled && m_builder.m_context.input().focus() == (*widget)->id;
    if(m_builder.m_editHost){
        result = actions ? m_builder.m_editHost->editActions(**widget, options, m_builder.m_context.popupToken(), model, *actions) : m_builder.m_editHost->edit(**widget, options, model);
    }
    if(!result.valid || m_builder.m_context.failed()){
        result.valid = false;
        m_builder.m_context.fail();
        return result;
    }
    const auto snapshotNumericFrame = [&](auto* const frame)noexcept{
        if(!frame)
            return true;
        if(!frame->model || frame->model->revision() != frame->revision)
            return false;
        frame->draftStamp = model.stamp();
        frame->focused = result.focused;
        return true;
    };
    if(!snapshotNumericFrame(integerFrame) || !snapshotNumericFrame(floatFrame)){
        m_builder.m_context.fail();
        result.valid = false;
        return result;
    }
    Item item(m_builder.m_arena);
    item.style = m_builder.m_style;
    item.editStyle = m_builder.m_editStyle;
    item.state = **widget;
    item.editOptions = options;
    if(!prepareEditBox(result, model, state, item)){
        result.valid = false;
        return result;
    }
    if((integerFrame && !m_builder.numericEditFrameMatches(*integerFrame)) || (floatFrame && !m_builder.numericEditFrameMatches(*floatFrame))){
        m_builder.m_context.fail();
        result.valid = false;
        return result;
    }
    if(integerFrame)
        item.integerEdit = static_cast<u32>(m_builder.m_scope->m_integerEdits.size());
    if(floatFrame)
        item.floatEdit = static_cast<u32>(m_builder.m_scope->m_floatEdits.size());
    LayoutNodeDesc description;
    description.width = options.width;
    description.height = options.height;
    description.intrinsicSize = ResolveEditIntrinsicSize(m_builder.loadEditMinimumSize(), item.editView.visualBounds().height, item.padding);
    if(!m_builder.addItemNode(description, item)){
        result.valid = false;
        return result;
    }
    m_builder.m_scope->m_items.push_back(Move(item));
    return result;
}

b8 EditBoxCoordinator::prepareEditBox(const EditBoxResult& result, EditModel& model, EditBoxState& state, Item& item){
    state.updateFromModel(model.stamp(), result.focused, m_builder.m_deltaSeconds);

    item.editFlags = { item.editOptions.enabled, m_builder.m_context.input().hover() == item.state.id, result.focused, item.editOptions.readOnly, state.caretElapsed < s_CaretVisiblePhaseEnd };
    item.editFlags.preeditCaretVisible = result.preeditCaretVisible;
    if(!item.editView.snapshot(model) || item.editView.shape(m_builder.m_text, m_builder.textShapeRequest({}, m_builder.m_style.fontSize)) != TextLayoutStatus::Success || !item.editView.measure(m_builder.m_text, m_builder.m_paint.displayMetrics())){
        m_builder.m_context.fail();
        return false;
    }
    item.editState = &state;
    item.padding = m_builder.m_skin ? ControlStyleMetrics().editPadding(*m_builder.m_skin, item.editStyle) : item.editStyle.padding;
    return true;
}

b8 EditBoxCoordinator::paintEditBox(const Item& item, const LayoutBox& box, const HitTarget* navigation){
    if(!m_builder.numericEditMatches(item))
        return false;
    const f32 caretWidth = 1.0f / m_builder.m_paint.displayMetrics().pixelScaleX;
    const auto arranged = item.editView.arrange(box.rectangle, item.padding, m_builder.visibleClip(box.clip), { item.editState->scroll, 0.0f }, caretWidth);
    if(!arranged)
        return false;
    const EditBoxPlacement& placement = *arranged;
    if(!item.editView.paint(m_builder.m_text, m_builder.m_paint, *m_builder.m_skin, placement, item.editStyle, item.editFlags))
        return false;
    item.editState->placement = placement;
    item.editState->scroll = placement.scroll;
    const HitTarget target = HitTargetFactory().makeEditBoxHitTarget(
        navigation ? *navigation : HitTarget{},
        box.rectangle,
        m_builder.visibleClip(box.clip),
        item.editOptions.enabled,
        item.contextMenu
    );
    if(!m_builder.m_context.addTarget(item.state, target))
        return false;
    const b8 published = !m_builder.m_editHost || m_builder.m_editHost->publish(item.state, item.editView, placement, item.editOptions);
    return published && m_builder.numericEditMatches(item);
}

EditBoxResult Builder::declareEditBox(const AStringView stableKey, const EditBoxOptions& options, EditModel& model, EditBoxState& state, IEditActionSink* actions, IntegerEditFrame* integerFrame, FloatEditFrame* floatFrame){
    return EditBoxCoordinator(*this).declareEditBox(stableKey, options, model, state, actions, integerFrame, floatFrame);
}

Point Builder::loadEditMinimumSize()const noexcept{
    if(!m_skin)
        return Point{ s_StyleMinimumFieldWidth, 0.0f };
    return ControlStyleMetrics().editMinimumSize(*m_skin, m_editStyle);
}

b8 Builder::prepareEditBox(const EditBoxResult& result, EditModel& model, EditBoxState& state, Item& item){
    return EditBoxCoordinator(*this).prepareEditBox(result, model, state, item);
}

b8 Builder::paintEditBox(const Item& item, const LayoutBox& box, const HitTarget* navigation){
    return EditBoxCoordinator(*this).paintEditBox(item, box, navigation);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ListResult ListCoordinator::virtualList(const AStringView stableKey, const IListDataSource& source, ListState& state, const ListOptions& options){
    ListResult result;
    if(m_builder.itemDeclarationBlocked() || !IsFinite(options.wheelRows) || options.wheelRows <= 0.0f){
        m_builder.m_context.fail();
        return result;
    }
    // A body callback may have closed or reopened its popup; no model loan is allowed past that boundary.
    if(!m_builder.synchronizePopup()){
        result.valid = true;
        return result;
    }
    m_builder.m_declaring = true;
    ScopeExit finish([this]()noexcept{ m_builder.m_declaring = false; });
    const auto widget = m_builder.m_context.declare(stableKey, WidgetKind::VirtualList);
    if(!widget)
        return result;
    const b8 previouslyFocused = m_builder.m_context.input().focus() == (*widget)->id;
    const u64 previousSelection = state.selectedKey();
    if(!ListBehavior::Reconcile(state, source)){
        m_builder.m_context.fail();
        return result;
    }
    const ControlToken token{ state.inputGeneration(), source.instanceGeneration(), source.revision() };
    m_builder.m_context.input().fenceControl((*widget)->id, (*widget)->declarationGeneration, token);
    if(!options.enabled)
        m_builder.m_context.input().invalidateTarget((*widget)->id);
    result.valid = true;
    result.selectionChanged = previousSelection != state.selectedKey();
    result.focused = options.enabled && m_builder.m_context.input().focus() == (*widget)->id;
    if(!ListInput(m_builder.m_context).consume(
        **widget,
        token,
        options.enabled,
        nullptr,
        state,
        []()noexcept{ return true; },
        [&](const ControlAction& action){
            return ListBehavior::Apply(source, options, action, state, result) ? ListActionOutcome::Continue : ListActionOutcome::Failed;
        }
    )){
        m_builder.m_context.fail();
        result.valid = false;
        return result;
    }
    ListFrame frame(m_builder.m_arena);
    const UiSkinRegion* background = prepareListFrame(source, state, options, token, source.rowCount(), options.enabled && previouslyFocused && !result.focused, {}, frame);
    if(!background){
        m_builder.m_context.fail();
        result.valid = false;
        return result;
    }
    // A fractional offset or a skin minimum can expose more than one row in a natural viewport.
    // Finish that bounded measurement before its intrinsic height enters the parent layout.
    if(options.height.policy == LayoutSizePolicy::Content && frame.rowCount != 0u){
        for(;;){
            LayoutBox provisional;
            provisional.rectangle = ResolveListProbeRect(*background, frame.padding, frame.style.scrollbarWidth, frame.rowHeight);
            provisional.clip = provisional.rectangle;
            const auto placement = ListLayoutPreparer(m_builder).measureListPlacement(frame, provisional, provisional.clip);
            if(!placement){
                m_builder.m_context.fail();
                result.valid = false;
                return result;
            }
            const f32 height = ResolveListIntrinsicSize(*background, frame.padding, frame.rowHeight).y;
            if(height == placement->bounds.height)
                break;
        }
    }
    Item item(m_builder.m_arena);
    item.state = **widget;
    item.enabled = options.enabled;
    item.list = static_cast<u32>(m_builder.m_scope->m_lists.size());
    LayoutNodeDesc description;
    description.width = options.width;
    description.height = options.height;
    description.intrinsicSize = ResolveListIntrinsicSize(*background, frame.padding, frame.rowHeight);
    if(!m_builder.addItemNode(description, item)){
        result.valid = false;
        return result;
    }
    m_builder.m_scope->m_lists.push_back(Move(frame));
    m_builder.m_scope->m_items.push_back(Move(item));
    return result;
}

const UiSkinRegion* ListCoordinator::prepareListFrame(
    const IListDataSource& source, ListState& state, const ListOptions& options, const ControlToken& token,
    const u64 rowCount, const b8 focusOnCommit, const WidgetId commandFocus, ListFrame& outFrame
){
    outFrame.source = &source;
    outFrame.state = &state;
    outFrame.widgetStyle = m_builder.m_style;
    outFrame.style = m_builder.m_listStyle;
    outFrame.options = options;
    outFrame.token = token;
    outFrame.rowCount = rowCount;
    outFrame.focusOnCommit = focusOnCommit;
    outFrame.commandFocus = commandFocus;
    outFrame.padding = outFrame.style.padding;
    const UiSkinRegion* background = m_builder.region(outFrame.style.background, outFrame.style.backgroundFallback);
    if(!background)
        return nullptr;
    outFrame.padding = MaxUiPadding(outFrame.padding, background->padding);
    return prepareListMetrics(outFrame) ? background : nullptr;
}

b8 ListCoordinator::prepareListMetrics(ListFrame& frame){
    if(!m_builder.listMatches(frame) || !m_builder.m_skin)
        return false;
    const auto metrics = MeasureSelectable(*m_builder.m_skin, frame.style.row);
    if(!metrics)
        return false;
    frame.style.row.padding = metrics->padding;
    const auto keyResult = TryBuildListMeasureKey(
        frame.token.contentGeneration,
        frame.token.contentRevision,
        frame.rowCount,
        m_builder.m_text.identity(),
        m_builder.m_text.generation(),
        frame.widgetStyle.fontSize,
        m_builder.m_paint.displayMetrics(),
        *metrics,
        m_builder.m_textScriptTag,
        StringView(m_builder.m_textLanguage.data(), m_builder.m_textLanguage.size())
    );
    if(!keyResult)
        return false;
    const ListMeasureKey key = *keyResult;
    frame.measureKey = key;
    ListState& state = *frame.state;
    if(key != state.m_measureKey || state.m_rowHeight <= 0.0f){
        auto empty = m_builder.m_text.layout(m_builder.textShapeRequest({}, key.fontSize));
        if(!empty)
            return false;
        const auto bounds = m_builder.m_text.measure(*empty, m_builder.m_paint.displayMetrics());
        if(!bounds)
            return false;
        const f32 height = ResolveListRowBaseHeight(*metrics, bounds->height);
        if(!IsFinite(height) || height <= 0.0f || !m_builder.listMatches(frame))
            return false;
        state.m_measureKey = key;
        state.m_rowHeight = height;
    }
    // The last painted offset may belong to a different styled declaration than the measurement cache.
    if(state.m_scrollRowHeight > 0.0f && state.m_scrollSourceGeneration == key.sourceGeneration && state.m_scrollRowHeight != state.m_rowHeight){
        const f64 offset = ComputeListRescaledScrollOffset(
            state.m_placement.contentClip.y,
            state.m_placement.viewport.y,
            state.scrollOffset(),
            state.m_scrollRowHeight,
            state.m_rowHeight
        );
        if(!state.m_scroll.setOffset(offset))
            return false;
    }
    state.m_scrollRowHeight = state.m_rowHeight;
    state.m_scrollSourceGeneration = key.sourceGeneration;
    frame.rowHeight = state.m_rowHeight;
    frame.scrollOffset = state.scrollOffset();
    frame.cursorKey = state.cursorKey();
    frame.revealCursor = state.m_ensureCursor;
    return m_builder.listMatches(frame);
}

ListResult Builder::virtualList(const AStringView stableKey, const IListDataSource& source, ListState& state, const ListOptions& options){
    return ListCoordinator(*this).virtualList(stableKey, source, state, options);
}

const UiSkinRegion* Builder::prepareListFrame(const IListDataSource& source, ListState& state, const ListOptions& options, const ControlToken& token, const u64 rowCount, const b8 focusOnCommit, const WidgetId commandFocus, ListFrame& outFrame){
    return ListCoordinator(*this).prepareListFrame(source, state, options, token, rowCount, focusOnCommit, commandFocus, outFrame);
}

b8 Builder::listMatches(const ListFrame& frame)const{
    return
        frame.source && listStateMatches(frame)
        && frame.source->instanceGeneration() == frame.token.contentGeneration && listStateMatches(frame)
        && frame.source->revision() == frame.token.contentRevision && listStateMatches(frame)
        && frame.source->rowCount() == frame.rowCount
        && listStateMatches(frame)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<ListLayoutPreparer::ListRow> ListLayoutPreparer::prepareListRow(const ListFrame& frame, const u64 index){
    ListRow row(m_builder.m_arena);
    if(!m_builder.listMatches(frame) || index >= frame.rowCount)
        return MakeUnexpected(Failure{});
    const u64 key = frame.source->key(index);
    if(key == 0u || !m_builder.listStateMatches(frame))
        return MakeUnexpected(Failure{});
    const auto resolved = frame.source->indexOf(key);
    if(!resolved || *resolved != index || !m_builder.listStateMatches(frame))
        return MakeUnexpected(Failure{});
    const b8 enabled = frame.options.enabled && frame.source->enabled(index);
    if(!m_builder.listStateMatches(frame))
        return MakeUnexpected(Failure{});
    const ShapeRequest request{ frame.source->text(index), StringView(reinterpret_cast<const char*>(frame.measureKey.language.data())), frame.widgetStyle.fontSize, frame.measureKey.scriptTag, TextDirection::LeftToRight };
    if(!m_builder.listStateMatches(frame))
        return MakeUnexpected(Failure{});
    // The source owns a temporary view; shaping must copy it before any further source callback.
    auto layout = m_builder.m_text.layout(request);
    if(!layout || !m_builder.listMatches(frame))
        return MakeUnexpected(Failure{});
    const auto bounds = m_builder.m_text.measure(*layout, m_builder.m_paint.displayMetrics());
    if(!bounds)
        return MakeUnexpected(Failure{});
    row.index = index;
    row.key = key;
    row.enabled = enabled;
    row.textBounds = *bounds;
    row.text = Move(*layout);
    if(!m_builder.listMatches(frame))
        return MakeUnexpected(Failure{});
    return row;
}

Expected<ScrollPlacement> ListLayoutPreparer::measureListPlacement(ListFrame& frame, const LayoutBox& box, const Rect& clip){
    auto& rows = frame.rows;
    ListState& state = *frame.state;
    ScrollState scroll;
    if(!m_builder.listMatches(frame) || !scroll.setOffset(frame.scrollOffset))
        return MakeUnexpected(Failure{});
    const b8 revealCursor = frame.revealCursor && frame.cursorKey != 0u;
    u64 cursorIndex = 0u;
    if(revealCursor){
        const auto resolved = frame.source->indexOf(frame.cursorKey);
        if(!resolved || *resolved >= frame.rowCount || !m_builder.listStateMatches(frame))
            return MakeUnexpected(Failure{});
        cursorIndex = *resolved;
        if(frame.source->key(cursorIndex) != frame.cursorKey || !m_builder.listStateMatches(frame) || !frame.source->enabled(cursorIndex) || !m_builder.listMatches(frame))
            return MakeUnexpected(Failure{});
    }
    for(;;){
        auto candidate = CalculateScrollPlacement(box.rectangle, clip, frame.padding, frame.style.scrollbarWidth, frame.style.minimumThumb, frame.rowCount, frame.rowHeight, scroll.offset());
        if(!candidate)
            return MakeUnexpected(Failure{});
        if(revealCursor){
            const f64 start = static_cast<f64>(cursorIndex) * static_cast<f64>(frame.rowHeight);
            const f64 end = start + static_cast<f64>(frame.rowHeight);
            if(!IsFinite(start) || !IsFinite(end) || end <= start || !scroll.ensureVisible(start, end, candidate->viewport.height))
                return MakeUnexpected(Failure{});
        }
        if(revealCursor)
            candidate = CalculateScrollPlacement(box.rectangle, clip, frame.padding, frame.style.scrollbarWidth, frame.style.minimumThumb, frame.rowCount, frame.rowHeight, scroll.offset());
        if(!candidate || candidate->endRow - candidate->firstRow > s_ListMaxPreparedRows)
            return MakeUnexpected(Failure{});
        f32 height = frame.rowHeight;
        for(u64 index = candidate->firstRow; index < candidate->endRow; ++index){
            const auto found = LowerBound(rows.begin(), rows.end(), index, [](const ListRow& row, const u64 value)noexcept{ return row.index < value; });
            if(found != rows.end() && found->index == index)
                continue;
            // The work bound is independent of dataset size, even when a cursor reveal introduces a new interval.
            if(rows.size() >= s_ListMaxPreparedRows)
                return MakeUnexpected(Failure{});
            auto row = prepareListRow(frame, index);
            if(!row)
                return MakeUnexpected(Failure{});
            height = ComputeListRowPaddedHeight(row->textBounds.height, frame.style.row.padding, height);
            rows.insert(found, Move(*row));
        }
        if(!IsFinite(height) || height <= 0.0f || !m_builder.listMatches(frame))
            return MakeUnexpected(Failure{});
        if(height == frame.rowHeight){
            frame.scrollOffset = candidate->offset;
            // Another declaration can share this state with a different sizing context.
            if(state.m_measureKey == frame.measureKey)
                state.m_rowHeight = Max(state.m_rowHeight, frame.rowHeight);
            return *candidate;
        }
        // Retain the first visible row and its pixel offset at the inherited clip edge while the uniform estimate grows.
        const f64 offset = ComputeListExpandedScrollOffset(
            candidate->contentClip.y,
            candidate->viewport.y,
            candidate->offset,
            candidate->firstRow,
            frame.rowHeight,
            height
        );
        if(!scroll.setOffset(offset))
            return MakeUnexpected(Failure{});
        frame.rowHeight = height;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ListCoordinator::paintList(const Item& item, const LayoutBox& box){
    if(item.list >= m_builder.m_scope->m_lists.size())
        return false;
    ListFrame& frame = m_builder.m_scope->m_lists[item.list];
    if(!m_builder.listMatches(frame))
        return false;
    const Rect clip = m_builder.visibleClip(box.clip);
    const auto measuredPlacement = ListLayoutPreparer(m_builder).measureListPlacement(frame, box, clip);
    if(!measuredPlacement)
        return false;
    const ScrollPlacement& placement = *measuredPlacement;
    if(!frame.state->m_scroll.setOffset(frame.scrollOffset))
        return false;
    frame.state->m_scrollRowHeight = frame.rowHeight;
    frame.state->m_scrollSourceGeneration = frame.token.contentGeneration;
    if(frame.revealCursor && frame.state->cursorKey() == frame.cursorKey)
        frame.state->m_ensureCursor = false;
    frame.state->m_placement = placement;
    const UiSkinRegion* background = m_builder.region(frame.style.background, frame.style.backgroundFallback);
    if(!background)
        return false;
    m_builder.m_paint.pushClip(clip);
    const b8 painted = m_builder.m_paint.drawRegion(background->name, box.rectangle);
    const b8 popped = m_builder.m_paint.popClip();
    if(!painted || !popped)
        return false;
    const HitTarget host = HitTargetFactory().makeListHostHitTarget(
        box.rectangle,
        clip,
        item.enabled,
        item.contextMenu,
        frame.token,
        frame.rowHeight,
        frame.options.wheelRows,
        placement,
        frame.rowCount,
        frame.focusOnCommit
    );
    if(!m_builder.m_context.addTarget(item.state, host) || !paintListRows(item, frame, placement))
        return false;
    // Paint owns its glyph pages now; keep no shaped rows alive through later sibling lists.
    frame.rows.clear();
    if(placement.scrollbarVisible){
        const auto thumbId = TryMakeWidgetId(item.state.id, s_WidgetPartScrollbar);
        if(!thumbId)
            return false;
        const b8 hover = m_builder.m_context.input().hover() == *thumbId;
        if(!ControlVisualPainter().paintListScrollbarVisual(m_builder.m_paint, *m_builder.m_skin, frame.style, placement, clip, hover))
            return false;
        if(item.enabled){
            const HitTarget trackTarget = HitTargetFactory().makeListScrollbarTrackHitTarget(placement, clip, frame.token);
            const auto trackId = TryMakeWidgetId(item.state.id, s_WidgetPartTrack);
            if(!trackId || !m_builder.m_context.addPartTarget(item.state, *trackId, trackTarget))
                return false;
            const HitTarget thumbTarget = HitTargetFactory().makeListScrollbarThumbHitTarget(placement, clip, frame.token);
            if(!m_builder.m_context.addPartTarget(item.state, *thumbId, thumbTarget))
                return false;
        }
    }
    return true;
}

b8 ListCoordinator::paintListRows(const Item& item, const ListFrame& frame, const ScrollPlacement& placement){
    const auto& rows = frame.rows;
    const auto rowsId = TryMakeWidgetId(item.state.id, s_WidgetPartRows);
    if(!rowsId)
        return false;
    const b8 focused = m_builder.m_context.input().focus() == item.state.id || (frame.commandFocus.valid() && m_builder.m_context.input().focus() == frame.commandFocus);
    auto found = LowerBound(rows.begin(), rows.end(), placement.firstRow, [](const ListRow& row, const u64 value)noexcept{ return row.index < value; });
    for(u64 index = placement.firstRow; index < placement.endRow; ++index, ++found){
        if(found == rows.end() || found->index != index || !m_builder.listStateMatches(frame))
            return false;
        const ListRow& row = *found;
        const u64 key = row.key;
        const b8 enabled = item.enabled && row.enabled;
        const auto part = TryMakeWidgetPartId(*rowsId, key);
        if(!part)
            return false;
        const auto rectangle = CalculateScrollRowBounds(index, placement, frame.rowHeight);
        if(!rectangle)
            return false;
        const SelectablePaintFlags flags{ enabled, frame.state->selectedKey() == key, m_builder.m_context.input().hover() == *part, focused && frame.state->cursorKey() == key };
        if(!PaintSelectable(m_builder.m_paint, m_builder.m_text, *m_builder.m_skin, row.text, row.textBounds, frame.style.row.padding, *rectangle, placement.contentClip, frame.style.row, flags, frame.widgetStyle.text, frame.widgetStyle.disabledText))
            return false;
        if(item.enabled){
            const HitTarget target = HitTargetFactory().makeListRowHitTarget(*rectangle, placement.contentClip, enabled, frame.token, key);
            if(!m_builder.m_context.addPartTarget(item.state, *part, target))
                return false;
        }
    }
    return m_builder.listMatches(frame);
}

b8 Builder::paintList(const Item& item, const LayoutBox& box){
    return ListCoordinator(*this).paintList(item, box);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_builder_numeric_edit{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <typename Model, typename Bounds>
class NumericActions final : public IEditActionSink{
public:
    NumericActions(const Bounds& bounds, const Context& context, Model& model, NumericEditResult& result, u64& revision)noexcept
        : m_model(model)
        , m_bounds(bounds)
        , m_result(result)
        , m_revision(revision)
        , m_context(context)
    {}
    virtual ~NumericActions()noexcept override = default;


public:
    [[nodiscard]] virtual b8 apply(EditModel& draft, const EditAction::Enum action, const b8 readOnly)noexcept(
        noexcept(static_cast<b8>(DeclVal<const Context&>().failed() || &draft != &DeclVal<Model&>().draft() || DeclVal<Model&>().revision() != DeclVal<u64&>()))
        && noexcept(DeclVal<NumericEditResult&>() = DeclVal<Model&>().submit(DeclVal<const Bounds&>()))
        && noexcept(DeclVal<NumericEditResult&>() = DeclVal<Model&>().cancel())
        && noexcept(DeclVal<NumericEditResult&>() = DeclVal<Model&>().abandon())
        && noexcept(DeclVal<NumericEditResult&>() = DeclVal<Model&>().blur(DeclVal<const Bounds&>()))
        && noexcept(DeclVal<NumericEditResult&>() = (readOnly ? DeclVal<Model&>().abandon() : DeclVal<Model&>().blur(DeclVal<const Bounds&>())))
        && noexcept(DeclVal<u64&>() = DeclVal<Model&>().revision())
    )override{
        if(m_context.failed() || &draft != &m_model.draft() || m_model.revision() != m_revision){
            m_result.valid = false;
            return false;
        }
        NumericEditResult result;
        switch(action){
        case EditAction::Submit:
            if(readOnly)
                result.valid = true;
            else
                result = m_model.submit(m_bounds);
            break;
        case EditAction::Cancel: result = m_model.cancel(); break;
        case EditAction::Blur: result = readOnly ? m_model.abandon() : m_model.blur(m_bounds); break;
        case EditAction::Abandon: result = m_model.abandon(); break;
        default: break;
        }
        m_revision = m_model.revision();
        MergeNumericEditResult(result, m_result);
        return result.valid && !m_context.failed();
    }


private:
    Model& m_model;
    const Bounds& m_bounds;
    NumericEditResult& m_result;
    u64& m_revision;
    const Context& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename FrameT, typename ModelT, typename OptionsT, typename ValidateBoundsF, typename FrameListT>
NumericEditBoxResult NumericEditCoordinator::declareNumericEdit(const AStringView stableKey, ModelT& model, EditBoxState& state, const OptionsT& options, const ValidateBoundsF validateBounds, FrameListT& frames){
    NumericEditBoxResult result;
    result.numeric.valid = true;
    if(m_builder.declarationBlocked() || !validateBounds(options.bounds)){
        m_builder.m_context.fail();
        result.numeric.valid = false;
        return result;
    }
    FrameT frame;
    frame.model = &model;
    frame.state = &state;
    frame.revision = model.revision();
    __hidden_builder_numeric_edit::NumericActions actions(options.bounds, m_builder.m_context, model, result.numeric, frame.revision);
    const usize previousCount = m_builder.m_scope->m_items.size();
    if constexpr(IsSame_V<FrameT, IntegerEditFrame>)
        result.edit = m_builder.declareEditBox(stableKey, options.edit, model.lendDraft(), state, &actions, &frame);
    else
        result.edit = m_builder.declareEditBox(stableKey, options.edit, model.lendDraft(), state, &actions, nullptr, &frame);
    result.numeric.valid &= result.edit.valid;
    if(result.edit.valid && m_builder.m_scope->m_items.size() > previousCount){
        frame.item = static_cast<u32>(previousCount);
        frames.push_back(frame);
    }
    return result;
}

NumericEditBoxResult NumericEditCoordinator::integerEdit(const AStringView stableKey, IntegerEditModel& model, EditBoxState& state, const IntegerEditOptions& options){
    return declareNumericEdit<IntegerEditFrame>(stableKey, model, state, options, ValidateIntegerBounds, m_builder.m_scope->m_integerEdits);
}

NumericEditBoxResult NumericEditCoordinator::floatEdit(const AStringView stableKey, FloatEditModel& model, EditBoxState& state, const FloatEditOptions& options){
    return declareNumericEdit<FloatEditFrame>(stableKey, model, state, options, ValidateFloatBounds, m_builder.m_scope->m_floatEdits);
}

NumericEditBoxResult Builder::integerEdit(const AStringView stableKey, IntegerEditModel& model, EditBoxState& state, const IntegerEditOptions& options){
    return NumericEditCoordinator(*this).integerEdit(stableKey, model, state, options);
}

NumericEditBoxResult Builder::floatEdit(const AStringView stableKey, FloatEditModel& model, EditBoxState& state, const FloatEditOptions& options){
    return NumericEditCoordinator(*this).floatEdit(stableKey, model, state, options);
}

b8 Builder::numericDraftMatches(const NumericEditFrame& frame, const EditModel& draft)const noexcept{
    return
        popupAncestorsVisible() && frame.state
        && draft.matchesStamp(frame.draftStamp)
        && frame.state->matchesStamp(frame.draftStamp, frame.focused)
    ;
}

b8 Builder::numericEditMatches(const Item& item)const noexcept{
    if(item.integerEdit != s_LayoutNoParent){
        if(item.integerEdit >= m_scope->m_integerEdits.size() || !numericEditFrameMatches(m_scope->m_integerEdits[item.integerEdit]))
            return false;
    }
    if(item.floatEdit != s_LayoutNoParent){
        if(item.floatEdit >= m_scope->m_floatEdits.size() || !numericEditFrameMatches(m_scope->m_floatEdits[item.floatEdit]))
            return false;
    }
    return true;
}

b8 Builder::numericStateAvailable(const EditBoxState& state)const noexcept{
    for(usize index = 0u; index <= m_popupFrameCount; ++index){
        const BuilderScopeFrame& scope = index == 0u ? m_scopeFrame : *m_popupFrames[index - 1u];
        for(const auto& frame : scope.m_integerEdits){
            if(frame.state == &state)
                return false;
        }
        for(const auto& frame : scope.m_floatEdits){
            if(frame.state == &state)
                return false;
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_builder_text_area{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class TextAreaActions final : public IEditActionSink{
public:
    TextAreaActions(const EditModel& model, const TextAreaState& state, const Context& context)noexcept
        : m_model(model)
        , m_state(state)
        , m_context(context)
        , m_instanceGeneration(model.instanceGeneration())
        , m_revision(state.revision())
    {}
    virtual ~TextAreaActions()noexcept override = default;


public:
    [[nodiscard]] virtual b8 apply(EditModel& model, const EditAction::Enum action, const b8)noexcept override{
        if(m_context.failed() || &model != &m_model || model.instanceGeneration() != m_instanceGeneration || m_state.revision() != m_revision)
            return false;
        return action <= EditAction::Abandon;
    }


private:
    const EditModel& m_model;
    const TextAreaState& m_state;
    const Context& m_context;
    const u64 m_instanceGeneration;
    const u64 m_revision;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


EditBoxResult TextAreaCoordinator::declareTextArea(const AStringView stableKey, EditModel& model, TextAreaState& state, const TextAreaOptions& options){
    if(m_builder.itemDeclarationBlocked() || !IsFinite(options.wheelLines) || options.wheelLines <= 0.0f || model.textMode() != EditTextMode::Multiline){
        m_builder.m_context.fail();
        return {};
    }
    if(!m_builder.synchronizePopup()){
        EditBoxResult result;
        result.valid = true;
        return result;
    }
    m_builder.m_declaring = true;
    ScopeExit finish([this]()noexcept{ m_builder.m_declaring = false; });
    const auto widget = m_builder.m_context.declare(stableKey, WidgetKind::TextArea);
    if(!widget)
        return {};
    if(!textAreaAvailable(model, state) || !m_builder.m_context.claimState(**widget, model.instanceGeneration())){
        m_builder.m_context.fail();
        return {};
    }
    if(!options.enabled)
        m_builder.m_context.input().invalidateTarget((*widget)->id);
    const u64 stateRevision = state.revision();
    const u64 modelGeneration = model.instanceGeneration();
    const EditBoxOptions editOptions{ options.width, options.height, options.enabled, options.readOnly };
    EditBoxResult result;
    result.valid = true;
    result.focused = options.enabled && m_builder.m_context.input().focus() == (*widget)->id;
    if(m_builder.m_editHost){
        TextAreaNavigationResolver navigation(m_builder.m_arena, m_builder.m_text, m_builder.m_context, state, m_builder.m_style.fontSize, m_builder.m_textScriptTag, StringView(m_builder.m_textLanguage.data(), m_builder.m_textLanguage.size()));
        __hidden_builder_text_area::TextAreaActions actions(model, state, m_builder.m_context);
        result = m_builder.m_editHost->editNavigated(**widget, editOptions, m_builder.m_context.popupToken(), model, state.m_navigation, navigation, actions);
    }
    if(!result.valid || m_builder.m_context.failed() || state.revision() != stateRevision || model.instanceGeneration() != modelGeneration){
        result.valid = false;
        m_builder.m_context.fail();
        return result;
    }
    const Point requestedScroll = state.scroll();
    b8 firstBinding = false;
    ReconcileTextAreaVisualScrollState(
        modelGeneration,
        model.revision(),
        model.selectionGeneration(),
        model.compositionGeneration(),
        model.anchor(),
        model.caret(),
        result.focused,
        m_builder.m_context.input().capture(),
        (*widget)->id,
        state.m_compositionGeneration,
        state.m_visual.modelGeneration,
        state.m_visual.revision,
        state.m_visual.selectionGeneration,
        state.m_visual.anchor,
        state.m_visual.caret,
        state.m_visual.focused,
        state.m_visual.scroll,
        state.m_scrollY,
        state.m_revealCaret,
        state.m_visual.caretElapsed,
        firstBinding
    );
    TextAreaFrame frame;
    frame.wheelLines = options.wheelLines;
    frame.model = &model;
    frame.state = &state;
    frame.focused = result.focused;
    snapshotTextArea(frame);
    Item item(m_builder.m_arena);
    item.style = m_builder.m_style;
    item.editStyle = m_builder.m_editStyle;
    item.scrollbarStyle = m_builder.m_scrollbarStyle;
    item.state = **widget;
    item.editOptions = editOptions;
    if(!m_builder.prepareEditBox(result, model, state.m_visual, item) || !m_builder.textAreaMatches(frame)){
        result.valid = false;
        m_builder.m_context.fail();
        return result;
    }
    if(firstBinding && !state.m_revealCaret){
        state.m_visual.scroll = requestedScroll.x;
        state.m_scrollY = requestedScroll.y;
    }
    state.m_compositionGeneration = model.compositionGeneration();
    item.textArea = static_cast<u32>(m_builder.m_scope->m_textAreas.size());
    LayoutNodeDesc description;
    description.width = options.width;
    description.height = options.height;
    description.intrinsicSize = ResolveEditIntrinsicSize(m_builder.loadEditMinimumSize(), item.editView.visualBounds().height, item.padding);
    if(!m_builder.addItemNode(description, item)){
        result.valid = false;
        return result;
    }
    frame.item = static_cast<u32>(m_builder.m_scope->m_items.size());
    m_builder.m_scope->m_items.push_back(Move(item));
    m_builder.m_scope->m_textAreas.push_back(frame);
    return result;
}

b8 TextAreaCoordinator::textAreaAvailable(const EditModel& model, const TextAreaState& state)const noexcept{
    for(usize index = 0u; index <= m_builder.m_popupFrameCount; ++index){
        const BuilderScopeFrame& scope = index == 0u ? m_builder.m_scopeFrame : *m_builder.m_popupFrames[index - 1u];
        for(const auto& frame : scope.m_textAreas){
            if(frame.model == &model || frame.state == &state)
                return false;
        }
    }
    return true;
}

void TextAreaCoordinator::snapshotTextArea(TextAreaFrame& frame)noexcept{
    frame.stateRevision = frame.state->revision();
    frame.navigation = frame.state->navigation().snapshot();
    frame.modelStamp = frame.model->stamp();
}

EditBoxResult Builder::textArea(const AStringView stableKey, EditModel& model, TextAreaState& state, const TextAreaOptions& options){
    return TextAreaCoordinator(*this).declareTextArea(stableKey, model, state, options);
}

b8 Builder::textAreaMatches(const TextAreaFrame& frame)const noexcept{
    return
        popupAncestorsVisible() && frame.model && frame.state
        && frame.state->revision() == frame.stateRevision && frame.state->navigation().matches(frame.navigation)
        && frame.model->matchesStamp(frame.modelStamp)
        && frame.state->m_visual.matchesStamp(frame.modelStamp, frame.focused)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 TextAreaCoordinator::paintTextArea(const Item& item, const LayoutBox& box){
    if(item.textArea >= m_builder.m_scope->m_textAreas.size())
        return false;
    const TextAreaFrame& frame = m_builder.m_scope->m_textAreas[item.textArea];
    if(!m_builder.textAreaMatches(frame))
        return false;
    TextAreaState& state = *frame.state;
    const Rect clip = m_builder.visibleClip(box.clip);
    const f32 caretWidth = 1.0f / m_builder.m_paint.displayMetrics().pixelScaleX;
    const Rect& visualBounds = item.editView.visualBounds();
    const auto viewport = CalculateScrollViewportPlacement(box.rectangle, clip, item.padding, { visualBounds.width, visualBounds.height }, caretWidth, state.scroll(), item.scrollbarStyle.thickness, item.scrollbarStyle.minimumThumb);
    if(!viewport)
        return false;
    const f32 lineHeight = item.editView.layout().lines().empty() ? item.editView.layout().fontSize() : item.editView.layout().lines().front().height;
    const auto stepResult = TryResolveTextAreaScrollStep(item.editView.layout().fontSize(), lineHeight, frame.wheelLines);
    if(!stepResult)
        return false;
    const Point step = *stepResult;
    const ControlToken token = state.m_scrollInput.prepare(item.state, m_builder.m_context.popupToken(), state.instanceGeneration(), state.revision(), *frame.model, item.editOptions.enabled, item.editOptions.readOnly, *viewport, step);
    m_builder.m_context.input().fenceControl(item.state.id, item.state.declarationGeneration, token);
    if(!applyTextAreaScrollInput(item, frame, token))
        return false;
    const auto arranged = item.editView.arrangeViewport(box.rectangle, viewport->viewport, clip, state.scroll(), caretWidth, state.m_revealCaret);
    if(!arranged)
        return false;
    const EditBoxPlacement& placement = *arranged;
    if(!TryUpdateScrollViewportOffsets({ placement.scroll, placement.scrollY }, state.m_scrollInput.m_placement))
        return false;
    if(!item.editView.paint(m_builder.m_text, m_builder.m_paint, *m_builder.m_skin, placement, item.editStyle, item.editFlags) || !m_builder.textAreaMatches(frame))
        return false;
    state.m_visual.placement = placement;
    state.m_visual.scroll = placement.scroll;
    state.m_scrollY = placement.scrollY;
    const HitTarget target = HitTargetFactory().makeTextAreaHitTarget(
        box.rectangle,
        m_builder.visibleClip(box.clip),
        item.editOptions.enabled,
        item.contextMenu,
        token,
        step,
        state.scrollbars()
    );
    if(!m_builder.m_context.addTarget(item.state, target) || !paintTextAreaScrollbars(item, state.scrollbars(), clip, token))
        return false;
    const b8 published = !m_builder.m_editHost || m_builder.m_editHost->publish(item.state, item.editView, placement, item.editOptions);
    if(!published || !m_builder.textAreaMatches(frame))
        return false;
    return true;
}

b8 TextAreaCoordinator::applyTextAreaScrollInput(const Item& item, const TextAreaFrame& frame, const ControlToken& token){
    TextAreaState& state = *frame.state;
    const b8 enabled = item.editOptions.enabled;
    const auto horizontalId = TryMakeWidgetId(item.state.id, s_WidgetPartScrollHorizontalThumb);
    if(!horizontalId)
        return false;
    const auto verticalId = TryMakeWidgetId(item.state.id, s_WidgetPartScrollVerticalThumb);
    if(!verticalId)
        return false;
    auto horizontal = m_builder.m_context.takePartPointerGesture(item.state, *horizontalId, enabled);
    b8 haveHorizontal = horizontal && horizontal->control == token;
    auto vertical = m_builder.m_context.takePartPointerGesture(item.state, *verticalId, enabled);
    b8 haveVertical = vertical && vertical->control == token;
    auto action = m_builder.m_context.takeControlAction(item.state, enabled, token);
    b8 haveAction = action.has_value();
    Point scroll = state.scroll();
    b8 manual = false;
    while(haveHorizontal || haveVertical || haveAction){
        const u64 horizontalSequence = haveHorizontal ? horizontal->updateSequence : Limit<u64>::s_Max;
        const u64 verticalSequence = haveVertical ? vertical->updateSequence : Limit<u64>::s_Max;
        const u64 actionSequence = haveAction ? action->id.sequence : Limit<u64>::s_Max;
        if(haveHorizontal && horizontalSequence <= verticalSequence && horizontalSequence <= actionSequence){
            const auto dragged = TryDragScrollbarOffset(*horizontal, true);
            if(!dragged)
                return false;
            scroll.x = *dragged;
            manual = true;
            horizontal = m_builder.m_context.takePartPointerGesture(item.state, *horizontalId, enabled);
            haveHorizontal = horizontal && horizontal->control == token;
        }
        else if(haveVertical && verticalSequence <= actionSequence){
            const auto dragged = TryDragScrollbarOffset(*vertical, false);
            if(!dragged)
                return false;
            scroll.y = *dragged;
            manual = true;
            vertical = m_builder.m_context.takePartPointerGesture(item.state, *verticalId, enabled);
            haveVertical = vertical && vertical->control == token;
        }
        else{
            if(!TryApplyTextAreaScrollControlAction(*action, state.scrollbars(), scroll, manual))
                return false;
            action = m_builder.m_context.takeControlAction(item.state, enabled, token);
            haveAction = action.has_value();
        }
    }
    if(manual){
        state.m_visual.scroll = scroll.x;
        state.m_scrollY = scroll.y;
        state.m_revealCaret = false;
    }
    return true;
}

b8 TextAreaCoordinator::paintTextAreaScrollbars(const Item& item, const ScrollViewportPlacement& placement, const Rect& clip, const ControlToken& token){
    const b8 enabled = item.editOptions.enabled;
    b8 horizontalHovered = false;
    b8 horizontalPressed = false;
    if(placement.horizontal.visible){
        const auto thumbId = TryMakeWidgetId(item.state.id, s_WidgetPartScrollHorizontalThumb);
        if(!thumbId)
            return false;
        horizontalHovered = m_builder.m_context.input().hover() == *thumbId;
        horizontalPressed = m_builder.m_context.input().capture() == *thumbId;
    }
    b8 verticalHovered = false;
    b8 verticalPressed = false;
    if(placement.vertical.visible){
        const auto thumbId = TryMakeWidgetId(item.state.id, s_WidgetPartScrollVerticalThumb);
        if(!thumbId)
            return false;
        verticalHovered = m_builder.m_context.input().hover() == *thumbId;
        verticalPressed = m_builder.m_context.input().capture() == *thumbId;
    }
    if(!ControlVisualPainter().paintTextAreaScrollbarsVisual(
        m_builder.m_paint,
        *m_builder.m_skin,
        item.scrollbarStyle,
        placement,
        clip,
        enabled,
        horizontalHovered,
        horizontalPressed,
        verticalHovered,
        verticalPressed
    ))
        return false;
    if(!enabled)
        return true;
    for(u32 axis = 0u; axis < s_ScrollAxisCount; ++axis){
        const b8 horizontal = axis == 0u;
        const ScrollbarPlacement& bar = horizontal ? placement.horizontal : placement.vertical;
        if(!bar.visible)
            continue;
        Rect beforeRect;
        Rect afterRect;
        ResolveScrollbarBeforeAfterRects(bar, horizontal, beforeRect, afterRect);
        const HitTarget beforeTarget = HitTargetFactory().makeScrollbarTrackActionHitTarget(
            beforeRect,
            clip,
            token,
            horizontal ? s_TextAreaScrollHorizontalBefore : s_TextAreaScrollVerticalBefore
        );
        const auto beforeId = TryMakeWidgetId(item.state.id, horizontal ? s_WidgetPartScrollHorizontalBefore : s_WidgetPartScrollVerticalBefore);
        if(!beforeId || !m_builder.m_context.addPartTarget(item.state, *beforeId, beforeTarget))
            return false;
        const HitTarget afterTarget = HitTargetFactory().makeScrollbarTrackActionHitTarget(
            afterRect,
            clip,
            token,
            horizontal ? s_TextAreaScrollHorizontalAfter : s_TextAreaScrollVerticalAfter
        );
        const auto afterId = TryMakeWidgetId(item.state.id, horizontal ? s_WidgetPartScrollHorizontalAfter : s_WidgetPartScrollVerticalAfter);
        if(!afterId || !m_builder.m_context.addPartTarget(item.state, *afterId, afterTarget))
            return false;
        const HitTarget thumbTarget = HitTargetFactory().makeScrollbarThumbHitTarget(bar, clip, token, horizontal);
        const auto thumbId = TryMakeWidgetId(item.state.id, horizontal ? s_WidgetPartScrollHorizontalThumb : s_WidgetPartScrollVerticalThumb);
        if(!thumbId || !m_builder.m_context.addPartTarget(item.state, *thumbId, thumbTarget))
            return false;
    }
    if(placement.corner.width > 0.0f && placement.corner.height > 0.0f){
        const HitTarget target = HitTargetFactory().makeScrollbarCornerHitTarget(placement.corner, clip, token);
        const auto cornerId = TryMakeWidgetId(item.state.id, s_WidgetPartScrollCorner);
        if(!cornerId || !m_builder.m_context.addPartTarget(item.state, *cornerId, target))
            return false;
    }
    return true;
}

b8 Builder::paintTextArea(const Item& item, const LayoutBox& box){
    return TextAreaCoordinator(*this).paintTextArea(item, box);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 TooltipCoordinator::declareTooltip(const AStringView stableKey, const AStringView anchorKey, const StringView text, TooltipState& state, const TooltipOptions& options){
    Item* anchor = m_builder.annotationAnchor(anchorKey);
    if(m_builder.panelDeclarationBlocked() || !anchor || m_builder.m_scope->m_tooltips.size() >= s_LayoutMaxNodes){
        m_builder.m_context.fail();
        return false;
    }
    const auto widget = m_builder.m_context.declare(stableKey, WidgetKind::Tooltip);
    if(!widget || !m_builder.m_context.claimState(**widget, state.instanceGeneration()))
        return false;
    if(BindTooltipOwner((*widget)->id, (*widget)->declarationGeneration, state.m_owner, state.m_ownerDeclaration)){
        state.reset();
        state.m_owner = (*widget)->id;
        state.m_ownerDeclaration = (*widget)->declarationGeneration;
    }
    const InputRouter& input = m_builder.m_context.input();
    const HitTarget* accepted = input.findTarget(anchor->state.id, anchor->state.declarationGeneration);
    const HitTarget* hovered = input.findTarget(input.hover());
    const b8 overAnchor = hovered && (hovered->id == anchor->state.id || hovered->owner == anchor->state.id);
    const b8 eligible = options.enabled && anchor->enabled && input.windowFocused() && input.pointerKnown()
        && !m_builder.m_pointerBusy && !input.primaryDown() && !input.secondaryDown() && !input.capture().valid()
        && accepted && accepted->enabled && accepted->popup == m_builder.m_context.popupToken() && overAnchor;
    if(!TooltipBehavior::Update(state, anchor->state.id, anchor->state.declarationGeneration, m_builder.m_context.popupToken(), input.focusLossGeneration(), input.hoverActivityGeneration(), eligible, m_builder.m_deltaSeconds, options)){
        m_builder.m_context.fail();
        return false;
    }
    TooltipFrame frame(m_builder.m_arena);
    frame.widget = **widget;
    frame.anchor = anchor->state;
    frame.state = &state;
    frame.style = m_builder.m_tooltipStyle;
    frame.options = options;
    frame.revision = state.revision();
    frame.anchorIndex = static_cast<u32>(anchor - m_builder.m_scope->m_items.data());
    frame.layer = m_builder.m_context.popupLayer() + 1u;
    const ShapeRequest request = m_builder.textShapeRequest(text, m_builder.m_style.fontSize);
    auto layout = m_builder.m_text.layout(request);
    if(!layout){
        m_builder.m_context.fail();
        return false;
    }
    frame.text = Move(*layout);
    anchor->annotated = true;
    m_builder.m_scope->m_tooltips.push_back(Move(frame));
    return true;
}

b8 TooltipCoordinator::paintTooltips(){
    for(auto& frame : m_builder.m_scope->m_tooltips){
        if(!frame.state || frame.state->revision() != frame.revision || frame.anchorIndex >= m_builder.m_scope->m_items.size())
            return false;
        const Item& anchor = m_builder.m_scope->m_items[frame.anchorIndex];
        const LayoutBox* box = m_builder.m_scope->m_layout.box(anchor.node);
        if(!box || anchor.state.id != frame.anchor.id || anchor.state.declarationGeneration != frame.anchor.declarationGeneration)
            return false;
        if(!frame.state->visible())
            continue;
        const Rect clip = m_builder.visibleClip(box->clip);
        const Point& pointer = m_builder.m_context.input().pointerPosition();
        const InputRouter& input = m_builder.m_context.input();
        if(
            !anchor.enabled || !input.pointerKnown() || !input.windowFocused() || m_builder.m_pointerBusy
            || input.primaryDown() || input.secondaryDown()
            || (m_builder.m_context.topPopupToken().valid() && m_builder.m_context.topPopupToken() != frame.state->m_popup)
            || !UiRectIntersectionContainsPoint(box->rectangle, clip, pointer)
        ){
            if(!TooltipBehavior::Update(*frame.state, frame.anchor.id, frame.anchor.declarationGeneration, frame.state->m_popup, input.focusLossGeneration(), input.hoverActivityGeneration(), false, 0.0f, frame.options))
                return false;
            frame.revision = frame.state->revision();
            continue;
        }
        const UiSkinRegion* background = m_builder.region(frame.style.background, frame.style.fallback);
        if(!background)
            return false;
        const Insets padding = MaxUiPadding(frame.style.padding, background->padding);
        const auto textBounds = m_builder.m_text.measure(frame.text, m_builder.m_paint.displayMetrics());
        if(!textBounds)
            return false;
        const Point measured{ textBounds->width, textBounds->height };
        const PopupOptions options = BuildTooltipPopupOptions(box->rectangle, measured, padding, *background, frame.options);
        const auto placement = PlacePopup(options, m_builder.m_paint.displayMetrics());
        if(!placement)
            return false;
        if(!ControlVisualPainter().paintTooltipOverlay(m_builder.m_paint, m_builder.m_text, frame.layer, background->name, placement->bounds, padding, frame.text, *textBounds, frame.style.text) || frame.state->revision() != frame.revision)
            return false;
        frame.state->m_placement = *placement;
    }
    return true;
}

b8 Builder::tooltip(const AStringView stableKey, const AStringView anchorKey, const StringView text, TooltipState& state, const TooltipOptions& options){
    return TooltipCoordinator(*this).declareTooltip(stableKey, anchorKey, text, state, options);
}

b8 Builder::paintTooltips(){
    return TooltipCoordinator(*this).paintTooltips();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
