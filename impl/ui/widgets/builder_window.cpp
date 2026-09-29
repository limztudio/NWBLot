// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../builder.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Builder::beginWindow(
    const AStringView stableKey, const StringView title, WindowState& state, const WindowOptions& options){
    if(m_panelActive || m_windowActive || !m_skin || m_context.failed()){
        m_context.fail();
        return false;
    }
    reset();
    const UiSkinRegion* frame = region(m_style.window, m_style.window);
    const UiSkinRegion* header = region(m_style.windowTitle, m_style.windowTitle);
    const UiSkinRegion* collapse = region(m_style.windowCollapse, m_style.windowCollapse);
    const UiSkinRegion* resize = region(m_style.windowResize, m_style.windowResize);
    WidgetState* window = m_context.declare(stableKey, WidgetKind::Window);
    ShapeRequest request{ title, m_style.fontSize };
    if(
        !frame || !header || (options.collapsible && !collapse)
        || (options.resizable && !resize && !region(m_style.white, m_style.white)) || !window
        || m_text.layout(request, m_window.title) != TextLayoutStatus::Success
        || !WindowLayout::Measure(
            *frame, *header, collapse, resize, m_style, options,
            m_window.title.measure(), m_skin->referenceDensity(), m_window.metrics
        )
    ){
        m_context.fail();
        return false;
    }
    m_panelState = *window;
    m_window.state = &state;
    m_window.options = options;
    m_window.firstUse = !state.initialized;
    WindowState candidate = state;
    if(!WindowBehavior::Initialize(candidate, options, m_window.metrics) || !m_context.pushScope(stableKey)){
        m_context.fail();
        return false;
    }
    // Keys beginning with @window. belong to the window's internal chrome.
    WidgetState* titleState = m_context.declare("@window.title", WidgetKind::Window);
    WidgetState* collapseState = m_context.declare("@window.collapse", WidgetKind::Button);
    WidgetState* resizeState = m_context.declare("@window.resize", WidgetKind::Window);
    if(!titleState || !collapseState || !resizeState){
        m_context.fail();
        return false;
    }
    m_window.titleState = *titleState;
    m_window.collapseState = *collapseState;
    m_window.resizeState = *resizeState;
    if(!options.collapsible)
        candidate.collapsed = false;
    if(m_context.takeActivation(m_window.collapseState, options.collapsible))
        candidate.collapsed = !candidate.collapsed;
    PointerGesture gesture;
    while(m_context.takePointerGesture(m_window.titleState, options.movable, gesture)){
        if(!WindowBehavior::ApplyMove(candidate, gesture)){
            m_context.fail();
            return false;
        }
    }
    while(m_context.takePointerGesture(m_window.resizeState, options.resizable && !candidate.collapsed, gesture)){
        if(!WindowBehavior::ApplyResize(candidate, gesture, m_window.metrics.minimumSize)){
            m_context.fail();
            return false;
        }
    }
    if(!WindowBehavior::Constrain(candidate, m_paint.displayMetrics(), m_window.metrics.titleHeight)){
        m_context.fail();
        return false;
    }
    m_bounds = WindowLayout::Content(candidate, m_window.metrics);
    LayoutNodeDesc description;
    description.direction = options.direction;
    description.width = m_window.firstUse && options.contentWidthFirstUse
        ? LayoutSize{ LayoutSizePolicy::Content, 0.0f } : LayoutSize{ LayoutSizePolicy::Fixed, m_bounds.width };
    description.height = m_window.firstUse && options.contentHeightFirstUse
        ? LayoutSize{ LayoutSizePolicy::Content, 0.0f } : LayoutSize{ LayoutSizePolicy::Fixed, m_bounds.height };
    description.padding = m_window.metrics.contentPadding;
    description.gap = m_style.gap;
    description.intrinsicSize = {
        Max(0.0f, m_window.metrics.minimumSize.x - description.padding.left - description.padding.right),
        Max(0.0f, m_window.metrics.minimumSize.y - m_window.metrics.titleHeight
            - description.padding.top - description.padding.bottom)
    };
    u32 node = 0u;
    if(!m_layout.addNode(s_LayoutNoParent, description, node)){
        m_context.fail();
        return false;
    }
    m_stack.push_back(node);
    m_panelActive = true;
    m_windowActive = true;
    state = candidate;
    return !state.collapsed;
}

bool Builder::endWindow(){
    if(!m_windowActive || !m_panelActive || !m_window.state || m_stack.size() != 1u || m_context.failed()){
        m_context.fail();
        return false;
    }
    WindowState& state = *m_window.state;
    bool arranged = true;
    if(!state.collapsed){
        const DisplayMetrics& display = m_paint.displayMetrics();
        const Rect viewport{ m_bounds.x, m_bounds.y, Max(m_bounds.width, display.logicalWidth),
            Max(m_bounds.height, display.logicalHeight) };
        arranged = m_layout.arrange(viewport);
        const LayoutBox* root = m_layout.box(0u);
        if(arranged && root){
            if(m_window.firstUse && m_window.options.contentWidthFirstUse)
                state.bounds.width = Max(root->rectangle.width, m_window.metrics.minimumSize.x);
            if(m_window.firstUse && m_window.options.contentHeightFirstUse)
                state.bounds.height = Max(
                    root->rectangle.height + m_window.metrics.titleHeight, m_window.metrics.minimumSize.y
                );
            arranged = WindowBehavior::Constrain(state, display, m_window.metrics.titleHeight);
            m_bounds = WindowLayout::Content(state, m_window.metrics);
            if(arranged)
                arranged = m_layout.arrange(m_bounds);
        }
        else
            arranged = false;
    }
    const bool painted = arranged && paintWindow();
    const bool popped = m_context.popScope();
    m_panelActive = false;
    m_windowActive = false;
    m_window.state = nullptr;
    m_stack.clear();
    const bool combosPainted = painted && popped && paintCombos();
    if(!combosPainted)
        m_context.fail();
    return combosPainted;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

