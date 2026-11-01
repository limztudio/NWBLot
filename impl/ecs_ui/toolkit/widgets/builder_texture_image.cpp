// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/builder.h>

#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    const auto metrics = ImageLayout::Measure(frame.options, *frame.source);
    if(!metrics){
        m_context.fail();
        return false;
    }
    frame.metrics = *metrics;
    return publishImageFrame(**widget, Move(frame));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

