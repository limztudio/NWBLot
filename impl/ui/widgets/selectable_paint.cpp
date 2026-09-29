// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "selectable_paint.h"
#include "../builder.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool SelectablePainter::Paint(
    PaintBuilder& paint, TextService& text, const UiSkin& skin, const TextLayout& layout,
    const Rect& bounds, const Rect& clip, const SelectableStyle& style,
    const SelectablePaintFlags& flags, const Color& textColor, const Color& disabledTextColor
){
    const Name& preferred = !flags.enabled ? style.disabled : flags.selected ? style.selected : flags.hover ? style.hover : style.normal;
    const Name& fallback = !flags.enabled ? style.disabledFallback : flags.selected ? style.selectedFallback
        : flags.hover ? style.hoverFallback : style.fallback;
    const UiSkinRegion* region = skin.findRegion(preferred);
    if(!region)
        region = skin.findRegion(fallback);
    if(!region)
        return false;
    paint.pushClip(clip);
    paint.pushClip(bounds);
    bool painted = paint.drawRegion(region->name, bounds);
    if(painted && flags.enabled && flags.focused && skin.findRegion(style.focus))
        painted = paint.drawRegion(style.focus, bounds);
    const Point origin{ bounds.x + style.padding.left, bounds.y + Max(0.0f, (bounds.height - layout.measure().y) * 0.5f) };
    if(painted)
        painted = text.paint(paint, layout, origin, flags.enabled ? textColor : disabledTextColor);
    const bool rowPopped = paint.popClip();
    const bool clipPopped = paint.popClip();
    return painted && rowPopped && clipPopped;
}

bool Builder::selectable(
    const AStringView stableKey, const StringView text, const bool selected, const WidgetOptions& options){
    Item* item = addItem(stableKey, text, WidgetKind::Selectable, options);
    if(!item)
        return false;
    item->checked = selected;
    return m_context.takeActivation(item->state, item->enabled);
}

bool Builder::paintSelectable(const Item& item, const LayoutBox& box){
    const InputRouter& input = m_context.input();
    const SelectablePaintFlags flags{ item.enabled, item.checked, input.hover() == item.state.id, input.focus() == item.state.id };
    if(!SelectablePainter::Paint(
        m_paint, m_text, *m_skin, item.text, box.rectangle, visibleClip(box.clip), m_listStyle.row, flags, m_style.text, m_style.disabledText
    ))
        return false;
    HitTarget target;
    target.rectangle = box.rectangle;
    target.clip = visibleClip(box.clip);
    target.enabled = item.enabled;
    target.focusable = item.enabled;
    target.activatable = item.enabled;
    target.contextMenu = item.contextMenu;
    return m_context.addTarget(item.state, target);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

