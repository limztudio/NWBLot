// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../builder.h"

#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Builder::image(const AStringView stableKey, const SharedImageSource& source, const ImageOptions& options){
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
    ImageFrame frame;
    frame.source = source;
    frame.options = options;
    if(!widget || !frame.source){
        m_context.fail();
        return false;
    }
    const auto metrics = ImageLayout::Measure(frame.options, *frame.source);
    if(!metrics){
        m_context.fail();
        return false;
    }
    frame.metrics = *metrics;
    return publishImageFrame(*widget, Move(frame));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

