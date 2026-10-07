// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../builder.h"

#include <global/simplemath.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ListResult Builder::virtualList(
    const AStringView stableKey, const IListDataSource& source, ListState& state, const ListOptions& options
){
    ListResult result;
    if(
        declarationBlocked() || !m_scope->m_panelActive || (m_scope->m_windowActive && m_scope->m_window.state->collapsed) || m_context.failed()
        || m_scope->m_items.size() >= s_LayoutMaxNodes || !IsFinite(options.rowHeight) || options.rowHeight <= 0.0f
        || !IsFinite(options.wheelRows) || options.wheelRows <= 0.0f
    ){
        m_context.fail();
        return result;
    }
    // A body callback may have closed or reopened its popup; no model loan is allowed past that boundary.
    if(!synchronizePopup()){
        result.valid = true;
        return result;
    }
    m_declaring = true;
    ScopeExit finish([this]()noexcept{ m_declaring = false; });
    WidgetState* widget = m_context.declare(stableKey, WidgetKind::VirtualList);
    if(!widget)
        return result;
    const bool previouslyFocused = m_context.input().focus() == widget->id;
    const u64 previousSelection = state.selectedKey();
    if(!ListBehavior::Reconcile(state, source)){
        m_context.fail();
        return result;
    }
    const ControlToken token{ state.inputGeneration(), source.instanceGeneration(), source.revision() };
    m_context.input().fenceControl(widget->id, widget->declarationGeneration, token);
    if(!options.enabled)
        m_context.input().invalidateTarget(widget->id);
    result.valid = true;
    result.selectionChanged = previousSelection != state.selectedKey();
    result.focused = options.enabled && m_context.input().focus() == widget->id;
    const WidgetId thumb = MakeWidgetId(widget->id, "scrollbar");
    PointerGesture gesture;
    bool haveGesture = m_context.takePartPointerGesture(*widget, thumb, options.enabled, gesture)
        && gesture.control == token;
    ControlAction action;
    bool haveAction = m_context.takeControlAction(*widget, options.enabled, token, action);
    while(haveGesture || haveAction){
        if(haveGesture && (!haveAction || gesture.updateSequence < action.id.sequence)){
            if(!applyListGesture(state, gesture)){
                m_context.fail();
                result.valid = false;
                return result;
            }
            haveGesture = m_context.takePartPointerGesture(*widget, thumb, options.enabled, gesture)
                && gesture.control == token;
        }
        else{
            if(!ListBehavior::Apply(state, source, options, action, result)){
                m_context.fail();
                result.valid = false;
                return result;
            }
            haveAction = m_context.takeControlAction(*widget, options.enabled, token, action);
        }
    }
    ListFrame frame;
    frame.source = &source;
    frame.state = &state;
    frame.widgetStyle = m_style;
    frame.style = m_listStyle;
    frame.options = options;
    frame.token = token;
    frame.rowCount = source.rowCount();
    frame.focusOnCommit = options.enabled && previouslyFocused && !result.focused;
    frame.padding = frame.style.padding;
    const UiSkinRegion* background = region(frame.style.background, frame.style.backgroundFallback);
    if(!background){
        m_context.fail();
        result.valid = false;
        return result;
    }
    frame.padding.left = Max(frame.padding.left, background->padding.left);
    frame.padding.top = Max(frame.padding.top, background->padding.top);
    frame.padding.right = Max(frame.padding.right, background->padding.right);
    frame.padding.bottom = Max(frame.padding.bottom, background->padding.bottom);
    Item item(m_arena);
    item.state = *widget;
    item.enabled = options.enabled;
    item.list = static_cast<u32>(m_scope->m_lists.size());
    LayoutNodeDesc description;
    description.width = options.width;
    description.height = options.height;
    description.intrinsicSize = { Max(120.0f, background->minimumWidth),
        Max(options.rowHeight + frame.padding.top + frame.padding.bottom, background->minimumHeight) };
    if(!m_scope->m_layout.addNode(m_scope->m_stack.back(), description, item.node)){
        m_context.fail();
        result.valid = false;
        return result;
    }
    m_scope->m_lists.push_back(frame);
    m_scope->m_items.push_back(Move(item));
    return result;
}

bool Builder::applyListGesture(ListState& state, const PointerGesture& gesture)noexcept{
    const f64 travel = static_cast<f64>(gesture.referenceRectangle.height) - static_cast<f64>(gesture.targetRectangle.height);
    if(!IsFinite(travel) || travel <= 0.0 || !IsFinite(gesture.maximum) || gesture.maximum < 0.0)
        return false;
    const f64 start = static_cast<f64>(gesture.targetRectangle.y) - static_cast<f64>(gesture.referenceRectangle.y);
    const f64 delta = static_cast<f64>(gesture.position.y) - static_cast<f64>(gesture.origin.y);
    const f64 fraction = Clamp((start + delta) / travel, 0.0, 1.0);
    state.m_ensureCursor = false;
    return state.m_scroll.setOffset(fraction * gesture.maximum);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

