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

b8 Builder::paintImage(const Item& item, const LayoutBox& box){
    if(item.image >= m_scope->m_images.size())
        return false;
    const ImageFrame& frame = m_scope->m_images[item.image];
    const auto placement = ImageLayout::Place(box.rectangle, visibleClip(box.clip));
    if(!placement)
        return false;
    m_paint.pushClip(placement->clip);
    const b8 painted = frame.source ? m_paint.drawImage(frame.source, placement->bounds, { 0.0f, 0.0f, 1.0f, 1.0f }, frame.options.tint) : m_paint.drawRegion(frame.region, placement->bounds, frame.options.tint);
    return finishClipPaint(item, placement->bounds, placement->clip, painted);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

