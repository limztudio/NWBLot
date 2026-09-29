// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../builder.h"

#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Builder::image(const AStringView stableKey, const Name& regionName, const ImageOptions& options){
    if(
        declarationBlocked() || !m_scope->m_panelActive || !m_skin || m_context.failed()
        || (m_scope->m_windowActive && m_scope->m_window.state->collapsed) || m_scope->m_items.size() >= s_LayoutMaxNodes
    ){
        m_context.fail();
        return false;
    }
    if(!synchronizePopup())
        return true;
    m_declaring = true;
    ScopeExit finish([this]()noexcept{ m_declaring = false; });
    WidgetState* widget = m_context.declare(stableKey, WidgetKind::Image);
    const UiSkinRegion* skinRegion = m_skin->findRegion(regionName);
    ImageFrame frame;
    frame.options = options;
    if(
        !widget || !skinRegion
        || !ImageLayout::Measure(options, *skinRegion, m_skin->referenceDensity(), frame.metrics)
    ){
        m_context.fail();
        return false;
    }
    frame.region = skinRegion->name;
    Item item(m_arena);
    item.state = *widget;
    item.image = static_cast<u32>(m_scope->m_images.size());
    LayoutNodeDesc description;
    description.width = frame.options.width;
    description.height = frame.options.height;
    description.intrinsicSize = frame.metrics.contentSize;
    if(!m_scope->m_layout.addNode(m_scope->m_stack.back(), description, item.node)){
        m_context.fail();
        return false;
    }
    m_scope->m_images.push_back(Move(frame));
    m_scope->m_items.push_back(Move(item));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

