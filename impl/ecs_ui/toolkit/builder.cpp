// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "builder.h"
#include "widgets/selectable_paint.h"

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
    , m_textLanguage("en", arena)
{}

bool Builder::beginPanel(const AStringView stableKey, const Rect& bounds, const LayoutDirection::Enum direction){
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
    LayoutNodeDesc description;
    description.direction = direction;
    description.width = { LayoutSizePolicy::Fixed, bounds.width };
    description.height = { LayoutSizePolicy::Fixed, bounds.height };
    description.padding = { panel->padding.left, panel->padding.top, panel->padding.right, panel->padding.bottom };
    description.gap = m_style.gap;
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

bool Builder::endPanel(){
    if(declarationBlocked() || !m_scope->m_panelActive || m_scope->m_windowActive || m_scope->m_popupState || m_scope->m_stack.size() != 1u || m_context.failed() || !m_scope->m_layout.arrange(m_scope->m_bounds)){
        m_context.fail();
        return false;
    }
    m_finalizing = true;
    ScopeExit finish([this]()noexcept{ m_finalizing = false; });
    const bool painted = paintPanel();
    const bool popped = m_context.popScope();
    m_scope->m_panelActive = false;
    m_scope->m_stack.clear();
    const bool combosPainted = painted && popped && paintDeferred();
    publishSliderResults(combosPainted);
    releaseDeferredLoans();
    if(!combosPainted)
        m_context.fail();
    return combosPainted;
}

bool Builder::beginRow(const AStringView stableKey, const ContainerOptions& options){
    return beginContainer(stableKey, LayoutDirection::Row, options);
}

bool Builder::beginColumn(const AStringView stableKey, const ContainerOptions& options){
    return beginContainer(stableKey, LayoutDirection::Column, options);
}

bool Builder::endContainer()noexcept{
    if(declarationBlocked() || !m_scope->m_panelActive || m_scope->m_stack.size() <= 1u){
        m_context.fail();
        return false;
    }
    m_scope->m_stack.pop_back();
    return m_context.popScope();
}

bool Builder::label(const AStringView stableKey, const StringView text, const WidgetOptions& options){
    return addItem(stableKey, text, WidgetKind::Label, options).has_value();
}

bool Builder::button(const AStringView stableKey, const StringView text, const WidgetOptions& options){
    const auto item = addItem(stableKey, text, WidgetKind::Button, options);
    return item && m_context.takeActivation((*item)->state, (*item)->enabled);
}

bool Builder::checkbox(const AStringView stableKey, const StringView text, bool& checked, const WidgetOptions& options){
    const auto item = addItem(stableKey, text, WidgetKind::Checkbox, options);
    if(!item)
        return false;
    const bool changed = m_context.takeActivation((*item)->state, (*item)->enabled);
    if(changed)
        checked = !checked;
    (*item)->checked = checked;
    return changed;
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

bool Builder::setTextShaping(const u32 scriptTag, const StringView language){
    if(!balanced() || m_context.failed()){
        m_context.fail();
        return false;
    }
    const ShapeRequest request{ {}, 16.0f, TextDirection::LeftToRight, scriptTag, language };
    if(ValidateTextRequest(request, false) != TextLayoutStatus::Success)
        return false;
    AString<Core::Alloc::GlobalArena> copied(language.data(), language.size(), m_arena);
    m_textLanguage = Move(copied);
    m_textScriptTag = scriptTag;
    return true;
}

WidgetStyle& Builder::style()noexcept{
    if(declarationBlocked())
        m_context.fail();
    return m_style;
}

ScrollbarStyle& Builder::scrollbarStyle()noexcept{
    if(declarationBlocked())
        m_context.fail();
    return m_scrollbarStyle;
}

EditBoxStyle& Builder::editStyle()noexcept{
    if(declarationBlocked())
        m_context.fail();
    return m_editStyle;
}

PopupStyle& Builder::popupStyle()noexcept{
    if(declarationBlocked())
        m_context.fail();
    return m_popupStyle;
}

ListStyle& Builder::listStyle()noexcept{
    if(declarationBlocked())
        m_context.fail();
    return m_listStyle;
}

RadioGroupStyle& Builder::radioGroupStyle()noexcept{
    if(declarationBlocked())
        m_context.fail();
    return m_radioGroupStyle;
}

SliderStyle& Builder::sliderStyle()noexcept{
    if(declarationBlocked())
        m_context.fail();
    return m_sliderStyle;
}

ProgressStyle& Builder::progressStyle()noexcept{
    if(declarationBlocked())
        m_context.fail();
    return m_progressStyle;
}

ComboStyle& Builder::comboStyle()noexcept{
    if(declarationBlocked())
        m_context.fail();
    return m_comboStyle;
}

TooltipStyle& Builder::tooltipStyle()noexcept{
    if(declarationBlocked())
        m_context.fail();
    return m_tooltipStyle;
}

void Builder::setEditHost(IEditBoxHost* host)noexcept{
    if(declarationBlocked())
        m_context.fail();
    else
        m_editHost = host;
}

void Builder::setDeltaSeconds(const f32 delta)noexcept{
    if(declarationBlocked())
        m_context.fail();
    else
        m_deltaSeconds = delta;
}

void Builder::setPointerBusy(const bool busy)noexcept{
    if(declarationBlocked())
        m_context.fail();
    else
        m_pointerBusy = busy;
}

ShapeRequest Builder::textShapeRequest(const StringView text, const f32 fontSize)const noexcept{
    return { text, fontSize, TextDirection::LeftToRight, m_textScriptTag, StringView(m_textLanguage.data(), m_textLanguage.size()) };
}

bool Builder::beginContainer(const AStringView stableKey, const LayoutDirection::Enum direction, const ContainerOptions& options){
    if(declarationBlocked() || !m_scope->m_panelActive || (m_scope->m_windowActive && m_scope->m_window.state->collapsed) || m_scope->m_stack.size() >= 64u || !m_context.declare(stableKey, WidgetKind::Container)){
        m_context.fail();
        return false;
    }
    LayoutNodeDesc description;
    description.direction = direction;
    description.width = options.width;
    description.height = options.height;
    description.padding = options.padding;
    description.gap = options.gap;
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

Expected<NotNull<Builder::Item*>, Builder::ItemDeclarationFailure> Builder::addItem(const AStringView stableKey, const StringView text, const WidgetKind::Enum kind, const WidgetOptions& options){
    if(declarationBlocked() || !m_scope->m_panelActive || (m_scope->m_windowActive && m_scope->m_window.state->collapsed) || m_context.failed() || m_scope->m_items.size() >= s_LayoutMaxNodes){
        m_context.fail();
        return MakeUnexpected(ItemDeclarationFailure{});
    }
    const auto state = m_context.declare(stableKey, kind);
    if(!state)
        return MakeUnexpected(ItemDeclarationFailure{});
    Item item(m_arena);
    item.style = m_style;
    item.state = **state;
    item.enabled = options.enabled && synchronizePopup();
    const ShapeRequest request = textShapeRequest(text, m_style.fontSize);
    auto layout = m_text.layout(request);
    if(!layout){
        m_context.fail();
        return MakeUnexpected(ItemDeclarationFailure{ layout.error() });
    }
    item.text = Move(*layout);
    const auto textBounds = m_text.measure(item.text, m_paint.displayMetrics());
    if(!textBounds){
        m_context.fail();
        return MakeUnexpected(ItemDeclarationFailure{});
    }
    item.textBounds = *textBounds;
    SIMDVectorSingle minimum = VectorZero();
    SIMDVectorSingle measured = VectorSet(item.textBounds.width, item.textBounds.height, item.textBounds.width, item.textBounds.height);
    if(kind == WidgetKind::Selectable){
        const auto metrics = SelectablePainter::Measure(*m_skin, m_listStyle.row);
        if(!metrics){
            m_context.fail();
            return MakeUnexpected(ItemDeclarationFailure{});
        }
        item.padding = metrics->padding;
        const SIMDVectorSingle paddingSize = VectorAdd(VectorSet(item.padding.left, item.padding.top, item.padding.left, item.padding.top), VectorSet(item.padding.right, item.padding.bottom, item.padding.right, item.padding.bottom));
        measured = VectorAdd(measured, paddingSize);
        minimum = VectorSet(metrics->minimum.x, metrics->minimum.y, metrics->minimum.x, metrics->minimum.y);
    }
    else if(kind == WidgetKind::Button){
        if(!region(m_style.button, m_style.button)){
            m_context.fail();
            return MakeUnexpected(ItemDeclarationFailure{});
        }
        SIMDVectorSingle paddingValue = VectorSet(item.padding.left, item.padding.top, item.padding.right, item.padding.bottom);
        loadButtonMetrics(minimum, paddingValue);
        const SIMDVectorSingle paddingSize = VectorAdd(VectorSwizzle<0, 1, 0, 1>(paddingValue), VectorSwizzle<2, 3, 2, 3>(paddingValue));
        measured = VectorAdd(measured, paddingSize);
        item.padding = { VectorGetX(paddingValue), VectorGetY(paddingValue), VectorGetZ(paddingValue), VectorGetW(paddingValue) };
    }
    else if(kind == WidgetKind::Checkbox){
        const Name names[]{ m_style.checkbox, m_style.checkboxHover, m_style.checkboxChecked, m_style.checkboxDisabled };
        SIMDVectorSingle checkboxExtent = VectorReplicate(m_style.checkboxExtent);
        if(!region(m_style.checkbox, m_style.checkbox)){
            m_context.fail();
            return MakeUnexpected(ItemDeclarationFailure{});
        }
        for(const auto& name : names){
            const UiSkinRegion* skinRegion = region(name, m_style.checkbox);
            const SIMDVectorSingle regionMinimum = VectorSet(skinRegion->minimumWidth, skinRegion->minimumHeight, 0.0f, 0.0f);
            checkboxExtent = VectorSplatX(VectorMax(checkboxExtent, VectorMax(regionMinimum, VectorSplatY(regionMinimum))));
        }
        const SIMDVectorSingle measuredSize = VectorSelect(VectorZero(), measured, s_SIMDMaskXY);
        const SIMDVectorSingle content = VectorMax(measuredSize, VectorSelect(measuredSize, checkboxExtent, s_SIMDMaskY));
        measured = VectorAdd(content, VectorAdd(VectorSelect(VectorZero(), checkboxExtent, s_SIMDMaskX), VectorSet(m_style.gap, 0.0f, 0.0f, 0.0f)));
        item.checkboxExtent = VectorGetX(checkboxExtent);
    }
    LayoutNodeDesc description;
    description.width = options.width;
    description.height = options.height;
    const SIMDVectorSingle intrinsic = VectorSelect(minimum, measured, VectorGreater(measured, minimum));
    description.intrinsicSize = { VectorGetX(intrinsic), VectorGetY(intrinsic) };
    const auto admittedNode = m_scope->m_layout.addNode(m_scope->m_stack.back(), description);
    if(!admittedNode){
        m_context.fail();
        return MakeUnexpected(ItemDeclarationFailure{});
    }
    item.node = *admittedNode;
    m_scope->m_items.push_back(Move(item));
    return MakeNotNull(&m_scope->m_items.back());
}

const UiSkinRegion* Builder::region(const Name& preferred, const Name& fallback)const noexcept{
    if(!m_skin)
        return nullptr;
    const UiSkinRegion* found = m_skin->findRegion(preferred);
    return found ? found : m_skin->findRegion(fallback);
}

void Builder::loadButtonMetrics(SIMDVectorSingle& size, SIMDVectorSingle& padding)const noexcept{
    const Name names[]{ m_style.button, m_style.buttonHover, m_style.buttonPressed, m_style.buttonDisabled };
    for(const auto& name : names){
        const UiSkinRegion* skinRegion = region(name, m_style.button);
        if(!skinRegion)
            continue;
        const SIMDVectorSingle sizeCandidate = VectorSet(skinRegion->minimumWidth, skinRegion->minimumHeight, skinRegion->minimumWidth, skinRegion->minimumHeight);
        size = VectorSelect(sizeCandidate, size, VectorGreater(size, sizeCandidate));
        const SIMDVectorSingle paddingCandidate = VectorSet(skinRegion->padding.left, skinRegion->padding.top, skinRegion->padding.right, skinRegion->padding.bottom);
        padding = VectorSelect(paddingCandidate, padding, VectorGreater(padding, paddingCandidate));
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

