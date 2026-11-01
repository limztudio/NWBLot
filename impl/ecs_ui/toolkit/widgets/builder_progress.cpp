// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/builder.h>

#include <global/simplemath.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 Builder::progress(const AStringView stableKey, const f64 fraction, const ProgressOptions& options){
    if(itemDeclarationBlocked() || !m_skin){
        m_context.fail();
        return false;
    }
    if(!synchronizePopup())
        return true;
    m_declaring = true;
    ScopeExit finish([this]()noexcept{ m_declaring = false; });
    const auto widget = m_context.declare(stableKey, WidgetKind::Progress);
    if(!widget)
        return false;
    ProgressFrame frame;
    frame.fraction = fraction;
    frame.options = options;
    frame.style = m_progressStyle;
    const UiSkinRegion* track = region(frame.style.track, frame.style.trackFallback);
    const UiSkinRegion* fill = region(frame.style.fill, frame.style.fillFallback);
    if(!IsFinite(fraction) || !track || !fill){
        m_context.fail();
        return false;
    }
    const auto metrics = ProgressLayout::Measure(options, frame.style, *track, *fill, m_skin->referenceDensity());
    if(!metrics){
        m_context.fail();
        return false;
    }
    frame.metrics = *metrics;
    frame.style.track = track->name;
    frame.style.fill = fill->name;
    Item item(m_arena);
    item.state = **widget;
    item.progress = static_cast<u32>(m_scope->m_progress.size());
    LayoutNodeDesc description;
    description.width = frame.options.width;
    description.intrinsicSize = frame.metrics.contentSize;
    if(!addItemNode(description, item))
        return false;
    m_scope->m_progress.push_back(Move(frame));
    m_scope->m_items.push_back(Move(item));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

