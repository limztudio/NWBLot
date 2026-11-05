// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "builder.h"

#include <global/simd/vector_arithmetic.h>
#include <global/simplemath.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


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
    const LayoutNodeDesc description = MakePanelRootLayoutNodeDesc(bounds, direction, panel->padding, m_style.gap);
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
    const LayoutNodeDesc description = MakeContainerLayoutNodeDesc(direction, options);
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
        const auto checkboxExtent = ResolveCheckboxStyleExtent(*m_skin, m_style);
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
    return ResolveSkinRegionsMaxPadding(*m_skin, initial, names, fallback);
}

void Builder::loadButtonMetrics(Point& size, Insets& padding)const noexcept{
    if(!m_skin)
        return;
    LoadButtonStyleMetrics(*m_skin, m_style, size, padding);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

