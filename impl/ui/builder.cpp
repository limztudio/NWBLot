// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "builder.h"

#include <global/simplemath.h>


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
{}

bool Builder::beginPanel(const AStringView stableKey, const Rect& bounds, const LayoutDirection::Enum direction){
    if(
        m_scope->m_panelActive || m_scope->m_windowActive || !m_skin
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
    m_scope->m_panelState = *state;
    m_scope->m_bounds = bounds;
    LayoutNodeDesc description;
    description.direction = direction;
    description.width = { LayoutSizePolicy::Fixed, bounds.width };
    description.height = { LayoutSizePolicy::Fixed, bounds.height };
    description.padding = { panel->padding.left, panel->padding.top, panel->padding.right, panel->padding.bottom };
    description.gap = m_style.gap;
    u32 node = 0u;
    if(!m_scope->m_layout.addNode(s_LayoutNoParent, description, node) || !m_context.pushScope(stableKey)){
        m_context.fail();
        return false;
    }
    m_scope->m_stack.push_back(node);
    m_scope->m_panelActive = true;
    return true;
}

bool Builder::endPanel(){
    if(!m_scope->m_panelActive || m_scope->m_windowActive || m_scope->m_popupState || m_scope->m_stack.size() != 1u || m_context.failed() || !m_scope->m_layout.arrange(m_scope->m_bounds)){
        m_context.fail();
        return false;
    }
    const bool painted = paintPanel();
    const bool popped = m_context.popScope();
    m_scope->m_panelActive = false;
    m_scope->m_stack.clear();
    const bool combosPainted = painted && popped && paintDeferred();
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
    if(!m_scope->m_panelActive || m_scope->m_stack.size() <= 1u){
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
    if(m_scope->m_panelActive || m_scope->m_windowActive)
        m_context.fail();
    m_scope->reset();
}

bool Builder::beginContainer(
    const AStringView stableKey, const LayoutDirection::Enum direction, const ContainerOptions& options){
    if(
        !m_scope->m_panelActive || (m_scope->m_windowActive && m_scope->m_window.state->collapsed) || m_scope->m_stack.size() >= 64u
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
    if(!m_scope->m_layout.addNode(m_scope->m_stack.back(), description, node) || !m_context.pushScope(stableKey)){
        m_context.fail();
        return false;
    }
    m_scope->m_stack.push_back(node);
    return true;
}

Builder::Item* Builder::addItem(
    const AStringView stableKey, const StringView text, const WidgetKind::Enum kind, const WidgetOptions& options){
    if(
        !m_scope->m_panelActive || (m_scope->m_windowActive && m_scope->m_window.state->collapsed)
        || m_context.failed() || m_scope->m_items.size() >= s_LayoutMaxNodes
    ){
        m_context.fail();
        return nullptr;
    }
    WidgetState* state = m_context.declare(stableKey, kind);
    if(!state)
        return nullptr;
    Item item(m_arena);
    item.state = *state;
    item.enabled = options.enabled && synchronizePopup();
    ShapeRequest request{ text, m_style.fontSize };
    if(m_text.layout(request, item.text) != TextLayoutStatus::Success){
        m_context.fail();
        return nullptr;
    }
    Point minimum;
    Point measured = item.text.measure();
    if(kind == WidgetKind::Selectable){
        const UiSkinRegion* skinRegion = region(m_listStyle.row.normal, m_listStyle.row.fallback);
        if(!skinRegion){
            m_context.fail();
            return nullptr;
        }
        item.padding = m_listStyle.row.padding;
        measured.x += item.padding.left + item.padding.right;
        measured.y += item.padding.top + item.padding.bottom;
        minimum = { skinRegion->minimumWidth, skinRegion->minimumHeight };
    }
    else if(kind == WidgetKind::Button){
        if(!region(m_style.button, m_style.button)){
            m_context.fail();
            return nullptr;
        }
        buttonMetrics(minimum, item.padding);
        measured.x += item.padding.left + item.padding.right;
        measured.y += item.padding.top + item.padding.bottom;
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
    description.intrinsicSize = { Max(measured.x, minimum.x), Max(measured.y, minimum.y) };
    if(!m_scope->m_layout.addNode(m_scope->m_stack.back(), description, item.node)){
        m_context.fail();
        return nullptr;
    }
    m_scope->m_items.push_back(Move(item));
    return &m_scope->m_items.back();
}

const UiSkinRegion* Builder::region(const Name& preferred, const Name& fallback)const{
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
        size.x = Max(size.x, skinRegion->minimumWidth);
        size.y = Max(size.y, skinRegion->minimumHeight);
        padding.left = Max(padding.left, skinRegion->padding.left);
        padding.top = Max(padding.top, skinRegion->padding.top);
        padding.right = Max(padding.right, skinRegion->padding.right);
        padding.bottom = Max(padding.bottom, skinRegion->padding.bottom);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

