// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/builder.h>

#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


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
    const UiSkinRegion* skinRegion = m_skin->findRegion(regionName);
    ImageFrame frame;
    frame.options = options;
    if(!widget || !skinRegion){
        m_context.fail();
        return false;
    }
    const auto metrics = ImageLayout::Measure(options, *skinRegion, m_skin->referenceDensity());
    if(!metrics){
        m_context.fail();
        return false;
    }
    frame.metrics = *metrics;
    frame.region = skinRegion->name;
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

