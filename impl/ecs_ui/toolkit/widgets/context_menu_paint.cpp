// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../builder.h"
#include "../rect_math.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Builder::paintContextMenus(){
    for(auto& frame : m_scope->m_contextMenus){
        if(!contextMenuMatches(frame) || frame.list >= m_scope->m_lists.size() || frame.anchorIndex >= m_scope->m_items.size())
            return false;
        if(frame.open && frame.options.enabled && !paintContextMenuPopup(frame))
            return false;
    }
    return true;
}

bool Builder::paintContextMenuPopup(ContextMenuFrame& frame){
    const Item& anchor = m_scope->m_items[frame.anchorIndex];
    const LayoutBox* anchorBox = m_scope->m_layout.box(anchor.node);
    const UiSkinRegion* background = region(frame.popupStyle.background, frame.popupStyle.fallback);
    if(!anchorBox || !background || !contextMenuMatches(frame))
        return false;
    const Rect clip = visibleClip(anchorBox->clip);
    if(
        !anchor.enabled || clip.width <= 0.0f || clip.height <= 0.0f
        || anchorBox->rectangle.x >= clip.x + clip.width || anchorBox->rectangle.y >= clip.y + clip.height
        || anchorBox->rectangle.x + anchorBox->rectangle.width <= clip.x || anchorBox->rectangle.y + anchorBox->rectangle.height <= clip.y
    ){
        frame.state->close();
        m_context.discardPopupScope(frame.popupToken);
        snapshotContextMenu(frame);
        return true;
    }
    PopupOptions options;
    options.anchor = frame.state->m_anchor;
    options.size = frame.options.size;
    options.gap = 0.0f;
    const auto placement = PopupLayout::Place(options, m_paint.displayMetrics());
    if(!placement)
        return false;
    PopupScope scope;
    scope.token = frame.popupToken;
    scope.parent = frame.parentToken;
    scope.bounds = placement->bounds;
    scope.viewport = placement->viewport;
    if(!m_context.updatePopupScope(frame.popupToken, scope) || !m_context.activatePopupScope(frame.popupToken))
        return false;
    if(!m_paint.beginOverlay(m_context.popupLayer())){
        const bool ended = m_context.endPopupScope(false);
        if(!ended)
            m_context.fail();
        return false;
    }
    const SIMDVector firstPadding = VectorSet(frame.popupStyle.padding.left, frame.popupStyle.padding.top,
        frame.popupStyle.padding.right, frame.popupStyle.padding.bottom);
    const SIMDVector secondPadding = VectorSet(background->padding.left, background->padding.top,
        background->padding.right, background->padding.bottom);
    const SIMDVector paddingValue = VectorSelect(secondPadding, firstPadding, VectorGreater(firstPadding, secondPadding));
    const Insets padding{ VectorGetX(paddingValue), VectorGetY(paddingValue), VectorGetZ(paddingValue), VectorGetW(paddingValue) };
    m_paint.pushClip(placement->bounds);
    const bool backgroundPainted = m_paint.drawRegion(background->name, placement->bounds);
    const bool popped = m_paint.popClip();
    HitTarget barrier;
    barrier.rectangle = placement->bounds;
    barrier.clip = placement->viewport;
    bool painted = backgroundPainted && popped && m_context.addTarget(frame.popup, barrier);
    if(painted){
        Item item(m_arena);
        item.state = frame.rows;
        item.list = frame.list;
        LayoutBox box;
        const SIMDVector inset = InsetRectValue(
            VectorSet(placement->bounds.x, placement->bounds.y, placement->bounds.width, placement->bounds.height),
            VectorSet(padding.left, padding.top, padding.right, padding.bottom)
        );
        box.rectangle = { VectorGetX(inset), VectorGetY(inset), VectorGetZ(inset), VectorGetW(inset) };
        box.clip = placement->bounds;
        painted = paintList(item, box) && contextMenuMatches(frame);
    }
    const bool overlayEnded = m_paint.endOverlay();
    const bool scopeEnded = m_context.endPopupScope(painted);
    if(painted)
        frame.state->m_popup.m_placement = *placement;
    return painted && overlayEnded && scopeEnded;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

