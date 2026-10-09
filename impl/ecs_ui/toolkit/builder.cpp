// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "builder.h"

#include <global/math/vector_arithmetic.h>
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

ShapeRequest Builder::textShapeRequest(const StringView text, const f32 fontSize)const{
    return { text, fontSize, TextDirection::LeftToRight, m_textScriptTag,
        StringView(m_textLanguage.data(), m_textLanguage.size()) };
}

bool Builder::beginPanel(const AStringView stableKey, const Rect& bounds, const LayoutDirection::Enum direction){
    if(
        declarationBlocked() || m_scope->m_panelActive || m_scope->m_windowActive || !m_skin
        || (direction != LayoutDirection::Row && direction != LayoutDirection::Column)
    ){
        m_context.fail();
        return false;
    }
    const UiSkinRegion* panel = region(m_style.panel, m_style.panel);
    WidgetState* state = m_context.declare(stableKey, WidgetKind::Panel);
    if(!panel || !state){
        m_context.fail();
        return false;
    }
    reset();
    m_scope->m_panelStyle = m_style;
    m_scope->m_panelState = *state;
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

bool Builder::endContainer(){
    if(declarationBlocked() || !m_scope->m_panelActive || m_scope->m_stack.size() <= 1u){
        m_context.fail();
        return false;
    }
    m_scope->m_stack.pop_back();
    return m_context.popScope();
}

bool Builder::label(const AStringView stableKey, const StringView text, const WidgetOptions& options){
    return addItem(stableKey, text, WidgetKind::Label, options) != nullptr;
}

bool Builder::button(const AStringView stableKey, const StringView text, const WidgetOptions& options){
    const Item* item = addItem(stableKey, text, WidgetKind::Button, options);
    return item && m_context.takeActivation(item->state, item->enabled);
}

bool Builder::checkbox(const AStringView stableKey, const StringView text, bool& checked, const WidgetOptions& options){
    Item* item = addItem(stableKey, text, WidgetKind::Checkbox, options);
    if(!item)
        return false;
    const bool changed = m_context.takeActivation(item->state, item->enabled);
    if(changed)
        checked = !checked;
    item->checked = checked;
    return changed;
}

void Builder::reset(){
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

bool Builder::beginContainer(
    const AStringView stableKey, const LayoutDirection::Enum direction, const ContainerOptions& options
){
    if(
        declarationBlocked() || !m_scope->m_panelActive || (m_scope->m_windowActive && m_scope->m_window.state->collapsed) || m_scope->m_stack.size() >= 64u
        || !m_context.declare(stableKey, WidgetKind::Container)
    ){
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

Builder::Item* Builder::addItem(
    const AStringView stableKey, const StringView text, const WidgetKind::Enum kind, const WidgetOptions& options
){
    if(
        declarationBlocked() || !m_scope->m_panelActive || (m_scope->m_windowActive && m_scope->m_window.state->collapsed)
        || m_context.failed() || m_scope->m_items.size() >= s_LayoutMaxNodes
    ){
        m_context.fail();
        return nullptr;
    }
    WidgetState* state = m_context.declare(stableKey, kind);
    if(!state)
        return nullptr;
    Item item(m_arena);
    item.style = m_style;
    item.state = *state;
    item.enabled = options.enabled && synchronizePopup();
    const ShapeRequest request = textShapeRequest(text, m_style.fontSize);
    auto layout = m_text.layout(request);
    if(!layout){
        m_context.fail();
        return nullptr;
    }
    item.text = Move(*layout);
    Point minimum;
    Point measured = item.text.measure();
    if(kind == WidgetKind::Selectable){
        const UiSkinRegion* skinRegion = region(m_listStyle.row.normal, m_listStyle.row.fallback);
        if(!skinRegion){
            m_context.fail();
            return nullptr;
        }
        item.padding = m_listStyle.row.padding;
        const SIMDVector paddingSize = VectorAdd(VectorSet(item.padding.left, item.padding.top, item.padding.left, item.padding.top),
            VectorSet(item.padding.right, item.padding.bottom, item.padding.right, item.padding.bottom));
        const SIMDVector paddedMeasure = VectorAdd(VectorSet(measured.x, measured.y, measured.x, measured.y), paddingSize);
        measured = { VectorGetX(paddedMeasure), VectorGetY(paddedMeasure) };
        minimum = { skinRegion->minimumWidth, skinRegion->minimumHeight };
    }
    else if(kind == WidgetKind::Button){
        if(!region(m_style.button, m_style.button)){
            m_context.fail();
            return nullptr;
        }
        buttonMetrics(minimum, item.padding);
        const SIMDVector paddingSize = VectorAdd(VectorSet(item.padding.left, item.padding.top, item.padding.left, item.padding.top),
            VectorSet(item.padding.right, item.padding.bottom, item.padding.right, item.padding.bottom));
        const SIMDVector paddedMeasure = VectorAdd(VectorSet(measured.x, measured.y, measured.x, measured.y), paddingSize);
        measured = { VectorGetX(paddedMeasure), VectorGetY(paddedMeasure) };
    }
    else if(kind == WidgetKind::Checkbox){
        const Name names[]{ m_style.checkbox, m_style.checkboxHover, m_style.checkboxChecked, m_style.checkboxDisabled };
        item.checkboxExtent = m_style.checkboxExtent;
        if(!region(m_style.checkbox, m_style.checkbox)){
            m_context.fail();
            return nullptr;
        }
        for(const auto& name : names){
            const UiSkinRegion* skinRegion = region(name, m_style.checkbox);
            item.checkboxExtent = Max(item.checkboxExtent, Max(skinRegion->minimumWidth, skinRegion->minimumHeight));
        }
        measured.x += item.checkboxExtent + m_style.gap;
        measured.y = Max(measured.y, item.checkboxExtent);
    }
    LayoutNodeDesc description;
    description.width = options.width;
    description.height = options.height;
    const SIMDVector measuredValue = VectorSet(measured.x, measured.y, measured.x, measured.y);
    const SIMDVector minimumValue = VectorSet(minimum.x, minimum.y, minimum.x, minimum.y);
    const SIMDVector intrinsic = VectorSelect(minimumValue, measuredValue, VectorGreater(measuredValue, minimumValue));
    description.intrinsicSize = { VectorGetX(intrinsic), VectorGetY(intrinsic) };
    const auto admittedNode = m_scope->m_layout.addNode(m_scope->m_stack.back(), description);
    if(!admittedNode){
        m_context.fail();
        return nullptr;
    }
    item.node = *admittedNode;
    m_scope->m_items.push_back(Move(item));
    return &m_scope->m_items.back();
}

const UiSkinRegion* Builder::region(const Name& preferred, const Name& fallback)const noexcept{
    if(!m_skin)
        return nullptr;
    const UiSkinRegion* found = m_skin->findRegion(preferred);
    return found ? found : m_skin->findRegion(fallback);
}

void Builder::buttonMetrics(Point& size, Insets& padding)const{
    const Name names[]{ m_style.button, m_style.buttonHover, m_style.buttonPressed, m_style.buttonDisabled };
    for(const auto& name : names){
        const UiSkinRegion* skinRegion = region(name, m_style.button);
        if(!skinRegion)
            continue;
        const SIMDVector sizeCurrent = VectorSet(size.x, size.y, size.x, size.y);
        const SIMDVector sizeCandidate = VectorSet(skinRegion->minimumWidth, skinRegion->minimumHeight,
            skinRegion->minimumWidth, skinRegion->minimumHeight);
        const SIMDVector sizeMaximum = VectorSelect(sizeCandidate, sizeCurrent,
            VectorGreater(sizeCurrent, sizeCandidate));
        size = { VectorGetX(sizeMaximum), VectorGetY(sizeMaximum) };
        const SIMDVector paddingCurrent = VectorSet(padding.left, padding.top, padding.right, padding.bottom);
        const SIMDVector paddingCandidate = VectorSet(skinRegion->padding.left, skinRegion->padding.top, skinRegion->padding.right, skinRegion->padding.bottom);
        const SIMDVector paddingMaximum = VectorSelect(paddingCandidate, paddingCurrent,
            VectorGreater(paddingCurrent, paddingCandidate));
        padding = { VectorGetX(paddingMaximum), VectorGetY(paddingMaximum), VectorGetZ(paddingMaximum), VectorGetW(paddingMaximum) };
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

